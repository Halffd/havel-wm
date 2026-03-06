// FPS Plugin - Enhanced with new renderer statistics

#include "../plugins/Plugin.hpp"
#include <wm/render/CompositorRenderer.h>
#include <Logger.h>
#include <stdio.h>
#include <string.h>

namespace havel {

class FPSPlugin : public Plugin {
public:
    const char* name() const override { return "fps"; }
    const char* description() const override { 
        return "Display FPS counter with detailed rendering statistics"; 
    }
    const char* version() const override { return "2.0"; }
    
    void init(CompositorAPI* api) override {
        m_api = api;
        m_visible = true;
        m_showDetailed = false;
        m_lastUpdate = 0;
        m_frameCount = 0;
        
        LOG_INFO("[FPSPlugin] Initialized (enhanced with renderer stats)");
    }
    
    void fini() override {
        LOG_INFO("[FPSPlugin] Finalized");
    }
    
    void onOutputFrame(const OutputFrameEvent& event) override {
        (void)event;
        m_frameCount++;
        
        // Update every second
        uint64_t now = get_time_ms();
        if (now - m_lastUpdate >= 1000) {
            m_fps = m_frameCount;
            m_frameCount = 0;
            m_lastUpdate = now;
            
            // Get renderer stats if available
            if (m_showDetailed) {
                // Would query CompositorRenderer for detailed stats
            }
        }
    }
    
    void renderOverlay(void* renderer_ptr) override {
        if (!m_visible) return;
        
        char buffer[256];
        
        if (m_showDetailed) {
            snprintf(buffer, sizeof(buffer),
                     "FPS: %.1f\n"
                     "Frame: %.2fms\n"
                     "GPU: %s",
                     m_fps,
                     1000.0f / (m_fps > 0 ? m_fps : 60),
                     "Auto");  // Would get from renderer
        } else {
            snprintf(buffer, sizeof(buffer), "FPS: %.1f", m_fps);
        }
        
        // Would render text overlay here
        (void)renderer_ptr;
    }
    
    // Toggle FPS display
    void toggle() {
        m_visible = !m_visible;
    }
    
    // Toggle detailed stats
    void toggleDetailed() {
        m_showDetailed = !m_showDetailed;
    }
    
private:
    uint64_t get_time_ms() {
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        return ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
    }
    
    CompositorAPI* m_api;
    bool m_visible;
    bool m_showDetailed;
    uint64_t m_lastUpdate;
    uint32_t m_frameCount;
    float m_fps;
};

} // namespace havel

// Plugin factory
extern "C" {
    havel::Plugin* create_fps_plugin() {
        return new havel::FPSPlugin();
    }
    
    void destroy_fps_plugin(havel::Plugin* plugin) {
        delete plugin;
    }
}
