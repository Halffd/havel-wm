// ScreenCast for the portal backend: PipeWire source streams fed from
// ext-image-copy-capture frames.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace havel_portal {

struct ScreencastStream {
    uint32_t nodeId = 0;    // PipeWire node id (deprecated for targeting,
                            // but still what a consumer without the serial
                            // property uses)
    uint32_t serial = 0;    // pipewire-serial, the modern targeting key
    uint32_t width = 0, height = 0;
    int32_t x = 0, y = 0;
};

// Start `count` streams (one per output) of the compositor's desktop.
// The node ids are registered in the compositor's own PipeWire daemon, so
// any consumer on the session bus - the portal frontend, gst-launch,
// ffmpeg - can connect to them by node id.
// Returns false with a reason; `streams` is then empty.
bool screencastStart(uint32_t count, std::vector<ScreencastStream>& streams,
                     std::string& err);
void screencastStop();

// Number of streams successfully started (0 when none).
uint32_t screencastStreamCount();

}  // namespace havel_portal