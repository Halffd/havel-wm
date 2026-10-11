// Alt-Tab Plugin - Phase 4.1
// Window switcher with thumbnails

#include <wm/plugins/Plugin.hpp>
#include <wm/plugins/CompositorAPI.hpp>
#include <wm/plugins/PluginConfig.hpp>
#include <wm/render/OverlayRenderer.hpp>
#include <wm/render/AppIconLoader.hpp>
#include <wm/View.hpp>
#include <xkbcommon/xkbcommon-keysyms.h>
#include <cstdio>
#include <functional>
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
    uint32_t textureId = 0;   // View capture handle (0 = no thumbnail, placeholder drawn)
    uint32_t iconTextureId = 0;  // AppIconLoader handle (CPU pixel data)
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

    m_thumbnailWidth = config.getIntValue("alt_tab", "thumbnailWidth", 360);
        m_thumbnailHeight = config.getIntValue("alt_tab", "thumbnailHeight", 240);
        m_maxVisibleWindows = config.getIntValue("alt_tab", "maxVisibleWindows", 12);
        m_columns = std::max(1, config.getIntValue("alt_tab", "columns", 3));

        // Monitor targeting: "cursor" (output under the pointer),
        // "main"/"primary" (primary output), or a plain number =
        // fixed output index. With one output all resolve identically;
        // multi-output mapping lands when CompositorAPI enumerates
        // outputs.
        std::string mon = config.getValue("alt_tab", "monitor", "cursor");
        if (mon == "main" || mon == "primary") m_monitor = Monitor::Main;
        else if (mon == "fixed") m_monitor = Monitor::Fixed;
        else m_monitor = Monitor::Cursor;
        m_fixedMonitorIndex = std::max(0, config.getIntValue("alt_tab", "monitorIndex", 0));

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
        
        printf("[AltTab] Config loaded (cell=%dx%d, columns=%d, max=%d, monitor=%s)\n",
               m_thumbnailWidth, m_thumbnailHeight, m_columns,
               m_maxVisibleWindows,
               m_monitor == Monitor::Cursor ? "cursor" :
               m_monitor == Monitor::Main ? "main" : "fixed");

        // Configurable trigger binding (default Alt+Tab): the cycle keysym
        // is the keysym from the spec; the Alt modifier is derived from it.
        uint32_t mods = 0, sym = 0;
        if (config.getKeybindingParsed("alt_tab", &mods, &sym)) {
            m_triggerMods = mods;
            m_triggerKeysym = sym;
            m_selectOnModRelease = (mods & (1u << 3)) != 0;  // Alt in spec
            printf("[AltTab] Trigger binding from config: mods=0x%x keysym=0x%x\n",
                   mods, sym);
        }
    }

    bool onKey(const KeyEvent& event) override {
        bool triggerModHeld = (event.modifiers & m_triggerMods) != 0;
        bool triggerModOnly = (event.modifiers == m_triggerMods);

        // Trigger modifier released -> select current
        if (!event.pressed && !triggerModHeld && m_selectOnModRelease && m_visible) {
            select();
            return true;
        }

        // Trigger binding press (Alt+Tab / Alt+Shift+Tab by default)
        if (triggerModHeld && event.keysym == m_triggerKeysym && event.pressed) {
            if (!m_visible) {
                m_reverse = !triggerModOnly;  // extra modifier = reverse
                show();
            } else {
                if (!triggerModOnly) {
                    navigate(-1);
                } else {
                    navigate(1);
                }
            }
            return true;
        }
        
        if (!m_visible) return false;
        if (!event.pressed) return false;
        
  switch (event.keysym) {
    case XKB_KEY_Escape: // Escape
      hide();
      return true;

    case XKB_KEY_Return: // Enter
    case XKB_KEY_KP_Enter:
      select();
      return true;

    // Grid navigation: arrows move the selection (auto-scroll follows)
    case XKB_KEY_Right:
        navigate(1);
        return true;
    case XKB_KEY_Left:
        navigate(-1);
        return true;
    case XKB_KEY_Down:
        navigate(m_columns);
        return true;
    case XKB_KEY_Up:
        navigate(-m_columns);
        return true;
    }

    return false;
    }

    // ---- Mouse: hover, wheel scroll, button hit-testing ----------------
    // Hit-testing goes through the same layout helpers as the renderer
    // so clicks land exactly on the drawn pixels.
    void onMouseMotion(int x, int y) override {
        if (!m_visible) return;
        int cell = -1;
        HoverBtn btn = buttonAt(x, y, cell);
        if (cell != m_hoverCell || btn != m_hoverBtn) {
            m_hoverCell = cell;
            m_hoverBtn = btn;
        }
    }

    void onMouseButton(uint32_t button, bool pressed, int x, int y) override {
        if (!m_visible || !pressed) return;

        // Wheel: scroll the grid (button 4 = up, 5 = down)
        if (button == 4 || button == 5) {
            int dir = (button == 5) ? 1 : -1;
            m_scrollRow += dir * 1;
            clampScroll();
            return;
        }

        // Left press only
        if (button != 0x110) return;  // BTN_LEFT

        int cell = -1;
        HoverBtn btn = buttonAt(x, y, cell);
        if (cell < 0) {
            // Click outside the panel: cancel (Windows behavior)
            hide();
            return;
        }
        switch (btn) {
            case HoverBtn::Close: closeEntry(cell); return;
            case HoverBtn::Hide:  hideEntry(cell);  return;
            case HoverBtn::Move:  moveEntry(cell);  return;
            case HoverBtn::None:
            default:
                break;
        }
        // Click on the cell body: select + activate
        m_selectedIndex = cell;
        select();
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
    // Persistent overlay tree (rebuilt only on selection/window change)
    wlr_scene_tree_t* m_tree = nullptr;
    OverlayRenderer* m_renderer = nullptr;  // cached for hide-time teardown
    uint64_t m_lastSig = ~0ull;  // window-set + selection redraw signature
    // Trigger binding (overridable via config "keybinding", default Alt+Tab)
    uint32_t m_triggerMods = 1u << 3;   // Mod1 (Alt)
    uint32_t m_triggerKeysym = XKB_KEY_Tab;
    bool m_selectOnModRelease = true;   // select when the trigger mod lifts
    
  // Customizable settings
  int m_thumbnailWidth;
  int m_thumbnailHeight;
  int m_maxVisibleWindows;
  float m_bgColor[4];
  float m_borderColor[4];
  float m_textColor[4];

  // Grid layout (Windows-style task switcher)
  int m_columns = 3;
  enum class Monitor { Cursor, Main, Fixed };
  Monitor m_monitor = Monitor::Cursor;
  int m_fixedMonitorIndex = 0;

  // Grid geometry cache (computed in layoutGrid; shared by render +
  // mouse hit-testing so clicks land where the pixels are)
  Rect m_panelRect;
  Rect m_gridRect;
  int m_visibleRows = 1;
  int m_scrollRow = 0;
  int m_hoverCell = -1;
  enum class HoverBtn { None, Close, Hide, Move };
  HoverBtn m_hoverBtn = HoverBtn::None;
  static constexpr int BTN_CLOSE = 0;
  static constexpr int BTN_HIDE = 1;
  static constexpr int BTN_MOVE = 2;
  static constexpr int BTN_COUNT = 3;

    
    void show() {
        m_visible = true;
        m_selectedIndex = 0;
        m_reverse = false;
        m_scrollRow = 0;
        m_hoverCell = -1;
        m_hoverBtn = HoverBtn::None;
        m_visibleRows = 2;  // default until the first layoutGrid pass
        m_lastSig = ~0ull;
        
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
        m_lastSig = ~0ull;
        // Drop the persistent overlay tree or the full-screen dim and
        // thumbnails stay frozen on screen after the switch.
        if (m_renderer && m_tree) {
            m_renderer->destroyPersistentTree(&m_tree);
        }
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

        // Auto-scroll: keep the selected cell inside the visible rows
        // (Windows task-switcher behavior: the grid follows the
        // selection instead of the selection leaving the viewport).
        int selRow = m_selectedIndex / m_columns;
        if (selRow < m_scrollRow) m_scrollRow = selRow;
        if (selRow >= m_scrollRow + m_visibleRows) {
            m_scrollRow = selRow - m_visibleRows + 1;
        }
        clampScroll();
        printf("[AltTab] Selected: %s (%d/%zu)\n",
               m_windows[m_selectedIndex].title.c_str(),
               m_selectedIndex + 1, m_windows.size());
    }

    // ---- Grid geometry + hit-testing ---------------------------------
    // Single source of truth: renderOverlay and the mouse handlers both
    // go through these, so clicks always land on what was drawn.

    void layoutGrid(int screenW, int screenH) {
        // Panel: centered, sized to the grid plus header/footer chrome,
        // capped to the screen. columns shrink when fewer windows than
        // columns exist.
        int cols = std::min(m_columns, std::max(1, (int)m_windows.size()));
        int gap = 12;
        int headerH = 34;
        int footerH = 30;

        int gridW = cols * m_thumbnailWidth + (cols + 1) * gap;
        int rows = (int)((m_windows.size() + cols - 1) / cols);
        int gridH = rows * (m_thumbnailHeight + gap) + gap;

        int panelW = std::min(screenW - 24, gridW + 2 * gap);
        int panelH = std::min(screenH - 24, headerH + gridH + footerH + 2 * gap);
        m_visibleRows = std::max(1, (panelH - headerH - footerH - 2 * gap - gap)
                                     / (m_thumbnailHeight + gap));

        clampScroll();

        m_panelRect = Rect{(screenW - panelW) / 2, (screenH - panelH) / 2, panelW, panelH};
        m_gridRect = Rect{m_panelRect.x + gap,
                          m_panelRect.y + headerH + gap,
                          panelW - 2 * gap - 14,   // 14px reserved for scrollbar
                          panelH - headerH - footerH - 2 * gap};
    }

    Rect cellRect(int index) const {
        int rel = index - m_scrollRow * m_columns;
        if (rel < 0) return Rect{};
        int col = rel % m_columns;
        int row = rel / m_columns;
        int gap = 12;
        return Rect{m_gridRect.x + gap + col * (m_thumbnailWidth + gap),
                    m_gridRect.y + gap + row * (m_thumbnailHeight + gap),
                    m_thumbnailWidth, m_thumbnailHeight};
    }

    Rect buttonRect(int index, int button) const {
        Rect cell = cellRect(index);
        // Button strip across the cell's top-right; 22x18 each, 4px gap
        int bw = 22, bh = 18;
        int x = cell.x + cell.w - (BTN_COUNT - button) * (bw + 4) - 4;
        int y = cell.y + 4;
        return Rect{x, y, bw, bh};
    }

    int cellAt(int x, int y) const {
        if (x < m_gridRect.x || y < m_gridRect.y ||
            x >= m_gridRect.x + m_gridRect.w || y >= m_gridRect.y + m_gridRect.h) {
            return -1;
        }
        int rel = (y - m_gridRect.y) / (m_thumbnailHeight + 12);
        int col = (x - m_gridRect.x) / (m_thumbnailWidth + 12);
        if (col >= m_columns) return -1;
        int index = (m_scrollRow + rel) * m_columns + col;
        if (index < 0 || index >= (int)m_windows.size()) return -1;
        Rect r = cellRect(index);
        if (x < r.x || y < r.y || x >= r.x + r.w || y >= r.y + r.h) return -1;
        return index;
    }

    HoverBtn buttonAt(int x, int y, int& cell) const {
        cell = cellAt(x, y);
        if (cell < 0) return HoverBtn::None;
        for (int b = 0; b < BTN_COUNT; b++) {
            Rect r = buttonRect(cell, b);
            if (x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h) {
                return (HoverBtn)(b + 1);  // enum starts at None
            }
        }
        return HoverBtn::None;
    }

    void clampScroll() {
        int rows = (int)((m_windows.size() + m_columns - 1) / m_columns);
        int maxScroll = std::max(0, rows - m_visibleRows);
        if (m_scrollRow > maxScroll) m_scrollRow = maxScroll;
        if (m_scrollRow < 0) m_scrollRow = 0;
    }

    void closeEntry(int index) {
        if (index < 0 || index >= (int)m_windows.size()) return;
        if (View* v = (View*)m_windows[index].viewPtr) {
            m_api->closeView(v);
        }
        removeEntry(index);
    }

    void hideEntry(int index) {
        if (index < 0 || index >= (int)m_windows.size()) return;
        if (View* v = (View*)m_windows[index].viewPtr) {
            m_api->minimizeView(v);
        }
        removeEntry(index);
    }

    void moveEntry(int index) {
        if (index < 0 || index >= (int)m_windows.size()) return;
        uint32_t target = (m_api->getActiveWorkspace() + 1) % m_api->getWorkspaceCount();
        if (View* v = (View*)m_windows[index].viewPtr) {
            m_api->moveViewToWorkspace(v, target);
        }
        removeEntry(index);
    }

    void removeEntry(int index) {
        if (index < 0 || index >= (int)m_windows.size()) return;
        m_windows.erase(m_windows.begin() + index);
        if (m_selectedIndex >= (int)m_windows.size()) {
            m_selectedIndex = (int)m_windows.size() - 1;
        }
        if (m_selectedIndex < 0) m_selectedIndex = 0;
        clampScroll();
        m_hoverCell = -1;
        m_hoverBtn = HoverBtn::None;
        if (m_windows.empty()) hide();
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

        // Draw into a PERSISTENT tree under the overlay layer. Rects drawn
        // into the per-frame tree did not reach the output in this wlroots
        // build (observed live), while persistent trees do - so the
        // switcher follows the proven path. The tree is created lazily and
        // its contents rebuilt only when the visible state (selection,
        // hover, scroll, window set) changes.
        OverlayRenderer* renderer = static_cast<OverlayRenderer*>(rendererPtr);
        m_renderer = renderer;
        if (!m_tree) {
            m_tree = renderer->createPersistentTree();
            if (!m_tree) return;
            m_lastSig = ~0ull;
        }

        // Signature over everything the user can see. m_columns is in it
        // so a live config change (plugin_config from config.hv) rebuilds
        // the grid on the next frame.
        uint64_t sig = 0;
        for (size_t i = 0; i < m_windows.size(); i++) {
            sig = sig * 131 + std::hash<std::string>{}(m_windows[i].title);
        }
        sig = sig * 131 + (uint64_t)m_selectedIndex;
        sig = sig * 131 + (uint64_t)m_scrollRow;
        sig = sig * 131 + (uint64_t)m_hoverCell;
        sig = sig * 131 + (uint64_t)(int)m_hoverBtn;
        sig = sig * 131 + (uint64_t)m_columns;
        if (sig == m_lastSig) return;
        m_lastSig = sig;

        // Rebuild: rect nodes cannot be cheaply re-laid-out, so drop the
        // old tree and draw fresh content into a new one.
        renderer->destroyPersistentTree(&m_tree);
        m_tree = renderer->createPersistentTree();
        if (!m_tree) return;
        renderer->setTargetTree(m_tree);

        int screenWidth = renderer->getScreenWidth();
        int screenHeight = renderer->getScreenHeight();
        layoutGrid(screenWidth, screenHeight);

        // Full-screen dim
        renderer->drawRect(0, 0, (float)screenWidth, (float)screenHeight,
                           Color(m_bgColor[0], m_bgColor[1], m_bgColor[2], m_bgColor[3]));

        // Panel background + border
        renderer->drawRect((float)m_panelRect.x, (float)m_panelRect.y,
                          (float)m_panelRect.w, (float)m_panelRect.h,
                          Color(0.13f, 0.13f, 0.17f, 0.97f));
        renderer->drawBorder(FloatRect((float)m_panelRect.x, (float)m_panelRect.y,
                                       (float)m_panelRect.w, (float)m_panelRect.h),
                             Color(m_borderColor[0]*0.6f, m_borderColor[1]*0.6f,
                                   m_borderColor[2]*0.6f, 0.8f), 1.0f);

        // Header: title + counter
        char countText[48];
        snprintf(countText, sizeof(countText), "Windows - %d / %zu",
                 m_selectedIndex + 1, m_windows.size());
        renderer->drawText(countText, (float)(m_panelRect.x + 12),
                          (float)(m_panelRect.y + 10), 16.0f,
                          Color(m_textColor[0], m_textColor[1], m_textColor[2], 1.0f));

        if (m_windows.empty() || m_selectedIndex < 0 ||
            m_selectedIndex >= (int)m_windows.size()) {
            renderer->setTargetTree(nullptr);
            return;
        }

        // Visible cells: row-major grid, scrolled by m_scrollRow
        int firstIdx = m_scrollRow * m_columns;
        int lastIdx = std::min((int)m_windows.size(),
                               (m_scrollRow + m_visibleRows) * m_columns);
        for (int index = firstIdx; index < lastIdx; index++) {
            const WindowEntry& entry = m_windows[index];
            Rect cell = cellRect(index);
            bool isSelected = (index == m_selectedIndex);
            bool isHovered = (index == m_hoverCell);

            // Cell background
            Color bg = isSelected ? Color(0.25f, 0.32f, 0.42f, 0.97f)
                     : isHovered  ? Color(0.19f, 0.22f, 0.28f, 0.95f)
                                  : Color(0.15f, 0.15f, 0.19f, 0.94f);
            renderer->drawRect((float)cell.x, (float)cell.y,
                              (float)cell.w, (float)cell.h, bg);

            // Icon (real icon pixels, else placeholder)
            if (entry.iconTextureId != 0) {
                AppIconLoader* icons = AppIconLoader::getInstance();
                const uint32_t* iconPixels = icons ? icons->getIconPixels(entry.iconTextureId) : nullptr;
                int iw = 0, ih = 0;
                if (icons) icons->getIconDimensions(entry.iconTextureId, &iw, &ih);
                int iconSize = 28;
                if (iconPixels && iw > 0 && ih > 0) {
                    renderer->drawPixels(iconPixels, iw, ih,
                                        (float)(cell.x + 8), (float)(cell.y + 8),
                                        (float)iconSize, (float)iconSize);
                } else {
                    renderer->drawRect((float)(cell.x + 8), (float)(cell.y + 8),
                                      (float)iconSize, (float)iconSize,
                                      Color(0.4f, 0.4f, 0.45f, 1.0f));
                }
            }

            // Title (truncated to the cell width)
            std::string label = entry.appId.empty() ? entry.title : entry.appId;
            float maxTextW = (float)cell.w - 16.0f;
            float estW = (float)label.size() * 9.0f;
            while (label.size() > 3 && estW > maxTextW) {
                label.resize(label.size() - 4);
                estW = (float)label.size() * 9.0f;
            }
            renderer->drawText(label.c_str(), (float)(cell.x + 8),
                              (float)(cell.y + cell.h - 30.0f), 13.0f,
                              Color(m_textColor[0], m_textColor[1],
                                    m_textColor[2], 0.95f));

            // Action buttons: [X] close  [-] hide  [>] move-to-ws
            const char* glyphs[BTN_COUNT] = {"X", "-", ">"};
            for (int b = 0; b < BTN_COUNT; b++) {
                Rect br = buttonRect(index, b);
                bool btnHovered = (m_hoverBtn == (HoverBtn)(b + 1) && isHovered);
                Color btnBg = btnHovered ? Color(0.85f, 0.3f, 0.25f, 0.95f)
                                         : Color(0.32f, 0.32f, 0.38f, 0.9f);
                if (b == BTN_HIDE && btnHovered)
                    btnBg = Color(0.85f, 0.65f, 0.2f, 0.95f);
                if (b == BTN_MOVE && btnHovered)
                    btnBg = Color(0.3f, 0.6f, 0.85f, 0.95f);
                renderer->drawRect((float)br.x, (float)br.y,
                                  (float)br.w, (float)br.h, btnBg);
                renderer->drawText(glyphs[b], (float)(br.x + 6),
                                  (float)(br.y + 3), 11.0f,
                                  Color(1.0f, 1.0f, 1.0f, 1.0f));
            }

            // Selected: accent border (the user-visible selection mark)
            if (isSelected) {
                renderer->drawBorder(FloatRect((float)cell.x - 2, (float)cell.y - 2,
                                               (float)cell.w + 4, (float)cell.h + 4),
                                     Color(m_borderColor[0], m_borderColor[1],
                                           m_borderColor[2], 1.0f), 3.0f);
            }
        }

        // Scrollbar (only when content overflows)
        int totalRows = (int)((m_windows.size() + m_columns - 1) / m_columns);
        if (totalRows > m_visibleRows) {
            Rect track{m_gridRect.x + m_gridRect.w + 4, m_gridRect.y,
                       8, m_gridRect.h};
            renderer->drawRect((float)track.x, (float)track.y,
                              (float)track.w, (float)track.h,
                              Color(0.25f, 0.25f, 0.3f, 0.8f));
            float frac = (float)m_visibleRows / totalRows;
            int thumbH = std::max(24, (int)(track.h * frac));
            int thumbY = track.y;
            if (totalRows > m_visibleRows) {
                thumbY += (int)((track.h - thumbH) *
                                ((float)m_scrollRow / (totalRows - m_visibleRows)));
            }
            renderer->drawRect((float)track.x, (float)thumbY,
                              (float)track.w, (float)thumbH,
                              Color(0.55f, 0.6f, 0.7f, 0.9f));
        }

        // Footer hints
        const char* hint = "Enter: select | Esc: cancel | Wheel: scroll | X close | - hide | > move";
        float hintW = strlen(hint) * 7.0f + 30.0f;
        float hintX = m_panelRect.x + (m_panelRect.w - hintW) / 2.0f;
        float hintY = m_panelRect.y + m_panelRect.h - 28.0f;
        renderer->drawText(hint, hintX + 10.0f, hintY + 8.0f, 12.0f,
                          Color(m_textColor[0]*0.75f, m_textColor[1]*0.75f,
                                m_textColor[2]*0.75f, 0.9f));

        renderer->setTargetTree(nullptr);
    }
};

// Plugin factory
Plugin* create_alt_tab_plugin() {
    return new AltTabPlugin();
}

} // namespace havel
