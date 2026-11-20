// ext-image-copy-capture-v1 client, shared by the portal backend's
// Screenshot and ScreenCast paths (moved out of havel-portal.cpp when
// ScreenCast needed the same capture).
//
// Protocol notes, both learned the hard way and verified live:
//  - a wl_registry proxy takes exactly ONE listener, so outputs are
//    resolved with a SECOND registry: the first binds the managers, the
//    second turns a wl_output into a capture source
//  - a session holds at most ONE frame object at a time, so a retry (or a
//    second capture) must destroy the previous frame first - creating
//    another trips "session already has a frame object" (error 1), which
//    kills every subsequent attempt
#include "capture.hpp"

#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>

#include <wayland-client.h>
#include <wayland-client-protocol.h>

#include "ext-image-capture-source-v1-client-protocol.h"
#include "ext-image-copy-capture-v1-client-protocol.h"

namespace havel_portal {

namespace {

// ---------------------------------------------------------------------------
// Session listeners (constraints)
// ---------------------------------------------------------------------------
void onBufferSize(void* data, ext_image_copy_capture_session_v1* s,
                  uint32_t width, uint32_t height) {
    (void)s;
    auto* st = static_cast<CaptureState*>(data);
    st->width = width;
    st->height = height;
}

void onShmFormat(void* data, ext_image_copy_capture_session_v1* s, uint32_t format) {
    (void)s;
    auto* st = static_cast<CaptureState*>(data);
    // Prefer ARGB8888: it is the layout every consumer here works in.
    if (!st->haveFormat || format == WL_SHM_FORMAT_ARGB8888) {
        st->shmFormat = format;
        st->haveFormat = true;
    }
}

void onDmabufDevice(void* data, ext_image_copy_capture_session_v1* s, wl_array*) {
    (void)data; (void)s;
}

void onDmabufFormat(void* data, ext_image_copy_capture_session_v1* s, uint32_t, wl_array*) {
    (void)data; (void)s;
}

void onSessionDone(void* data, ext_image_copy_capture_session_v1* s) {
    (void)data; (void)s;
}

void onSessionStopped(void* data, ext_image_copy_capture_session_v1* s) {
    auto* st = static_cast<CaptureState*>(data);
    (void)s;
    st->stopped = true;
    fprintf(stderr, "capture: session stopped\n");
}

const ext_image_copy_capture_session_v1_listener kSessionListener = {
    .buffer_size = onBufferSize,
    .shm_format = onShmFormat,
    .dmabuf_device = onDmabufDevice,
    .dmabuf_format = onDmabufFormat,
    .done = onSessionDone,
    .stopped = onSessionStopped,
};

// ---------------------------------------------------------------------------
// Frame listeners
// ---------------------------------------------------------------------------
void onFrameTransform(void* d, ext_image_copy_capture_frame_v1* f, uint32_t) {
    (void)d; (void)f;
}

void onFrameDamage(void* d, ext_image_copy_capture_frame_v1* f, int32_t, int32_t,
                   int32_t, int32_t) {
    (void)d; (void)f;
}

void onFramePresentationTime(void* d, ext_image_copy_capture_frame_v1* f,
                             uint32_t, uint32_t, uint32_t) {
    (void)d; (void)f;
}

void onFrameReady(void* d, ext_image_copy_capture_frame_v1* f) {
    (void)f;
    auto* st = static_cast<CaptureState*>(d);
    st->frameReady = true;
}

void onFrameFailed(void* d, ext_image_copy_capture_frame_v1* f, uint32_t reason) {
    (void)f;
    auto* st = static_cast<CaptureState*>(d);
    st->frameFailed = true;
    fprintf(stderr, "capture: frame failed (reason %u)\n", reason);
}

const ext_image_copy_capture_frame_v1_listener kFrameListener = {
    .transform = onFrameTransform,
    .damage = onFrameDamage,
    .presentation_time = onFramePresentationTime,
    .ready = onFrameReady,
    .failed = onFrameFailed,
};

// ---------------------------------------------------------------------------
// Registry binds
// ---------------------------------------------------------------------------
void bindGlobals(void* data, wl_registry* registry, uint32_t name,
                 const char* interface, uint32_t version) {
    auto* st = static_cast<CaptureState*>(data);
    (void)version;
    if (strcmp(interface, wl_shm_interface.name) == 0) {
        st->shm = static_cast<wl_shm*>(
            wl_registry_bind(registry, name, &wl_shm_interface, 1));
    } else if (strcmp(interface, ext_image_copy_capture_manager_v1_interface.name) == 0) {
        st->manager = static_cast<ext_image_copy_capture_manager_v1*>(
            wl_registry_bind(registry, name, &ext_image_copy_capture_manager_v1_interface, 1));
    } else if (strcmp(interface,
                      ext_output_image_capture_source_manager_v1_interface.name) == 0) {
        st->sourceManager =
            static_cast<ext_output_image_capture_source_manager_v1*>(
                wl_registry_bind(registry, name,
                                 &ext_output_image_capture_source_manager_v1_interface, 1));
    }
}

void bindGlobalsRemove(void* d, wl_registry* r, uint32_t name) {
    (void)d; (void)r; (void)name;
}

const wl_registry_listener kRegistryListener = {
    .global = bindGlobals,
    .global_remove = bindGlobalsRemove,
};

void createSource(void* data, wl_registry* registry, uint32_t name,
                  const char* interface, uint32_t version) {
    auto* st = static_cast<CaptureState*>(data);
    (void)version;
    if (st->source || strcmp(interface, wl_output_interface.name) != 0) return;
    wl_output* output = static_cast<wl_output*>(
        wl_registry_bind(registry, name, &wl_output_interface, 1));
    if (!output) return;
    st->source = ext_output_image_capture_source_manager_v1_create_source(
        st->sourceManager, output);
}

const wl_registry_listener kOutputRegistryListener = {
    .global = createSource,
    .global_remove = bindGlobalsRemove,
};

uint32_t fourccToShmFormat(uint32_t fourcc) {
    if (fourcc == 0x34325241) return WL_SHM_FORMAT_ARGB8888;  // 'ARG8' little-endian
    if (fourcc == 0x34325258) return WL_SHM_FORMAT_XRGB8888;  // 'XRG8'
    return WL_SHM_FORMAT_ARGB8888;
}

}  // namespace

CaptureSession::CaptureSession() : m_state(new CaptureState) {}
CaptureSession::~CaptureSession() { close(); }

bool CaptureSession::open(std::string& err) {
    CaptureState& st = *m_state;
    st.display = wl_display_connect(nullptr);
    if (!st.display) {
        err = "cannot connect to the wayland display (WAYLAND_DISPLAY?)";
        return false;
    }
    st.registry = wl_display_get_registry(st.display);
    wl_registry_add_listener(st.registry, &kRegistryListener, &st);
    wl_display_roundtrip(st.display);
    if (!st.manager) {
        err = "compositor exposes no ext_image_copy_capture_manager_v1";
        return false;
    }
    if (!st.sourceManager) {
        err = "compositor exposes no ext_output_image_capture_source_manager_v1";
        return false;
    }
    // Second registry: resolve the first wl_output into a capture source.
    st.outputRegistry = wl_display_get_registry(st.display);
    wl_registry_add_listener(st.outputRegistry, &kOutputRegistryListener, &st);
    wl_display_roundtrip(st.display);
    if (!st.source) {
        err = "no capture source could be created (no outputs?)";
        return false;
    }

    // A compositor needs a fresh output frame to serve a capture, and a
    // static desktop produces no damage - so a schedule here at session
    // creation plus a retry loop in captureFrame (same as the compositor
    // side, which also schedules a frame per new session).
    st.session = ext_image_copy_capture_manager_v1_create_session(
        st.manager, st.source, 0);
    if (!st.session) {
        err = "create_session failed";
        return false;
    }
    ext_image_copy_capture_session_v1_add_listener(st.session, &kSessionListener, &st);
    for (int i = 0; i < 200 && !st.haveFormat && !st.stopped; i++) {
        wl_display_roundtrip(st.display);
    }
    if (!st.haveFormat || st.width == 0 || st.height == 0) {
        err = "capture session advertised no format/size";
        return false;
    }

    uint32_t fmt = fourccToShmFormat(st.shmFormat);
    int32_t bpp = (fmt == WL_SHM_FORMAT_ARGB8888 || fmt == WL_SHM_FORMAT_XRGB8888) ? 4 : 0;
    if (bpp != 4) {
        err = "unsupported capture shm format 0x" + std::to_string(st.shmFormat);
        return false;
    }
    st.shmFormat = fmt;
    int32_t stride = (int32_t)st.width * 4;
    st.shmSize = (size_t)stride * st.height;
    st.fd = memfd_create("havel-portal-capture", MFD_CLOEXEC);
    if (st.fd < 0) {
        err = "memfd_create failed";
        return false;
    }
    if (ftruncate(st.fd, (off_t)st.shmSize) != 0) {
        err = "ftruncate failed";
        close();
        return false;
    }
    st.shmData = mmap(nullptr, st.shmSize, PROT_READ | PROT_WRITE,
                      MAP_SHARED, st.fd, 0);
    if (st.shmData == MAP_FAILED) {
        st.shmData = nullptr;
        close();
        err = "mmap failed";
        return false;
    }
    wl_shm_pool* pool = wl_shm_create_pool(st.shm, st.fd, (int32_t)st.shmSize);
    st.buffer = wl_shm_pool_create_buffer(pool, 0, (int32_t)st.width,
                                           (int32_t)st.height, stride, st.shmFormat);
    wl_shm_pool_destroy(pool);
    if (!st.buffer) {
        err = "create_buffer failed";
        close();
        return false;
    }
    return true;
}

bool CaptureSession::captureFrame(std::string& err) {
    CaptureState& st = *m_state;
    if (!valid()) {
        err = "capture session not open";
        return false;
    }
    if (st.stopped) {
        err = "capture session stopped";
        return false;
    }
    // One frame object per session at a time: destroy the previous one
    // before creating the next.
    if (st.frame) {
        ext_image_copy_capture_frame_v1_destroy(st.frame);
        st.frame = nullptr;
    }
    st.frameReady = false;
    st.frameFailed = false;
    st.frame = ext_image_copy_capture_session_v1_create_frame(st.session);
    if (!st.frame) {
        err = "create_frame failed";
        return false;
    }
    ext_image_copy_capture_frame_v1_add_listener(st.frame, &kFrameListener, &st);
    ext_image_copy_capture_frame_v1_attach_buffer(st.frame, st.buffer);
    ext_image_copy_capture_frame_v1_capture(st.frame);

    for (int i = 0; i < 200 && !st.frameReady && !st.frameFailed && !st.stopped; i++) {
        wl_display_roundtrip(st.display);
    }
    if (st.frameFailed) {
        err = "capture failed";
        return false;
    }
    if (st.stopped) {
        err = "capture session stopped";
        return false;
    }
    if (!st.frameReady) {
        // No frame yet (no damage on a quiet desktop): the caller retries,
        // and the retry creates a fresh frame exactly as above.
        err = "no frame";
        return false;
    }
    return true;
}

void CaptureSession::close() {
    CaptureState& st = *m_state;
    if (st.frame) {
        ext_image_copy_capture_frame_v1_destroy(st.frame);
        st.frame = nullptr;
    }
    if (st.buffer) {
        wl_buffer_destroy(st.buffer);
        st.buffer = nullptr;
    }
    if (st.session) {
        ext_image_copy_capture_session_v1_destroy(st.session);
        st.session = nullptr;
    }
    if (st.source) {
        ext_image_capture_source_v1_destroy(st.source);
        st.source = nullptr;
    }
    if (st.shmData) {
        munmap(st.shmData, st.shmSize);
        st.shmData = nullptr;
        st.shmSize = 0;
    }
    if (st.fd >= 0) {
        // ::close: CaptureSession::close is in scope here and would win.
        ::close(st.fd);
        st.fd = -1;
    }
    if (st.registry) {
        wl_registry_destroy(st.registry);
        st.registry = nullptr;
    }
    if (st.display) {
        wl_display_disconnect(st.display);
        st.display = nullptr;
    }
}

bool CaptureSession::valid() const { return m_state->session && m_state->buffer; }

const uint8_t* CaptureSession::data() const {
    return m_state->shmData ? static_cast<const uint8_t*>(m_state->shmData)
                            : nullptr;
}
int CaptureSession::width() const { return (int)m_state->width; }
int CaptureSession::height() const { return (int)m_state->height; }
int CaptureSession::stride() const { return (int)m_state->width * 4; }
uint32_t CaptureSession::shmFormat() const { return m_state->shmFormat; }

}  // namespace havel_portal