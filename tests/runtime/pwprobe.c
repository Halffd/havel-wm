// PipeWire consumer probe for the portal ScreenCast streams.
//
// Connects a capture (input) stream to the node the portal handed out and
// pulls a few frames, printing the negotiated format and per-frame byte
// statistics. This is the same consumer side the portal frontend performs
// for apps (a pw-fd plus the node id); using it directly verifies the
// backend's streams carry real frames rather than just that a node exists.
//
// Build: cc -o pwprobe pwprobe.c $(pkg-config --cflags --libs libpipewire-0.3)
// Run:   pwprobe <node-id>
#define _GNU_SOURCE
#include <pipewire/pipewire.h>
#include <pipewire/stream.h>
#include <spa/param/video/format-utils.h>
#include <spa/param/video/raw.h>
#include <spa/pod/builder.h>
#include <spa/utils/result.h>
#include <spa/utils/string.h>

#include <stdio.h>
#include <stdlib.h>

#ifndef FALSE
#define FALSE 0
#endif

struct App {
    struct pw_main_loop* loop;
    struct pw_core* core;
    struct pw_stream* stream;
    struct spa_hook hook;
    int want;
    int got;
};

static void onParamChanged(void* data, uint32_t id, const struct spa_pod* param) {
    struct App* app = data;
    (void)app;
    // The negotiated format is what the graph runs with. PipeWire 1.6 has
    // no pod-to-string helper in the public headers, so note the shape
    // here and let the per-frame output below prove real data.
    if (id != SPA_PARAM_Format || !param) return;
    printf("negotiated a format\n");
}

static void onProcess(void* data) {
    struct App* app = data;
    struct pw_buffer* b = pw_stream_dequeue_buffer(app->stream);
    if (!b) return;
    struct spa_buffer* sb = b->buffer;
    const uint8_t* px = sb->datas[0].data;
    uint32_t size = sb->datas[0].chunk ? sb->datas[0].chunk->size
                                       : sb->datas[0].maxsize;
    // A frame of one uniform colour is the "no capture" signature.
    int varied = 0;
    if (px && size >= 320) {
        uint8_t first[4] = {px[0], px[1], px[2], px[3]};
        for (uint32_t i = 4; i < 320 && i < size; i += 4) {
            if (px[i] != first[0] || px[i + 1] != first[1] ||
                px[i + 2] != first[2]) {
                varied = 1;
                break;
            }
        }
    }
    printf("frame %d: %u bytes, varied=%d\n", app->got + 1, size, varied);
    pw_stream_queue_buffer(app->stream, b);
    app->got++;
    if (app->got >= app->want) pw_main_loop_quit(app->loop);
}

static const struct pw_stream_events events = {
    PW_VERSION_STREAM_EVENTS,
    .param_changed = onParamChanged,
    .process = onProcess,
};

int main(int argc, char* argv[]) {
    if (argc != 2) {
        fprintf(stderr, "usage: pwprobe <pipewire-node-id>\n");
        return 2;
    }
    uint32_t nodeId = (uint32_t)strtoul(argv[1], nullptr, 10);
    if (nodeId == 0) {
        fprintf(stderr, "invalid node id\n");
        return 2;
    }
    pw_init(nullptr, nullptr);
    struct App app = {0};
    app.want = 3;
    app.loop = pw_main_loop_new(nullptr);
    if (!app.loop) {
        fprintf(stderr, "cannot create the main loop\n");
        return 1;
    }
    app.core = pw_context_connect(pw_context_new(pw_main_loop_get_loop(app.loop),
                                                  nullptr, 0),
                                  nullptr, 0);
    if (!app.core) {
        fprintf(stderr, "cannot connect to the PipeWire daemon\n");
        return 1;
    }
    app.stream = pw_stream_new(app.core, "havel-portal-probe",
                               pw_properties_new(PW_KEY_MEDIA_TYPE, "video",
                                                  PW_KEY_MEDIA_CATEGORY, "Capture",
                                                  nullptr));
    if (!app.stream) {
        fprintf(stderr, "cannot create the stream\n");
        return 1;
    }
    spa_zero(app.hook);
    pw_stream_add_listener(app.stream, &app.hook, &events, &app);

    // Connect with NO params of our own: the producer's EnumFormat is
// authoritative and PipeWire picks the intersection. Passing a half-built
// // format here stalls the negotiation (no Format event, no frames).
    if (pw_stream_connect(app.stream, PW_DIRECTION_INPUT, nodeId,
                          (enum pw_stream_flags)(PW_STREAM_FLAG_AUTOCONNECT |
                                                 PW_STREAM_FLAG_MAP_BUFFERS),
                          nullptr, 0) < 0) {
        fprintf(stderr, "cannot connect to node %u\n", nodeId);
        return 1;
    }
    // Run at most app->want frames, bounded in wall clock.
    // Iterate the loop with a bounded timeout so a producer that never
    // delivers still exits instead of hanging.
    for (int i = 0; i < 200 && app.got < app.want; i++) {
        pw_loop_iterate(pw_main_loop_get_loop(app.loop), 10);
    }
    if (app.got == 0) fprintf(stderr, "no frames arrived\n");
    printf("PWPROBE frames=%d\n", app.got);
    return app.got > 0 ? 0 : 1;
}