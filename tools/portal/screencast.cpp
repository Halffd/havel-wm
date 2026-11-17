// ScreenCast for the portal backend: PipeWire source streams fed from
// ext-image-copy-capture frames.
//
// Producer streams (PW_DIRECTION_OUTPUT) on the session PipeWire daemon,
// driven by this process: a timer dequeues a PipeWire buffer, the capture
// session fills it, the buffer goes back. The consumer side (the portal
// frontend, or gst-launch/ffmpeg for verification) connects by node id.
//
// SHM frames only: the compositor renders with pixman here, so it exports
// no DMA-BUF handles and the dmabuf formats the protocol offers cannot be
// used. Advertising only what works keeps consumers honest.
#include "screencast.hpp"

#include <atomic>
#include <mutex>
#include <thread>
#include <chrono>
#include <vector>

#include <pipewire/pipewire.h>
#include <spa/debug/types.h>
#include <spa/param/video/format-utils.h>
#include <spa/param/video/raw.h>
#include <spa/pod/builder.h>

#include "capture.hpp"

namespace havel_portal {

namespace {

constexpr int kTargetFps = 10;

struct Stream {
    pw_stream* handle = nullptr;
    pw_thread_loop* loop = nullptr;
    pw_context* context = nullptr;
    pw_core* core = nullptr;
    CaptureSession* capture = nullptr;
    spa_hook hook = {0};
    std::atomic<bool> stop{false};
    uint32_t width = 0, height = 0;
    uint32_t nodeId = 0;
};

// Capture cadence lives on the PipeWire loop: the wayland round trips run
// there, so a timer on that thread never stalls the D-Bus side. With
// PW_STREAM_FLAG_TRIGGER nothing else schedules a producer stream - the
// node sits suspended and consumers get no frames until it pushes one.
void pumpFrame(void* data, uint64_t expirations);

std::vector<Stream*> g_streams;
std::mutex g_mutex;
bool g_pw_initialized = false;

// Feed one frame into the stream's buffers. Returns false when the stream
// has no buffer available right now (consumer not asking) - a normal,
// non-error condition for a producer.
bool pushFrame(pw_stream* stream, CaptureSession* cap) {
    pw_buffer* buf = pw_stream_dequeue_buffer(stream);
    if (!buf) return false;
    spa_buffer* sb = buf->buffer;
    const uint8_t* src = cap->data();
    const int stride = cap->stride();
    const int height = cap->height();

    if (buf->requested) {
        // Consumer asked for a size: honour it when it fits, else skip.
        buf->size = (uint32_t)buf->requested + 1;
        buf->buffer = sb = &buf->buffer[buf->requested];
        sb->n_datas = 0;
    }
    uint32_t dst = SPA_MIN(sb->datas[0].maxsize, (uint32_t)(stride * height));
    if (dst < (uint32_t)(stride * height)) {
        pw_stream_return_buffer(stream, buf);
        return false;
    }
    void* out = sb->datas[0].data;
    if (!out) {
        pw_stream_return_buffer(stream, buf);
        return false;
    }
    memcpy(out, src, stride * height);
    sb->datas[0].chunk->offset = 0;
    sb->datas[0].chunk->stride = (int32_t)stride;
    sb->datas[0].chunk->size = (uint32_t)(stride * height);
    pw_stream_queue_buffer(stream, buf);
    return true;
}

void onProcess(void* data) {
    auto* st = static_cast<Stream*>(data);
    if (!st->capture || !st->capture->valid()) return;
    std::string err;
    st->capture->captureFrame(err);
    if (!err.empty()) return;
    pushFrame(st->handle, st->capture);
}

// Timer callback: pulls the wayland connection forward WITHOUT waiting on
// it (no round trips) so a capture session with no consumer attached keeps
// its protocol state alive. PipeWire's own process() is the frame source
// when a consumer IS attached - running the capture here too made the
// wayland and PipeWire loops block each other on one thread and the whole
// Handshake deadlocked (observed).
void pumpFrame(void* data, uint64_t expirations) {
    (void)expirations;
    auto* st = static_cast<Stream*>(data);
    if (!st->capture) return;
    st->capture->dispatchPending();
}

void onParamChanged(void* data, uint32_t id, const spa_pod* param) {
    (void)data; (void)id; (void)param;
}

pw_stream_events kStreamEvents = {
    .version = PW_VERSION_STREAM_EVENTS,
    .param_changed = onParamChanged,
    .process = onProcess,
};

// Video format for the compositor's shm frames: ARGB8888 (or XRGB8888)
// is [B][G][R][A] in memory, which is SPA BGRx (x = ignored byte).
uint32_t spaFormatForShm(uint32_t shmFormat) {
    (void)shmFormat;
    return SPA_VIDEO_FORMAT_BGRx;
}

spa_pod* buildFormatParam(std::vector<uint8_t>& buffer, uint32_t width,
                          uint32_t height, uint32_t shmFormat) {
    spa_pod_builder b = SPA_POD_BUILDER_INIT(buffer.data(), (uint32_t)buffer.size());

    spa_pod_frame f;
    spa_pod_builder_push_object(&b, &f, SPA_TYPE_OBJECT_Format,
                                SPA_PARAM_EnumFormat);
    spa_pod_builder_prop(&b, SPA_FORMAT_mediaType, 0);
    spa_pod_builder_id(&b, (uint32_t)SPA_MEDIA_TYPE_video);
    spa_pod_builder_prop(&b, SPA_FORMAT_mediaSubtype, 0);
    spa_pod_builder_id(&b, (uint32_t)SPA_MEDIA_SUBTYPE_raw);
    spa_pod_builder_prop(&b, SPA_FORMAT_VIDEO_format, 0);
    spa_pod_builder_id(&b, spaFormatForShm(shmFormat));
    if (width > 0) {
        spa_pod_builder_prop(&b, SPA_FORMAT_VIDEO_size, 0);
        spa_pod_builder_rectangle(&b, width, height);
    }
    spa_pod_builder_prop(&b, SPA_FORMAT_VIDEO_framerate, 0);
    spa_pod_builder_fraction(&b, kTargetFps, 1);
    spa_pod_builder_prop(&b, SPA_FORMAT_VIDEO_maxFramerate, 0);
    spa_pod_builder_fraction(&b, kTargetFps, 1);
    return reinterpret_cast<spa_pod*>(spa_pod_builder_pop(&b, &f));
}

}  // namespace

bool screencastStart(uint32_t count, std::vector<ScreencastStream>& streams,
                     std::string& err) {
    if (count == 0) count = 1;
    if (!g_pw_initialized) {
        pw_init(nullptr, nullptr);
        g_pw_initialized = true;
    }

    std::lock_guard<std::mutex> lock(g_mutex);
    for (uint32_t i = 0; i < count; i++) {
        Stream* st = new Stream;
        st->capture = new CaptureSession;
        if (!st->capture->open(err)) {
            fprintf(stderr, "screencast: capture open failed: %s\n", err.c_str());
            delete st->capture;
            delete st;
            return false;
        }
        st->width = (uint32_t)st->capture->width();
        st->height = (uint32_t)st->capture->height();

        st->loop = pw_thread_loop_new("havel-screencast", nullptr);
        if (!st->loop) {
            err = "cannot create the PipeWire thread loop";
            delete st->capture;
            delete st;
            return false;
        }
        pw_thread_loop_lock(st->loop);
        st->context = pw_context_new(pw_thread_loop_get_loop(st->loop),
                                     nullptr, 0);
        st->core = st->context ? pw_context_connect(st->context, nullptr, 0)
                              : nullptr;
        if (!st->core) {
            pw_thread_loop_unlock(st->loop);
            pw_thread_loop_destroy(st->loop);
            err = "cannot connect to the PipeWire daemon";
            delete st->capture;
            delete st;
            return false;
        }
        pw_properties* props = pw_properties_new(
            PW_KEY_MEDIA_TYPE, "video",
            PW_KEY_MEDIA_CATEGORY, "Source",
            PW_KEY_MEDIA_ROLE, "Screen",
            nullptr);
        st->handle = pw_stream_new(st->core, "havel-portal-screencast", props);
        if (!st->handle) {
            err = "cannot create the PipeWire stream";
            pw_core_disconnect(st->core);
            pw_thread_loop_unlock(st->loop);
            pw_thread_loop_destroy(st->loop);
            delete st->capture;
            delete st;
            return false;
        }
        spa_zero(st->hook);
        pw_stream_add_listener(st->handle, &st->hook, &kStreamEvents, st);

        // One buffer in flight is enough for a producer streaming at 10fps;
        // PipeWire negotiates its real count with the consumer.
        std::vector<uint8_t> buf(4096);
        const spa_pod* params[1] = {buildFormatParam(buf, st->width, st->height,
                                                     st->capture->shmFormat())};
        // DRIVER (not TRIGGER): a TRIGGER producer is scheduled by nobody, so
        // with no consumer attached it stays suspended and the portal's
        // frames never exist (observed: pwprobe got 0 frames). As DRIVER
        // the graph drives it at the negotiated framerate, and the
        // TRIGGER-based process callback stays the frame source.
        int rc = pw_stream_connect(
            st->handle, PW_DIRECTION_OUTPUT, PW_ID_ANY,
            (pw_stream_flags)(PW_STREAM_FLAG_MAP_BUFFERS | PW_STREAM_FLAG_DRIVER),
            params, 1);
        pw_thread_loop_unlock(st->loop);
        if (rc < 0) {
            err = "cannot connect the PipeWire stream (rc " + std::to_string(rc) + ")";
            pw_stream_destroy(st->handle);
            pw_thread_loop_destroy(st->loop);
            delete st->capture;
            delete st;
            return false;
        }

        // Start the loop only now: connect only queues the negotiation, and
        // reading the node id before the graph has run returns PW_ID_ANY
        // (observed: the portal handed out 4294967295 instead of a node id).
        pw_thread_loop_start(st->loop);
        for (int i = 0; i < 200; i++) {
            pw_thread_loop_lock(st->loop);
            bool got = pw_stream_get_node_id(st->handle) != PW_ID_ANY;
            pw_thread_loop_unlock(st->loop);
            if (got) break;
            usleep(10 * 1000);
        }
        pw_thread_loop_lock(st->loop);
        st->nodeId = pw_stream_get_node_id(st->handle);
        pw_thread_loop_unlock(st->loop);
        if (st->nodeId == PW_ID_ANY) {
            err = "PipeWire never allocated a node id for the stream";
            // Tear down this stream only: a previously started stream in
            // the same call must survive.
            if (st->loop) {
                pw_thread_loop_lock(st->loop);
                if (st->handle) {
                    pw_stream_destroy(st->handle);
                    st->handle = nullptr;
                }
                pw_thread_loop_unlock(st->loop);
                pw_thread_loop_stop(st->loop);
                pw_thread_loop_destroy(st->loop);
            }
            delete st->capture;
            delete st;
            return false;
        }

        // Frames come from a dedicated thread, NOT from the PipeWire loop:
        // the wayland round trips a capture needs cannot nest inside the
        // loop that must service them - that combination deadlocked the
        // ScreenCast handshake (observed). The thread captures, then hands
        // the frame to the PipeWire loop under its lock.
        std::thread([st]() {
            std::string err;
            while (!st->stop.load()) {
                if (!st->capture->captureFrame(err)) {
    
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                    continue;
                }

                pw_thread_loop_lock(st->loop);
                if (st->handle) pushFrame(st->handle, st->capture);
                pw_thread_loop_unlock(st->loop);
                std::this_thread::sleep_for(
                    std::chrono::milliseconds(1000 / kTargetFps));
            }
        }).detach();

        streams.push_back(ScreencastStream{
            st->nodeId, st->nodeId, st->width, st->height,
            (int32_t)i, 0});
        g_streams.push_back(st);
    }
    fprintf(stderr, "screencast: %zu stream(s) started\n", streams.size());
    return true;
}

void screencastStop() {
    std::lock_guard<std::mutex> lock(g_mutex);
    for (Stream* st : g_streams) {
        st->stop.store(true);
        if (st->loop) {
            pw_thread_loop_lock(st->loop);
            if (st->handle) {
                pw_stream_destroy(st->handle);
                st->handle = nullptr;
            }
            pw_thread_loop_unlock(st->loop);
            pw_thread_loop_stop(st->loop);
            pw_thread_loop_destroy(st->loop);
        }
        delete st->capture;
        delete st;
    }
    g_streams.clear();
}

uint32_t screencastStreamCount() {
    std::lock_guard<std::mutex> lock(g_mutex);
    return (uint32_t)g_streams.size();
}

}  // namespace havel_portal