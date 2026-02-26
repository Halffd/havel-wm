// Custom Layouts Plugin - demonstrates workspace API capability
// Shows how to implement custom tiling layouts

#include <wm/plugins/Plugin.hpp>
#include <wm/plugins/CompositorAPI.hpp>
#include <cstdio>
#include <vector>

namespace havel {

/**
 * Custom Layouts Plugin
 * 
 * Demonstrates workspace and view manipulation for custom layouts.
 * Actual implementation requires:
 * - Layout algorithm (bsp, grid, deck, etc.)
 * - View positioning logic
 * - Layout switching mechanism
 * 
 * This stub shows the plugin structure.
 */
class CustomLayoutsPlugin : public Plugin {
public:
    const char* name() const override { return "custom_layouts"; }
    const char* version() const override { return "0.1.0"; }
    
    void init(CompositorAPI* api) override {
        m_api = api;
        m_currentLayout = Layout::TILING;
        printf("[CustomLayoutsPlugin] Initialized\n");
    }
    
    void fini() override {
        printf("[CustomLayoutsPlugin] Finalized\n");
        m_api = nullptr;
    }
    
    void loadConfig(const std::string& configPath) override {
        // Would load layout preferences, gaps, etc.
        (void)configPath;
        printf("[CustomLayoutsPlugin] Config loaded\n");
    }
    
    bool onKey(const KeyEvent& event) override {
        constexpr uint32_t MOD_LOGO = 1 << 6;
        
        if (!event.pressed) return false;
        
        if (!(event.modifiers & MOD_LOGO)) return false;
        
        switch (event.keycode) {
            case 36:  // Meta+H - Horizontal split
                setLayout(Layout::HORIZONTAL);
                return true;
                
            case 37:  // Meta+V - Vertical split
                setLayout(Layout::VERTICAL);
                return true;
                
            case 43:  // Meta+G - Grid layout
                setLayout(Layout::GRID);
                return true;
                
            case 20:  // Meta+T - Tiling (default)
                setLayout(Layout::TILING);
                return true;
        }
        
        return false;
    }
    
private:
    CompositorAPI* m_api = nullptr;
    
    enum class Layout {
        TILING,      // Master-stack (default)
        HORIZONTAL,  // All windows horizontal
        VERTICAL,    // All windows vertical
        GRID         // Grid arrangement
    };
    
    Layout m_currentLayout;
    
    void setLayout(Layout layout) {
        if (m_currentLayout == layout) {
            return;
        }
        
        m_currentLayout = layout;
        
        const char* names[] = {"tiling", "horizontal", "vertical", "grid"};
        printf("[CustomLayoutsPlugin] Layout: %s\n", names[static_cast<int>(layout)]);
        
        // Actual implementation would:
        // 1. Get all views in current workspace
        // 2. Calculate positions based on layout algorithm
        // 3. Apply positions to views
        // 4. Animate transitions
        
        uint32_t ws = m_api->getActiveWorkspace();
        printf("[CustomLayoutsPlugin] Applying to workspace %u\n", ws);
    }
};

// Plugin factory
Plugin* create_custom_layouts_plugin() {
    return new CustomLayoutsPlugin();
}

} // namespace havel
