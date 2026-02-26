// Scale Plugin - Grid Overview with Navigation
// Demonstrates view transformers and grid navigation

#include <wm/plugins/Plugin.hpp>
#include <wm/plugins/CompositorAPI.hpp>
#include <cstdio>
#include <vector>

namespace havel {

/**
 * Scale Plugin with Grid Navigation
 * 
 * Shows overview of all windows in current workspace.
 * Navigate with arrow keys, select with Enter.
 * 
 * Features:
 * - Grid layout of windows
 * - Arrow key navigation
 * - Visual highlighting of selection
 * - Enter to select, Escape to cancel
 */
class ScalePlugin : public Plugin {
public:
    const char* name() const override { return "scale"; }
    const char* version() const override { return "0.2.0"; }
    
    void init(CompositorAPI* api) override {
        m_api = api;
        m_active = false;
        m_selectedIndex = 0;
        m_windowCount = 0;
        printf("[ScalePlugin] Initialized with grid navigation\n");
    }
    
    void fini() override {
        if (m_active) {
            endScale();
        }
        printf("[ScalePlugin] Finalized\n");
        m_api = nullptr;
    }
    
    void loadConfig(const std::string& configPath) override {
        (void)configPath;
        printf("[ScalePlugin] Config loaded\n");
    }
    
    void onViewMap(const ViewEvent& event) override {
        // Refresh window count when windows change
        if (m_active) {
            printf("[ScalePlugin] Window mapped: %s\n", 
                   event.appId ? event.appId : "unknown");
        }
    }
    
    void onViewDestroy(const ViewEvent& event) override {
        // Refresh window count when windows close
        if (m_active) {
            printf("[ScalePlugin] Window destroyed: %s\n", 
                   event.title ? event.title : "unknown");
        }
    }
    
    bool onKey(const KeyEvent& event) override {
        if (!m_active) {
            // Meta+S to toggle scale overview
            constexpr uint32_t MOD_LOGO = 1 << 6;
            if (event.pressed && (event.modifiers & MOD_LOGO) && event.keycode == 31) {
                beginScale();
                return true;
            }
            return false;
        }
        
        // Navigation while scale is active
        if (!event.pressed) return false;
        
        switch (event.keycode) {
            case 111:  // Escape - cancel
                endScale();
                return true;
                
            case 28:   // Enter - select
                selectFocused();
                return true;
                
            case 103:  // Up arrow
                moveSelection(0, -1);
                return true;
                
            case 108:  // Down arrow
                moveSelection(0, 1);
                return true;
                
            case 105:  // Left arrow
                moveSelection(-1, 0);
                return true;
                
            case 106:  // Right arrow
                moveSelection(1, 0);
                return true;
                
            case 35:   // H - left
                moveSelection(-1, 0);
                return true;
                
            case 38:   // L - right
                moveSelection(1, 0);
                return true;
                
            case 30:   // J - down
                moveSelection(0, 1);
                return true;
                
            case 31:   // K - up
                moveSelection(0, -1);
                return true;
        }
        
        return false;
    }
    
private:
    CompositorAPI* m_api = nullptr;
    bool m_active;
    int m_selectedIndex;
    int m_windowCount;
    
    // Grid layout info
    int m_gridCols;
    int m_gridRows;
    
    void beginScale() {
        m_active = true;
        m_selectedIndex = 0;
        
        // Get window count from current workspace
        // For now, estimate - would get actual count from API
        m_windowCount = 5;  // Stub
        m_gridCols = 3;
        m_gridRows = (m_windowCount + m_gridCols - 1) / m_gridCols;
        
        printf("[ScalePlugin] Scale overview began (%d windows, %dx%d grid)\n",
               m_windowCount, m_gridCols, m_gridRows);
        
        // Actual implementation would:
        // 1. Get all views from current workspace
        // 2. Calculate grid positions
        // 3. Apply scale transform to each view
        // 4. Highlight first window
        highlightSelected();
    }
    
    void endScale() {
        m_active = false;
        m_selectedIndex = 0;
        printf("[ScalePlugin] Scale overview ended\n");
        
        // Actual implementation would:
        // 1. Remove scale transforms from all views
        // 2. Animate back to original positions
        // 3. Clear highlights
    }
    
    void selectFocused() {
        printf("[ScalePlugin] Selected window %d/%d\n", 
               m_selectedIndex + 1, m_windowCount);
        endScale();
        
        // Actual implementation would:
        // 1. Get the view at m_selectedIndex
        // 2. Focus and raise that view
    }
    
    void moveSelection(int dx, int dy) {
        int oldIndex = m_selectedIndex;
        
        // Calculate current row and column
        int col = m_selectedIndex % m_gridCols;
        int row = m_selectedIndex / m_gridCols;
        
        // Apply movement
        col += dx;
        row += dy;
        
        // Clamp to valid range
        if (col < 0) col = 0;
        if (col >= m_gridCols) col = m_gridCols - 1;
        if (row < 0) row = 0;
        if (row >= m_gridRows) row = m_gridRows - 1;
        
        // Calculate new index
        m_selectedIndex = row * m_gridCols + col;
        
        // Clamp to window count
        if (m_selectedIndex >= m_windowCount) {
            m_selectedIndex = m_windowCount - 1;
        }
        
        if (m_selectedIndex != oldIndex) {
            printf("[ScalePlugin] Selection: %d -> %d (row=%d, col=%d)\n",
                   oldIndex, m_selectedIndex, row, col);
            highlightSelected();
        }
    }
    
    void highlightSelected() {
        // Actual implementation would:
        // 1. Clear highlight from previously selected window
        // 2. Add highlight border/glow to newly selected window
        // 3. Ensure selected window is visible
        printf("[ScalePlugin] Highlighting window %d\n", m_selectedIndex + 1);
    }
};

// Plugin factory
Plugin* create_scale_plugin() {
    return new ScalePlugin();
}

} // namespace havel
