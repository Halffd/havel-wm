// Alt-Tab Plugin - Phase 4.1
// Window switcher with thumbnails

#include <wm/plugins/Plugin.hpp>
#include <wm/plugins/CompositorAPI.hpp>
#include <wm/render/OverlayRenderer.hpp>
#include <cstdio>
#include <cstring>
#include <vector>
#include <string>

namespace havel {

/**
 * Window entry for Alt-Tab
 */
struct WindowEntry {
    void* viewPtr = nullptr;  // Opaque pointer - actual View* used internally
    std::string appId;
    std::string title;
    uint32_t workspace = 0;
    bool isFocused = false;
    int x = 0, y = 0, w = 0, h = 0;
};

/**
 * Alt-Tab Plugin
 * 
 * Provides window switching via:
 * - Alt+Tab: Cycle forward
 * - Alt+Shift+Tab: Cycle backward
 * - Visual thumbnail overlay (stubbed)
 * 
 * Keybindings:
 * - Alt+Tab: Next window
 * - Alt+Shift+Tab: Previous window
 * - Alt+Escape: Cancel
 * - Enter: Select window
 */
class AltTabPlugin : public Plugin {
public:
    const char* name() const override { return "alt_tab"; }
    const char* version() const override { return "0.1.0"; }
    
    void init(CompositorAPI* api) override {
        m_api = api;
        m_visible = false;
        m_selectedIndex = 0;
        m_reverse = false;
        
        printf("[AltTab] Initialized\n");
    }
    
    void fini() override {
        if (m_visible) {
            hide();
        }
        printf("[AltTab] Finalized\n");
        m_api = nullptr;
    }
    
    void loadConfig(const std::string& configPath) override {
        (void)configPath;
        printf("[AltTab] Config loaded\n");
    }
    
    bool onKey(const KeyEvent& event) override {
        constexpr uint32_t MOD_ALT = 1 << 3;
        constexpr uint32_t MOD_SHIFT = 1 << 0;
        
        bool alt = (event.modifiers & MOD_ALT) != 0;
        bool shift = (event.modifiers & MOD_SHIFT) != 0;
        
        // Alt+Tab or Alt+Shift+Tab
        if (alt && event.keycode == 23 && event.pressed) {  // Tab
            if (!m_visible) {
                // Show Alt-Tab
                m_reverse = shift;
                show();
            } else {
                // Cycle windows
                if (shift) {
                    navigate(-1);
                } else {
                    navigate(1);
                }
            }
            return true;
        }
        
        if (!m_visible) return false;
        if (!event.pressed) return false;
        
        switch (event.keycode) {
            case 111:  // Escape - cancel
                hide();
                return true;
                
            case 28:   // Enter - select
                select();
                return true;
        }
        
        return false;
    }
    
    void onViewMap(const ViewEvent& event) override {
        // Refresh window list when windows change
        if (m_visible) {
            printf("[AltTab] Window mapped: %s\n", 
                   event.appId ? event.appId : "unknown");
        }
    }
    
    void onViewDestroy(const ViewEvent& event) override {
        // Refresh window list when windows close
        if (m_visible) {
            printf("[AltTab] Window destroyed: %s\n",
                   event.title ? event.title : "unknown");
        }
    }
    
private:
    CompositorAPI* m_api = nullptr;
    bool m_visible;
    int m_selectedIndex;
    bool m_reverse;
    std::vector<WindowEntry> m_windows;
    
    void show() {
        m_visible = true;
        m_selectedIndex = 0;
        m_reverse = false;
        
        // Collect all windows
        collectWindows();
        
        if (m_windows.empty()) {
            hide();
            return;
        }
        
        printf("[AltTab] Showing (%zu windows)\n", m_windows.size());
    }
    
    void hide() {
        m_visible = false;
        m_windows.clear();
        m_selectedIndex = 0;
        printf("[AltTab] Hidden\n");
    }
    
    void collectWindows() {
        m_windows.clear();
        
        // Would get actual windows from compositor
        // For now, stub entries showing the pattern
        
        // Example: Get focused view
        View* focused = m_api->getFocusedView();
        
        // Stub entries for demonstration
        WindowEntry term;
        term.viewPtr = focused;
        term.appId = "foot";
        term.title = "Terminal";
        term.workspace = m_api->getActiveWorkspace();
        term.isFocused = (focused != nullptr);
        term.x = 100; term.y = 100; term.w = 800; term.h = 600;
        m_windows.push_back(term);
        
        WindowEntry browser;
        browser.viewPtr = nullptr;  // Would get actual view
        browser.appId = "firefox";
        browser.title = "Firefox";
        browser.workspace = m_api->getActiveWorkspace();
        browser.isFocused = false;
        browser.x = 200; term.y = 200; browser.w = 1200; browser.h = 800;
        m_windows.push_back(browser);
        
        // Find and mark focused window
        for (auto& win : m_windows) {
            win.isFocused = (win.viewPtr == focused);
            if (win.isFocused) {
                m_selectedIndex = 0;
                // Move focused to front of list
                break;
            }
        }
    }
    
