// ext-image-copy-capture-v1 client, shared by the portal backend's
// Screenshot (one frame) and ScreenCast (continuous) paths.
//
// The session is opened once and kept open: ScreenCast captures at its own
// cadence, and re-running session setup per frame would pay for protocol
// round trips that buy nothing. Screenshot just takes one frame from a
// session it opens for the call.
//
// The frame lands in a persistent wl_shm buffer the compositor writes
// into; data() points at it and stays valid until the next captureFrame,
// so a consumer can copy it straight into its own target (the PipeWire
// buffer for ScreenCast) without an intermediate copy.
#pragma once

#include <cstdint>
#include <cstddef>
#include <memory>
#include <string>

struct wl_display;
struct wl_registry;
struct wl_shm;
struct wl_buffer;
struct ext_image_copy_capture_manager_v1;
struct ext_image_copy_capture_session_v1;
struct ext_image_copy_capture_frame_v1;
struct ext_image_capture_source_v1;
struct ext_output_image_capture_source_manager_v1;

namespace havel_portal {

// Wayland/protocol state for one CaptureSession. Declared at namespace
// scope so the C protocol callbacks can take it as their data pointer
// without the class exposing its internals.
struct CaptureState {
    // wayland
    wl_display* display = nullptr;
    wl_registry* registry = nullptr;
    wl_registry* outputRegistry = nullptr;
    wl_shm* shm = nullptr;
    ext_image_copy_capture_manager_v1* manager = nullptr;
    ext_output_image_capture_source_manager_v1* sourceManager = nullptr;
    ext_image_capture_source_v1* source = nullptr;
    ext_image_copy_capture_session_v1* session = nullptr;
    // session state
    uint32_t width = 0, height = 0, shmFormat = 0;
    bool haveFormat = false;
    bool stopped = false;
    // persistent shm buffer the compositor writes the frame into
    wl_buffer* buffer = nullptr;
    void* shmData = nullptr;
    size_t shmSize = 0;
    int fd = -1;
    // per-frame state
    ext_image_copy_capture_frame_v1* frame = nullptr;
    bool frameReady = false;
    bool frameFailed = false;
};

class CaptureSession {
public:
    CaptureSession();
    ~CaptureSession();
    CaptureSession(const CaptureSession&) = delete;
    CaptureSession& operator=(const CaptureSession&) = delete;

    // Connect to the compositor and create a capture session for the first
    // output source. False with a reason on failure.
    bool open(std::string& err);
    void close();

    // Capture one frame. The session's size and shm format are known after
    // open(); this only drives one frame through it.
    bool captureFrame(std::string& err);

    // Pump the wayland connection without blocking on it (no round trips):
    // keeps a session's protocol state alive while something else owns the
    // blocking waits. Used from the PipeWire thread, which cannot nest
    // wayland round trips inside PipeWire's loop.
    void dispatchPending();

    bool valid() const;
    const uint8_t* data() const;
    int width() const;
    int height() const;
    int stride() const;          // bytes per row
    uint32_t shmFormat() const;  // WL_SHM_FORMAT_*, for SPA format choice

private:
    std::unique_ptr<CaptureState> m_state;
};

}  // namespace havel_portal
