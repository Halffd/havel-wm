// Screen Capture - PipeWire integration (stub for future expansion)
// Full implementation requires wlroots with screencopy support

#include "ScreenCapture.hpp"
#include "PipeWireStream.hpp"
#include <Logger.h>
#include <cstdio>

namespace havel {

ScreenCapture::ScreenCapture() = default;

ScreenCapture::~ScreenCapture() {
    shutdown();
}

bool ScreenCapture::initialize(struct wl_display* display, struct wlr_output_layout* layout) {
    if (m_initialized) return true;

    m_display = display;
    m_layout = layout;

    // Initialize PipeWire manager for screen sharing
    // Note: Full wlroots screencopy integration requires:
    // 1. wlr_screencopy_manager_v1_create() - may not be available in all wlroots versions
    // 2. Frame capture handlers
    // 3. DMA-BUF buffer export
    //
    // For now, we initialize PipeWire directly which enables:
    // - Screen sharing via xdg-desktop-portal-wlr
    // - Direct PipeWire stream creation
    
    PipeWireManager::getInstance().initialize();

    m_initialized = true;
    LOG_INFO("[ScreenCapture] Screen capture initialized");
    LOG_INFO("[ScreenCapture] PipeWire screen sharing ready");
    return true;
}

void ScreenCapture::shutdown() {
    // Destroy PipeWire streams
    PipeWireManager::getInstance().shutdown();

    m_sessions.clear();
    m_initialized = false;
}

bool ScreenCapture::captureOutput(struct wlr_output* output, const char* outputPath) {
    if (!m_initialized || !output) return false;

    LOG_INFO("[ScreenCapture] Capturing output: %s", outputPath);

#ifdef HAVE_PIPEWIRE
    // Create PipeWire stream for this output
    auto* pwStream = PipeWireManager::getInstance().createStream(output, outputPath);
    if (!pwStream) {
        LOG_ERROR("[ScreenCapture] Failed to create PipeWire stream for %s", outputPath);
        return false;
    }

    // Create capture session
    auto session = std::make_unique<CaptureSession>();
    session->output = output;
    session->outputPath = outputPath;
    session->pwStream = pwStream;
    session->active = true;

    m_sessions.push_back(std::move(session));
    return true;
#else
    // Without PipeWire, just log the request
    LOG_INFO("[ScreenCapture] Capture requested for %s (PipeWire not available)", outputPath);
    return true;
#endif
}

void ScreenCapture::setupScreencopyListeners() {
    // wlroots screencopy manager handles protocol internally
    // Applications connect via the Wayland protocol
}

// Frame handlers
void ScreenCapture::handleFrameReady(void* data, struct wl_resource* resource) {
    (void)data;
    (void)resource;
}

void ScreenCapture::handleFrameFailed(void* data, struct wl_resource* resource) {
    (void)data;
    (void)resource;
    LOG_WARN("[ScreenCapture] Frame capture failed");
}

} // namespace havel
