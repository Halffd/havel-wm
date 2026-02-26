// Wallpaper Plugin - demonstrates output frame hook capability
// Shows how to render custom content behind windows

#include <wm/plugins/Plugin.hpp>
#include <wm/plugins/CompositorAPI.hpp>
#include <cstdio>
#include <string>

namespace havel {

/**
 * Wallpaper Plugin
 * 
 * Demonstrates output frame hook for rendering wallpapers.
 * Actual implementation requires:
 * - Image loading (PNG, JPG, etc.)
 * - Texture upload to GPU
 * - Render pass integration to draw behind windows
 * 
 * This stub shows the plugin structure.
 */
class WallpaperPlugin : public Plugin {
public:
    const char* name() const override { return "wallpaper"; }
    const char* version() const override { return "0.1.0"; }
    
    void init(CompositorAPI* api) override {
        m_api = api;
        m_colorR = 0.1f;
        m_colorG = 0.1f;
        m_colorB = 0.15f;
        // Set initial background color
        m_api->setBackgroundColor(m_colorR, m_colorG, m_colorB);
        printf("[WallpaperPlugin] Initialized (solid color wallpaper)\n");
    }
    
    void fini() override {
        printf("[WallpaperPlugin] Finalized\n");
        m_api = nullptr;
    }
    
    void loadConfig(const std::string& configPath) override {
        // Would load wallpaper image path or color settings
        (void)configPath;
        printf("[WallpaperPlugin] Config loaded from: %s\n", configPath.c_str());
    }
    
    void onOutputFrame(const OutputFrameEvent& event) override {
        // Called every frame for each output
        // Actual wallpaper would render here
        
        // For now, just occasional logging
        if (m_frameCount++ % 3600 == 0) {  // Log every ~60 seconds
            printf("[WallpaperPlugin] Output %dx%d @ %.1fHz\n",
                   event.width, event.height, event.refresh / 1000.0f);
        }
    }
    
    bool onKey(const KeyEvent& event) override {
        constexpr uint32_t MOD_LOGO = 1 << 6;
        
        // Meta+W cycles wallpaper colors
        if (event.pressed && (event.modifiers & MOD_LOGO) && event.keycode == 32) {
            cycleColor();
            return true;
        }
        
        return false;
    }
    
private:
    CompositorAPI* m_api = nullptr;
    float m_colorR, m_colorG, m_colorB;
    uint64_t m_frameCount = 0;
    
    void cycleColor() {
        // Simple color cycle for demo
        static int cycle = 0;
        cycle = (cycle + 1) % 4;

        switch (cycle) {
            case 0:  // Dark blue
                m_colorR = 0.1f; m_colorG = 0.1f; m_colorB = 0.15f;
                break;
            case 1:  // Dark green
                m_colorR = 0.1f; m_colorG = 0.15f; m_colorB = 0.1f;
                break;
            case 2:  // Dark purple
                m_colorR = 0.15f; m_colorG = 0.1f; m_colorB = 0.15f;
                break;
            case 3:  // Dark gray
                m_colorR = 0.12f; m_colorG = 0.12f; m_colorB = 0.12f;
                break;
        }

        // Apply the new background color
        m_api->setBackgroundColor(m_colorR, m_colorG, m_colorB);
        
        printf("[WallpaperPlugin] Wallpaper color: (%.2f, %.2f, %.2f)\n",
               m_colorR, m_colorG, m_colorB);
    }
};

// Plugin factory
Plugin* create_wallpaper_plugin() {
    return new WallpaperPlugin();
}

} // namespace havel
