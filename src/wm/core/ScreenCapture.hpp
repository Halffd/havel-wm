// Screen Capture - PipeWire screencopy support for screen sharing
// Implements wlr-screencopy-unstable-v1 protocol

#pragma once

#include <wayland-server-core.h>
#include <wlr/types/wlr_screencopy_v1.h>
#include <vector>
#include <memory>
#include <string>

namespace havel {

/**
 * Screen Capture Manager
 * 
 * Implements screen capture for:
 * - PipeWire screen sharing (browser, apps)
 * - Screenshots
 * - Screen recording
 * 
 * Uses wlr-screencopy-unstable-v1 protocol
 * which is supported by:
 * - OBS Studio
 * - Firefox/Chrome (PipeWire)
 * - wf-recorder
 * - grim/slurp
 */
class ScreenCapture {
public:
    ScreenCapture();
    ~ScreenCapture();

    // Initialize with wl_display
    bool initialize(struct wl_display* display, struct wlr_output_layout* layout);
    void shutdown();

    // Take screenshot of output
    bool captureOutput(struct wlr_output* output, const char* outputPath);
    
    // Get screencopy manager handle
    struct wlr_screencopy_manager_v1* getManager() const { return m_manager; }

    bool isInitialized() const { return m_initialized; }

private:
    struct wl_display* m_display = nullptr;
    struct wlr_output_layout* m_layout = nullptr;
    struct wlr_screencopy_manager_v1* m_manager = nullptr;
    bool m_initialized = false;

    // Capture state
    struct CaptureSession {
        struct wlr_screencopy_frame_v1* frame = nullptr;
        struct wlr_output* output = nullptr;
        std::string outputPath;
        bool active = false;
    };

    std::vector<std::unique_ptr<CaptureSession>> m_sessions;

    // Frame handlers
    static void handleFrameReady(void* data, struct wl_resource* resource);
    static void handleFrameFailed(void* data, struct wl_resource* resource);
};

} // namespace havel
