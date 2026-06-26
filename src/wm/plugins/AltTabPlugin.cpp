// Alt-Tab Plugin - Phase 4.1
// Window switcher with thumbnails

#include <wm/plugins/Plugin.hpp>
#include <wm/plugins/CompositorAPI.hpp>
#include <wm/plugins/PluginConfig.hpp>
#include <wm/render/OverlayRenderer.hpp>
#include <wm/render/AppIconLoader.hpp>
#include <wm/View.hpp>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <vector>
#include <string>

namespace havel {

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
 * - Visual thumbnail overlay with window previews
 *
 * Keybindings:
 * - Alt+Tab: Next window
 * - Alt+Shift+Tab: Previous window
 * - Alt+Escape: Cancel
 * - Enter: Select window
 * - Alt released: Select current window
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
    auto& config = PluginConfig::getInstance();

    m_thumbnailWidth = config.getIntValue("alt_tab", "thumbnailWidth", 500);
        m_thumbnailHeight = config.getIntValue("alt_tab", "thumbnailHeight", 375);
        m_maxVisibleWindows = config.getIntValue("alt_tab", "maxVisibleWindows", 5);
        
    // Colors: "R,G,B,A"
    std::string bgColor = config.getValue("alt_tab", "backgroundColor", "0.0,0.0,0.0,0.7");
        sscanf(bgColor.c_str(), "%f,%f,%f,%f", 
               &m_bgColor[0], &m_bgColor[1], &m_bgColor[2], &m_bgColor[3]);
        
        std::string borderColor = config.getValue("alt_tab", "borderColor", "1.0,1.0,1.0,1.0");
        sscanf(borderColor.c_str(), "%f,%f,%f,%f", 
               &m_borderColor[0], &m_borderColor[1], &m_borderColor[2], &m_borderColor[3]);
        
        std::string textColor = config.getValue("alt_tab", "textColor", "1.0,1.0,1.0,1.0");
        sscanf(textColor.c_str(), "%f,%f,%f,%f", 
               &m_textColor[0], &m_textColor[1], &m_textColor[2], &m_textColor[3]);
        
