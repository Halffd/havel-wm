// Screen Capture - PipeWire integration
// Provides screen sharing via PipeWire

#pragma once

#include <wayland-server-core.h>
#include <vector>
#include <memory>
#include <string>

struct wlr_output;
struct wlr_output_layout;

namespace havel {

#ifdef HAVE_PIPEWIRE
class PipeWireStream;
#endif

/**
 * Screen Capture Manager
 * 
 * Implements screen capture for:
 * - PipeWire screen sharing (browser, apps)
 * - Screenshots
 * - Screen recording
 * 
 * Integration points:
 * - xdg-desktop-portal-wlr (browser screen sharing)
 * - OBS Studio (recording)
 * - wf-recorder (command line recording)
 */
class ScreenCapture {
public:
    ScreenCapture();
    ~ScreenCapture();

    // Initialize with wl_display and output_layout
    bool initialize(struct wl_display* display, struct wlr_output_layout* layout);
    void shutdown();

    // Capture output to PipeWire stream
    bool captureOutput(struct wlr_output* output, const char* outputPath);
    
    bool isInitialized() const { return m_initialized; }

private:
    struct wl_display* m_display = nullptr;
    struct wlr_output_layout* m_layout = nullptr;
    bool m_initialized = false;

    // Capture session
    struct CaptureSession {
        struct wlr_output* output = nullptr;
        std::string outputPath;
#ifdef HAVE_PIPEWIRE
        PipeWireStream* pwStream = nullptr;
#endif
        bool active = false;
    };

    std::vector<std::unique_ptr<CaptureSession>> m_sessions;

    // Setup listeners
    void setupScreencopyListeners();

    // Frame handlers
    static void handleFrameReady(void* data, struct wl_resource* resource);
    static void handleFrameFailed(void* data, struct wl_resource* resource);
};

} // namespace havel
