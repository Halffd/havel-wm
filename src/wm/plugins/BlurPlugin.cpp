// Blur Plugin - demonstrates render hook capability
// Note: Actual blur requires GLES2 shader and aux buffer
// This is a stub showing the plugin architecture

#include <wm/plugins/Plugin.hpp>
#include <wm/plugins/CompositorAPI.hpp>
#include <cstdio>

namespace havel {

/**
 * Blur Plugin
 * 
 * Demonstrates render hook for post-processing effects.
 * Actual blur implementation requires:
 * - GLES2 shader with Gaussian blur kernel
 * - Aux buffer for intermediate rendering
 * - Proper render pass integration
 * 
 * This stub shows the plugin structure.
 */
class BlurPlugin : public Plugin {
public:
    const char* name() const override { return "blur"; }
    const char* version() const override { return "0.1.0"; }
    
    void init(CompositorAPI* api) override {
        m_api = api;
        m_enabled = false;
        printf("[BlurPlugin] Initialized (blur requires GLES2 shader)\n");
    }
    
    void fini() override {
        printf("[BlurPlugin] Finalized\n");
        m_api = nullptr;
    }
    
    void loadConfig(const std::string& configPath) override {
        // Would load blur radius, enabled state, etc.
        (void)configPath;
        printf("[BlurPlugin] Config loaded from: %s\n", configPath.c_str());
    }
    
    void renderOverlay(void* renderPass) override {
        if (!m_enabled) return;
        
        // Actual blur implementation would:
        // 1. Allocate aux buffer
        // 2. Render scene to aux buffer
        // 3. Apply blur shader
        // 4. Composite back to main pass
        
        // Stub for now
        (void)renderPass;
    }
    
    bool onKey(const KeyEvent& event) override {
        constexpr uint32_t MOD_LOGO = 1 << 6;
        
        // Meta+B toggles blur
        if (event.pressed && (event.modifiers & MOD_LOGO) && event.keycode == 30) {
            m_enabled = !m_enabled;
            printf("[BlurPlugin] Blur %s\n", m_enabled ? "enabled" : "disabled");
            return true;
        }
        
        return false;
    }
    
private:
    CompositorAPI* m_api = nullptr;
    bool m_enabled;
};

// Plugin factory
Plugin* create_blur_plugin() {
    return new BlurPlugin();
}

} // namespace havel
