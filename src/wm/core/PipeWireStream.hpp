// PipeWire Stream Manager - Full screen sharing integration
// Implements PipeWire streaming for browser screen sharing and recording

#pragma once

#ifdef HAVE_PIPEWIRE

#include <pipewire/pipewire.h>
#include <spa/param/video/format-utils.h>
#include <spa/buffer/meta.h>
#include <vector>
#include <memory>
#include <string>
#include <functional>

namespace havel {

/**
 * PipeWire Stream Manager
 * 
 * Provides full PipeWire integration for:
 * - Browser screen sharing (Firefox, Chrome via xdg-desktop-portal)
 * - Screen recording (OBS, wf-recorder)
 * - Screenshot tools (grim, slurp)
 * 
 * Features:
 * - DMA-BUF buffer export
 * - Multiple stream support
 * - Dynamic resolution scaling
 * - Frame rate control
 * - Cursor compositing option
 */
class PipeWireStream {
public:
    PipeWireStream();
    ~PipeWireStream();

    // Initialize PipeWire main loop
    bool initialize();
    void shutdown();

    // Create stream for output
    bool createStream(struct wlr_output* output, const char* outputName);
    
    // Destroy stream
    void destroyStream();

    // Push frame to PipeWire
    bool pushFrame(struct wlr_buffer* buffer, uint32_t width, uint32_t height);

    // Get stream state
    bool isActive() const { return m_active; }
    uint32_t nodeId() const { return m_nodeId; }

    // Callbacks
    using StateCallback = std::function<void(bool active)>;
    void setStateCallback(StateCallback callback) { m_stateCallback = callback; }

private:
    struct pw_main_loop* m_loop = nullptr;
    struct pw_context* m_context = nullptr;
    struct pw_core* m_core = nullptr;
    struct pw_stream* m_stream = nullptr;
    struct pw_listener m_coreListener;
    
    uint32_t m_nodeId = 0;
    bool m_active = false;
    bool m_initialized = false;

    StateCallback m_stateCallback;

    // Stream parameters
    struct spa_video_info_raw m_videoInfo;
    uint32_t m_width = 0;
    uint32_t m_height = 0;

    // PipeWire event listeners
    static void onCoreDone(void* userdata, uint32_t id, uint32_t version);
    static void onStreamStateChanged(void* userdata, pw_stream_state old, 
                                      pw_stream_state state, const char* error);
    static void onStreamProcess(void* userdata);
    static void onStreamParamChanged(void* userdata, uint32_t id,
                                      const struct spa_pod* param);
    static void onStreamBufferAdded(void* userdata, uint32_t id,
                                     struct pw_buffer* buffer);
    static void onStreamBufferRemoved(void* userdata, uint32_t id,
                                       struct pw_buffer* buffer);

    static const struct pw_core_events s_coreEvents;
    static const struct pw_stream_events s_streamEvents;
};

/**
 * PipeWire Manager - Manages multiple streams
 */
class PipeWireManager {
public:
    static PipeWireManager& getInstance();

    bool initialize();
    void shutdown();

    // Create/destroy streams
    PipeWireStream* createStream(struct wlr_output* output, const char* outputName);
    void destroyStream(PipeWireStream* stream);

    // Get all active streams
    const std::vector<std::unique_ptr<PipeWireStream>>& getStreams() const { 
        return m_streams; 
    }

    bool isInitialized() const { return m_initialized; }

private:
    PipeWireManager() = default;
    ~PipeWireManager() { shutdown(); }

    std::vector<std::unique_ptr<PipeWireStream>> m_streams;
    bool m_initialized = false;
};

} // namespace havel

#else // HAVE_PIPEWIRE

// Stub implementation when PipeWire is not available
namespace havel {

class PipeWireStream {
public:
    bool initialize() { return false; }
    void shutdown() {}
    bool isActive() const { return false; }
};

class PipeWireManager {
public:
    static PipeWireManager& getInstance() {
        static PipeWireManager instance;
        return instance;
    }
    bool initialize() { return false; }
    void shutdown() {}
    bool isInitialized() const { return false; }
};

} // namespace havel

#endif // HAVE_PIPEWIRE
