// Alt-Tab Plugin - Phase 4.1
// Window switcher with thumbnails

#include <wm/plugins/Plugin.hpp>
#include <wm/plugins/CompositorAPI.hpp>
#include <wm/render/OverlayRenderer.hpp>
#include <GLES2/gl2.h>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <vector>
#include <string>

namespace havel {

/**
 * Window entry for Alt-Tab
 */
struct WindowEntry {
    void* viewPtr = nullptr;  // Opaque pointer - actual View* used internally
    uint64_t viewId = 0;      // Opaque window ID (for focusViewById)
    std::string appId;
    std::string title;
    uint32_t workspace = 0;
    bool isFocused = false;
    int x = 0, y = 0, w = 0, h = 0;
    uint32_t textureId = 0;   // OpenGL texture ID for thumbnail
    int textureWidth = 0;
    int textureHeight = 0;
    uint32_t iconTextureId = 0;  // OpenGL texture ID for app icon
    int iconSize = 32;  // Icon display size
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
        // Clean up icon textures
        for (auto& entry : m_windows) {
            if (entry.iconTextureId != 0) {
                glDeleteTextures(1, &entry.iconTextureId);
            }
        }
        
        m_visible = false;
        m_windows.clear();
        m_selectedIndex = 0;
        printf("[AltTab] Hidden\n");
    }

    // Load or generate app icon (simple colored square based on appId hash)
    uint32_t loadAppIcon(const std::string& appId) {
        if (appId.empty()) return 0;

        // Generate a simple colored icon based on appId hash
        // In a real implementation, this would load from icon theme
        GLuint iconTexture;
        glGenTextures(1, &iconTexture);
        glBindTexture(GL_TEXTURE_2D, iconTexture);

        // Generate color from appId hash
        unsigned int hash = 0;
        for (char c : appId) {
            hash = hash * 31 + c;
        }
        float r = ((hash >> 16) & 0xFF) / 255.0f;
        float g = ((hash >> 8) & 0xFF) / 255.0f;
        float b = (hash & 0xFF) / 255.0f;

        // Create 32x32 solid color texture
        unsigned char pixels[32 * 32 * 4];
        for (int i = 0; i < 32 * 32; i++) {
            pixels[i * 4 + 0] = (unsigned char)(r * 255);
            pixels[i * 4 + 1] = (unsigned char)(g * 255);
            pixels[i * 4 + 2] = (unsigned char)(b * 255);
            pixels[i * 4 + 3] = 255;  // Alpha
        }

        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 32, 32, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glBindTexture(GL_TEXTURE_2D, 0);

        return iconTexture;
    }

    void collectWindows() {
        m_windows.clear();

        // Get all views from compositor
        auto allViews = m_api->getAllViews();
        uint32_t currentWorkspace = m_api->getActiveWorkspace();
        View* focused = m_api->getFocusedView();

        // Collect views - use opaque IDs, don't dereference View pointers
        for (View* view : allViews) {
            if (!view) continue;

            WindowEntry entry;
            entry.viewPtr = view;  // Keep for internal use
            entry.viewId = 0;  // Would get from API in real impl
            entry.workspace = currentWorkspace;  // Assume current workspace
            entry.isFocused = (view == focused);

            // Get app ID and title via CompositorAPI
            entry.appId = m_api->getViewAppId(view);
            entry.title = m_api->getViewTitle(view);

            // Get texture for thumbnail rendering
            entry.textureId = m_api->getViewTextureId(view);
            entry.textureWidth = m_api->getViewTextureWidth(view);
            entry.textureHeight = m_api->getViewTextureHeight(view);

            // Load app icon (generated colored square)
            entry.iconTextureId = loadAppIcon(entry.appId);
            entry.iconSize = 32;

            // Get geometry
            entry.x = 0; entry.y = 0; entry.w = 800; entry.h = 600;

            m_windows.push_back(entry);
        }

        // If no windows, add placeholder
        if (m_windows.empty()) {
            WindowEntry placeholder;
            placeholder.viewPtr = nullptr;
            placeholder.viewId = 0;
            placeholder.appId = "none";
            placeholder.title = "No windows";
            placeholder.workspace = currentWorkspace;
            placeholder.isFocused = false;
            placeholder.x = 100; placeholder.y = 100;
            placeholder.w = 400; placeholder.h = 200;
            placeholder.textureId = 0;
            placeholder.textureWidth = 0;
            placeholder.textureHeight = 0;
            m_windows.push_back(placeholder);
        }

        // Sort: focused first, then by workspace, then by title
        std::sort(m_windows.begin(), m_windows.end(),
            [focused](const WindowEntry& a, const WindowEntry& b) {
                if (a.isFocused != b.isFocused) return a.isFocused;
                if (a.workspace != b.workspace) return a.workspace < b.workspace;
                return a.title < b.title;
            });

        // Find focused window index
        for (size_t i = 0; i < m_windows.size(); ++i) {
            if (m_windows[i].isFocused) {
                m_selectedIndex = (int)i;
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
        printf("[AltTab] Selecting: %s (id=%lu)\n", 
               selected.title.c_str(), (unsigned long)selected.viewId);

        // Focus the selected window using opaque ID (no raw pointer!)
        if (selected.viewId != 0) {
            m_api->focusViewById(selected.viewId);
        } else if (selected.viewPtr) {
            // Fallback for placeholder entries
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
            const WindowEntry& entry = m_windows[i];

            // Draw thumbnail background (fallback if no texture)
            Color bgColor = isSelected ? Color(0.3f, 0.4f, 0.5f, 0.9f) : Color(0.2f, 0.2f, 0.2f, 0.8f);
            renderer->drawRect((float)x, (float)y, (float)thumbnailWidth, (float)thumbnailHeight, bgColor);

            // Draw window texture if available
            if (entry.textureId != 0) {
                renderer->drawTexture(entry.textureId,
                                      (float)x, (float)y,
                                      (float)thumbnailWidth, (float)thumbnailHeight,
                                      1.0f);
            }

            // Draw border
            Color borderColor = isSelected ? Color(1.0f, 1.0f, 1.0f, 1.0f) : Color(0.5f, 0.5f, 0.5f, 0.5f);
            renderer->drawBorder(FloatRect((float)x, (float)y, (float)thumbnailWidth, (float)thumbnailHeight), borderColor, isSelected ? 3.0f : 2.0f);

            // Draw app icon at bottom-right corner (small, 32x32)
            if (entry.iconTextureId != 0) {
                int iconX = x + thumbnailWidth - entry.iconSize - 8;  // 8px padding from right
                int iconY = y + thumbnailHeight - entry.iconSize - 8;  // 8px padding from bottom
                renderer->drawTexture(entry.iconTextureId,
                                      (float)iconX, (float)iconY,
                                      (float)entry.iconSize, (float)entry.iconSize,
                                      1.0f);
            }

            // Draw app name
            const std::string& name = entry.appId;
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
