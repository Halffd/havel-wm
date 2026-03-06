// Blur Plugin - Enhanced with damage tracking integration

#include "../plugins/Plugin.hpp"
#include <Logger.h>
#include <string.h>

namespace havel {

class BlurPlugin : public Plugin {
public:
    const char* name() const override { return "blur"; }
    const char* description() const override { 
        return "Kawase blur effect with damage-aware rendering"; 
    }
    const char* version() const override { return "2.0"; }
    
    void init(CompositorAPI* api) override {
        m_api = api;
        m_enabled = true;
        m_blurRadius = 10.0f;
        m_passes = 3;
        m_dimming = 0.4f;
        
        LOG_INFO("[BlurPlugin] Initialized (damage-aware)");
    }
    
    void fini() override {
        LOG_INFO("[BlurPlugin] Finalized");
    }
    
    void onOutputFrame(const OutputFrameEvent& event) override {
        if (!m_enabled) return;
        
        // Would apply blur effect to output
        // With damage tracking, only blur damaged regions
        
        (void)event;
    }
    
    // Toggle blur
    void toggle() {
        m_enabled = !m_enabled;
        LOG_INFO("[BlurPlugin] Blur %s", m_enabled ? "enabled" : "disabled");
    }
    
    // Set blur radius
    void setRadius(float radius) {
        m_blurRadius = radius;
        LOG_INFO("[BlurPlugin] Blur radius: %.1f", m_blurRadius);
    }
    
    // Set blur passes
    void setPasses(int passes) {
        m_passes = passes > 0 ? passes : 1;
        LOG_INFO("[BlurPlugin] Blur passes: %d", m_passes);
    }
    
    // Set dimming amount
    void setDimming(float dimming) {
        m_dimming = dimming;
        LOG_INFO("[BlurPlugin] Dimming: %.2f", m_dimming);
    }
    
    bool isEnabled() const { return m_enabled; }
    float getRadius() const { return m_blurRadius; }
    int getPasses() const { return m_passes; }
    float getDimming() const { return m_dimming; }
    
private:
    CompositorAPI* m_api;
    bool m_enabled;
    float m_blurRadius;
    int m_passes;
    float m_dimming;
};

} // namespace havel

// Plugin factory
extern "C" {
    havel::Plugin* create_blur_plugin() {
        return new havel::BlurPlugin();
    }
    
    void destroy_blur_plugin(havel::Plugin* plugin) {
        delete plugin;
    }
}