        printf("[AltTab] Config loaded (thumbnail=%dx%d, max=%d)\n",
               m_thumbnailWidth, m_thumbnailHeight, m_maxVisibleWindows);
    }
    
    bool onKey(const KeyEvent& event) override {
        constexpr uint32_t MOD_ALT = 1 << 3;
        constexpr uint32_t MOD_SHIFT = 1 << 0;
        
        bool alt = (event.modifiers & MOD_ALT) != 0;
        bool shift = (event.modifiers & MOD_SHIFT) != 0;
        
    // Alt released
    if (!event.pressed && !alt && m_visible) {
      select();
      return true;
        }
        
        // Alt+Tab or Alt+Shift+Tab
    if (alt && event.keycode == 23 && event.pressed) {
      if (!m_visible) {
        m_reverse = shift;
        show();
      } else {
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
    case 111: // Escape
      hide();
      return true;

    case 28: // Enter
      select();
                return true;
        }
        
        return false;
    }
    
  void onViewMap(const ViewEvent& event) override {
    if (m_visible) {
            printf("[AltTab] Window mapped: %s\n", 
                   event.appId ? event.appId : "unknown");
        }
    }
    
  void onViewDestroy(const ViewEvent& event) override {
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
    
  // Customizable settings
  int m_thumbnailWidth;
  int m_thumbnailHeight;
  float m_bgColor[4];
  float m_borderColor[4];
  float m_textColor[4];
    
    void show() {
        m_visible = true;
        m_selectedIndex = 0;
        m_reverse = false;
        
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

    auto allViews = m_api->getAllViews();
        uint32_t currentWorkspace = m_api->getActiveWorkspace();
        View* focused = m_api->getFocusedView();

        printf("[AltTab] Collecting windows (workspace=%u, total views=%zu)\n", 
               currentWorkspace, allViews.size());

    for (View* view : allViews) {
      if (!view) continue;

      if (!view->isMapped()) {
                printf("[AltTab] Skipping unmapped view %p\n", (void*)view);
                continue;
            }
            
      if (view->workspaceId() != currentWorkspace) {
                printf("[AltTab] Skipping view %p on workspace %u (current=%u)\n", 
                       (void*)view, view->workspaceId(), currentWorkspace);
                continue;
            }

            WindowEntry entry;
            entry.viewPtr = view;
            entry.viewId = view->windowId();
            entry.workspace = view->workspaceId();
            entry.isFocused = (view == focused);

      entry.appId = m_api->getViewAppId(view);
      entry.title = m_api->getViewTitle(view);

      entry.textureId = m_api->getViewTextureId(view);
            entry.textureWidth = m_api->getViewTextureWidth(view);
            entry.textureHeight = m_api->getViewTextureHeight(view);

      entry.iconTextureId = havel::AppIconLoader::getInstance()->loadIcon(entry.appId);
            entry.iconSize = havel::AppIconLoader::getInstance()->getIconSize();

      entry.x = m_api->getViewX(view);
            entry.y = m_api->getViewY(view);
            entry.w = m_api->getViewWidth(view);
            entry.h = m_api->getViewHeight(view);

            printf("[AltTab] Added: %s - %s (mapped=%d, floating=%d)\n",
                   entry.appId.c_str(), entry.title.c_str(),
                   view->isMapped(), view->isFloating());

            m_windows.push_back(entry);
        }

    if (m_windows.empty()) {
            printf("[AltTab] No windows on workspace %u, hiding\n", currentWorkspace);
            hide();
            return;
        }

        printf("[AltTab] Collected %zu windows on workspace %u\n", 
               m_windows.size(), currentWorkspace);

    // Focused first, then by title
    std::sort(m_windows.begin(), m_windows.end(),
            [focused](const WindowEntry& a, const WindowEntry& b) {
                if (a.isFocused != b.isFocused) return a.isFocused;
                return a.title < b.title;
            });

    m_selectedIndex = 0;
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

    if (selected.viewPtr) {
            m_api->focusView((View*)selected.viewPtr);
        }

        hide();
    }
    
    void renderOverlay(void* rendererPtr) override {
        if (!m_visible || !rendererPtr) return;

        OverlayRenderer* renderer = static_cast<OverlayRenderer*>(rendererPtr);

        int screenWidth = renderer->getScreenWidth();
        int screenHeight = renderer->getScreenHeight();

    renderer->drawRect(0, 0, screenWidth, screenHeight, Color(m_bgColor[0], m_bgColor[1], m_bgColor[2], m_bgColor[3]));
        
    // Vignette
    for (int i = 0; i < 3; i++) {
            float vignetteAlpha = 0.08f * (i + 1);
            int inset = i * 150;
            renderer->drawRect(inset, inset, screenWidth - inset*2, screenHeight - inset*2, 
                              Color(0.0f, 0.0f, 0.0f, vignetteAlpha));
        }

        if (m_windows.empty()) return;

    if (m_selectedIndex < 0 || m_selectedIndex >= (int)m_windows.size()) {
            m_selectedIndex = 0;
        }

    int thumbnailWidth = 500;
    int thumbnailHeight = 375;
    int spacing = 40;
    int maxVisibleWindows = 5;
    int visibleCount = std::min((int)m_windows.size(), maxVisibleWindows);
    int totalWidth = visibleCount * (thumbnailWidth + spacing) - spacing;
    int startX = (screenWidth - totalWidth) / 2;
    int y = (screenHeight - thumbnailHeight) / 2 - 50;

    int scrollOffset = 0;
    if ((int)m_windows.size() > maxVisibleWindows) {
      int firstVisible = std::max(0, m_selectedIndex - maxVisibleWindows / 2);
            firstVisible = std::min(firstVisible, (int)m_windows.size() - maxVisibleWindows);
            scrollOffset = firstVisible * (thumbnailWidth + spacing);
        }

    for (int i = 0; i < visibleCount; i++) {
            int windowIndex = i + (scrollOffset / (thumbnailWidth + spacing));
            if (windowIndex >= (int)m_windows.size()) break;
            
            int x = startX + i * (thumbnailWidth + spacing);
            bool isSelected = (windowIndex == m_selectedIndex);
            const WindowEntry& entry = m_windows[windowIndex];

      // Drop shadow
      float shadowAlpha = isSelected ? 0.4f : 0.3f;
            for (int s = 3; s >= 1; s--) {
                renderer->drawRect((float)(x + s*2), (float)(y + s*2 + 10), 
                                  (float)thumbnailWidth, (float)thumbnailHeight,
                                  Color(0.0f, 0.0f, 0.0f, shadowAlpha * (float)s / 3.0f));
            }

      // Fallback background
      Color bgColor = isSelected ? Color(0.3f, 0.4f, 0.5f, 0.95f) : Color(0.2f, 0.2f, 0.2f, 0.92f);
            renderer->drawRect((float)x, (float)y, (float)thumbnailWidth, (float)thumbnailHeight, bgColor);

      if (entry.textureId != 0) {
                renderer->drawTexture(entry.textureId,
                                      (float)x, (float)y,
                                      (float)thumbnailWidth, (float)thumbnailHeight,
                                      1.0f);
            }

      if (isSelected) {
        for (int g = 2; g >= 1; g--) {
                    float glowAlpha = 0.4f * (float)g / 2.0f;
                    int glowOffset = g * 2;
                    renderer->drawRect((float)(x - glowOffset), (float)(y - glowOffset),
                                      (float)(thumbnailWidth + glowOffset*2), (float)(thumbnailHeight + glowOffset*2),
                                      Color(m_borderColor[0], m_borderColor[1], m_borderColor[2], glowAlpha));
                }
            }

      Color borderColor = isSelected ? Color(m_borderColor[0], m_borderColor[1], m_borderColor[2], m_borderColor[3])
                                           : Color(m_borderColor[0]*0.5f, m_borderColor[1]*0.5f, m_borderColor[2]*0.5f, m_borderColor[3]*0.5f);
            renderer->drawBorder(FloatRect((float)x, (float)y, (float)thumbnailWidth, (float)thumbnailHeight), borderColor, isSelected ? 4.0f : 2.0f);

      if (entry.iconTextureId != 0) {
                int iconX = x + thumbnailWidth - entry.iconSize - 8;
                int iconY = y + thumbnailHeight - entry.iconSize - 8;
                renderer->drawTexture(entry.iconTextureId,
                                      (float)iconX, (float)iconY,
                                      (float)entry.iconSize, (float)entry.iconSize,
                                      1.0f);
            }

      const std::string& name = entry.appId;
            renderer->drawText(name.c_str(), (float)(x + 15), (float)(y + thumbnailHeight - 35), 20.0f, Color(m_textColor[0], m_textColor[1], m_textColor[2], m_textColor[3]));

      if (!entry.title.empty() && entry.title != name) {
                renderer->drawText(entry.title.c_str(), (float)(x + 15), (float)(y + thumbnailHeight - 15), 14.0f, Color(m_textColor[0]*0.8f, m_textColor[1]*0.8f, m_textColor[2]*0.8f, m_textColor[3]));
            }
        }

    // Instruction bar
    const char* instruction = "Alt+Tab: Cycle | Enter: Select | Esc: Cancel";
        float textWidth = strlen(instruction) * 10.0f + 40.0f;
        float textX = (screenWidth - textWidth) / 2.0f;
        float textY = (float)(screenHeight - 55);
        
    renderer->drawRect(textX - 20.0f, textY - 5.0f, textWidth, 28.0f, Color(0.0f, 0.0f, 0.0f, 0.75f));
        renderer->drawBorder(FloatRect(textX - 20.0f, textY - 5.0f, textWidth, 28.0f), Color(0.45f, 0.45f, 0.45f, 0.55f), 1.0f);
        
    renderer->drawText(instruction, textX + 20.0f, textY + 15.0f, 14.0f, Color(0.92f, 0.92f, 0.92f, 1.0f));
        
    // Window counter
    char countText[32];
        snprintf(countText, sizeof(countText), "%d / %zu", m_selectedIndex + 1, m_windows.size());
        float countWidth = strlen(countText) * 10.0f + 30.0f;
        float countX = screenWidth - countWidth - 20.0f;
        
    renderer->drawRect(countX - 15.0f, textY - 5.0f, countWidth, 28.0f, Color(0.1f, 0.1f, 0.15f, 0.85f));
        renderer->drawBorder(FloatRect(countX - 15.0f, textY - 5.0f, countWidth, 28.0f), Color(m_borderColor[0], m_borderColor[1], m_borderColor[2], 0.55f), 1.0f);
        
    renderer->drawText(countText, countX + 15.0f, textY + 15.0f, 14.0f, Color(0.92f, 0.92f, 0.92f, 1.0f));
    }
};

// Plugin factory
Plugin* create_alt_tab_plugin() {
    return new AltTabPlugin();
}

} // namespace havel