    void navigate(int delta) {
        if (m_windows.empty()) return;
        
        if (m_reverse) {
            delta = -delta;
        }
        
        m_selectedIndex += delta;
        
        // Wrap around
        if (m_selectedIndex < 0) {
            m_selectedIndex = (int)m_windows.size() - 1;
        } else if (m_selectedIndex >= (int)m_windows.size()) {
            m_selectedIndex = 0;
        }
        
        printf("[AltTab] Selected: %s (%d/%zu)\n",
               m_windows[m_selectedIndex].title.c_str(),
               m_selectedIndex + 1, m_windows.size());
    }
    
    void select() {
        if (m_windows.empty() || m_selectedIndex < 0 ||
            m_selectedIndex >= (int)m_windows.size()) {
            hide();
            return;
        }
        
        WindowEntry& selected = m_windows[m_selectedIndex];
        printf("[AltTab] Selecting: %s\n", selected.title.c_str());
        
        // Focus the selected window
        if (selected.viewPtr) {
            m_api->focusView((View*)selected.viewPtr);
        }

        // Switch to its workspace if needed
        if (selected.workspace != m_api->getActiveWorkspace()) {
            m_api->setActiveWorkspace(selected.workspace);
        }

        hide();
    }
    
    void renderOverlay(void* rendererPtr) override {
        if (!m_visible || !rendererPtr) return;
        
        OverlayRenderer* renderer = static_cast<OverlayRenderer*>(rendererPtr);
        
        int screenWidth = renderer->getScreenWidth();
        int screenHeight = renderer->getScreenHeight();
        
        // Draw semi-transparent background
        renderer->drawRect(0, 0, screenWidth, screenHeight, Color(0.0f, 0.0f, 0.0f, 0.7f));
        
        if (m_windows.empty()) return;
        
        // Calculate thumbnail size and positions
        int thumbnailWidth = 200;
        int thumbnailHeight = 150;
        int spacing = 30;
        int totalWidth = (int)m_windows.size() * (thumbnailWidth + spacing) - spacing;
        int startX = (screenWidth - totalWidth) / 2;
        int y = (screenHeight - thumbnailHeight) / 2;
        
        // Draw each window thumbnail
        for (size_t i = 0; i < m_windows.size(); i++) {
            int x = startX + (int)i * (thumbnailWidth + spacing);
            bool isSelected = ((int)i == m_selectedIndex);
            
            // Draw thumbnail background
            Color bgColor = isSelected ? Color(0.3f, 0.4f, 0.5f, 0.9f) : Color(0.2f, 0.2f, 0.2f, 0.8f);
            renderer->drawRect((float)x, (float)y, (float)thumbnailWidth, (float)thumbnailHeight, bgColor);
            
            // Draw border
            Color borderColor = isSelected ? Color(1.0f, 1.0f, 1.0f, 1.0f) : Color(0.5f, 0.5f, 0.5f, 0.5f);
            renderer->drawBorder(FloatRect((float)x, (float)y, (float)thumbnailWidth, (float)thumbnailHeight), borderColor, isSelected ? 3.0f : 2.0f);
            
            // Draw app name
            const std::string& name = m_windows[i].appId;
            renderer->drawText(name.c_str(), (float)(x + 10), (float)(y + thumbnailHeight - 25), 16.0f, Color(1.0f, 1.0f, 1.0f, 1.0f));
        }
        
        // Draw instruction text
        const char* instruction = "Alt+Tab: Cycle | Enter: Select | Esc: Cancel";
        float textWidth = strlen(instruction) * 10.0f;
        float textX = (screenWidth - textWidth) / 2.0f;
        renderer->drawText(instruction, textX, (float)(screenHeight - 40), 14.0f, Color(0.8f, 0.8f, 0.8f, 1.0f));
    }
};

// Plugin factory
Plugin* create_alt_tab_plugin() {
    return new AltTabPlugin();
}

} // namespace havel
