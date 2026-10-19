// Workspace Overview Plugin - Phase 4.2
// Exposé-style workspace and window overview

#include <wm/plugins/Plugin.hpp>
#include <wm/plugins/CompositorAPI.hpp>
#include <wm/plugins/PluginConfig.hpp>
#include <wm/render/OverlayRenderer.hpp>
#include <wm/View.hpp>
#include <xkbcommon/xkbcommon-keysyms.h>
#include <cstdio>
#include <algorithm>
#include <cstring>
#include <vector>
#include <string>
#include <chrono>

namespace havel {

// Get current time in milliseconds
static uint64_t getMonotonicTimeMs() {
    auto now = std::chrono::steady_clock::now();
    return std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
}

/**
 * Window entry for overview
 */
struct OverviewWindow {
    void* viewPtr = nullptr;  // Opaque pointer - actual View* used internally
    std::string appId;
    std::string title;
    uint32_t workspace = 0;
    int gridX = 0, gridY = 0;  // Grid position
    int x = 0, y = 0, w = 0, h = 0;  // Original geometry
    std::vector<uint32_t> thumbPixels;
    int thumbW = 0, thumbH = 0;
};

/**
 * Workspace entry for overview
 */
struct OverviewWorkspace {
    uint32_t id = 0;
    std::vector<OverviewWindow> windows;
    int gridX = 0, gridY = 0;  // Position in overview grid
};

/**
 * Workspace Overview Plugin
 * 
 * Provides workspace/window overview via:
 * - Grid layout of all workspaces
 * - Window thumbnails per workspace
 * - Keyboard/mouse selection
 * 
 * Keybindings:
 * - Meta+W: Toggle overview
 * - Arrow keys: Navigate
 * - Enter: Select workspace
 * - Escape: Cancel
 */
class OverviewPlugin : public Plugin {
public:
    const char* name() const override { return "overview"; }
    const char* version() const override { return "0.1.0"; }
    
    void init(CompositorAPI* api) override {
        m_api = api;
        m_visible = false;
        m_selectedWorkspace = 0;
        m_selectedWindow = -1;
        
        printf("[Overview] Initialized (%d workspaces)\n", m_api->getWorkspaceCount());
    }
    
    void fini() override {
        if (m_visible) {
            hide();
        }
        printf("[Overview] Finalized\n");
        m_api = nullptr;
    }
    
    void loadConfig(const std::string& configPath) override {
        (void)configPath;
        // Configurable toggle binding (default Meta+W)
        auto& config = PluginConfig::getInstance();
        uint32_t mods = 0, sym = 0;
        if (config.getKeybindingParsed("overview", &mods, &sym)) {
            m_bindingMods = mods;
            m_bindingKeysym = sym;
            printf("[Overview] Toggle binding from config: mods=0x%x keysym=0x%x\n",
                   mods, sym);
        }
        printf("[Overview] Config loaded\n");
    }
    
    void onOutputFrame(const OutputFrameEvent& event) override {
        if (!m_visible && m_animOpacity <= 0.0f) return;
        if (!m_animating) return;
        
        // Update animation
        uint64_t now = getMonotonicTimeMs();
        float elapsed = (float)(now - m_animStartTime) / 1000.0f;  // seconds
        float progress = elapsed / m_animDuration;
        
        if (progress >= 1.0f) {
            progress = 1.0f;
            m_animating = false;
            
            if (!m_visible) {
                m_animOpacity = 0.0f;  // Fully hidden
            }
        }
        
        // Ease-out cubic for smooth animation
        progress = 1.0f - (1.0f - progress) * (1.0f - progress) * (1.0f - progress);
        
        if (m_visible) {
            m_animOpacity = progress;  // Fade in from 0 to 1
        } else {
            m_animOpacity = 1.0f - progress;  // Fade out from 1 to 0
        }
        
        m_api->scheduleRedraw();
        (void)event;
    }
    
    bool onKey(const KeyEvent& event) override {
        // Configurable toggle binding (default Meta+W); exact modifier match
        if (event.pressed && event.modifiers == m_bindingMods &&
            event.keysym == m_bindingKeysym) {
            toggle();
            return true;
        }

        if (!m_visible) return false;
        if (!event.pressed) return false;

        switch (event.keysym) {
            case XKB_KEY_Escape:  // Escape - cancel
                hide();
                return true;

            case XKB_KEY_Return:  // Enter - select
            case XKB_KEY_KP_Enter:
                select();
                return true;

            case XKB_KEY_Up:  // Up
                navigate(0, -1);
                return true;

            case XKB_KEY_Down:  // Down
                navigate(0, 1);
                return true;

            case XKB_KEY_Left:  // Left
                navigate(-1, 0);
                return true;

            case XKB_KEY_Right:  // Right
                navigate(1, 0);
                return true;

            case XKB_KEY_space:  // Space - toggle between workspace and window selection
                toggleSelectionLevel();
                return true;
        }

        return false;
    }
    
private:
    CompositorAPI* m_api = nullptr;
    bool m_visible;
    int m_selectedWorkspace;
    int m_selectedWindow;
    // Toggle binding (overridable via config "keybinding", default Meta+W)
    uint32_t m_bindingMods = 1u << 6;  // Mod4 (Meta/Logo)
    uint32_t m_bindingKeysym = XKB_KEY_w;
    std::vector<OverviewWorkspace> m_workspaces;
    int m_gridCols = 3;
    int m_gridRows = 4;
    
    // Animation state
    bool m_animating = false;
    float m_animOpacity = 0.0f;  // 0.0 = fully transparent, 1.0 = fully opaque
    uint64_t m_animStartTime = 0;
    float m_animDuration = 0.25f;  // 250ms fade animation
    
    void toggle() {
        if (m_visible) {
            hide();
        } else {
            show();
        }
    }
    
    void show() {
        m_visible = true;
        m_selectedWorkspace = m_api->getActiveWorkspace();
        m_selectedWindow = -1;
        
        // Start fade-in animation
        m_animating = true;
        m_animStartTime = getMonotonicTimeMs();
        m_animOpacity = 0.0f;
        
        // Collect all workspaces and windows
        collectWorkspaces();
        
        // Calculate grid layout
        calculateGridLayout();
        
        printf("[Overview] Showing (%d workspaces)\n", 
               (int)m_workspaces.size());
    }
    
    void hide() {
        // Start fade-out animation
        m_animating = true;
        m_animStartTime = getMonotonicTimeMs();
        // m_animOpacity stays at current value, will fade to 0
        
        // Clear data after animation completes
        m_workspaces.clear();
        m_selectedWorkspace = 0;
        m_selectedWindow = -1;
        printf("[Overview] Hidden\n");
    }

    void toggleSelectionLevel() {
        // Toggle between workspace and window selection
        OverviewWorkspace& ws = m_workspaces[m_selectedWorkspace];
        if (!ws.windows.empty()) {
            if (m_selectedWindow < 0) {
                // Enter window selection - select first window
                m_selectedWindow = 0;
                printf("[Overview] Entered window selection\n");
            } else {
                // Exit to workspace selection
                m_selectedWindow = -1;
                printf("[Overview] Exited to workspace selection\n");
            }
        }
    }
    
    void collectWorkspaces() {
        m_workspaces.clear();
        
        uint32_t wsCount = m_api->getWorkspaceCount();
        
        for (uint32_t ws = 0; ws < wsCount; ws++) {
            OverviewWorkspace ows;
            ows.id = ws;
            
            // Get real windows for this workspace
            std::vector<View*> views = m_api->getViewsInWorkspace(ws);
            
            for (View* view : views) {
                OverviewWindow win;
                win.viewPtr = view;
                // Get window metadata from API
                win.appId = m_api->getViewAppId(view);
                win.title = m_api->getViewTitle(view);
                
                // Use actual geometry from view
                Rect geom = view->geom();
                win.x = geom.x;
                win.y = geom.y;
                win.w = geom.w;
                win.h = geom.h;
                win.workspace = ws;
                
                {
                    std::vector<uint32_t> pixels;
                    int pw = 0, ph = 0;
                    if (m_api->captureViewPixels(view, pixels, &pw, &ph) && pw > 0 && ph > 0) {
                        const int maxW = 120, maxH = 64;
                        float scl = std::min((float)maxW / pw, (float)maxH / ph);
                        if (scl > 1.0f) scl = 1.0f;
                        int tw = std::max(1, (int)(pw * scl));
                        int th = std::max(1, (int)(ph * scl));
                        win.thumbPixels.resize((size_t)tw * th);
                        win.thumbW = tw;
                        win.thumbH = th;
                        for (int y2 = 0; y2 < th; y2++) {
                            const uint32_t* src = &pixels[(size_t)(y2 * ph / th) * pw];
                            uint32_t* dst = &win.thumbPixels[(size_t)y2 * tw];
                            for (int x2 = 0; x2 < tw; x2++) dst[x2] = src[(int)((x2 + 0.5f) * pw / tw)];
                        }
                    }
                }
                ows.windows.push_back(win);
            }
            
            m_workspaces.push_back(ows);
        }
        
        printf("[Overview] Collected %zu workspaces with real window data\n", 
               m_workspaces.size());
    }
    
    void calculateGridLayout() {
        uint32_t wsCount = (uint32_t)m_workspaces.size();
        
        // Calculate grid dimensions
        m_gridCols = 3;
        m_gridRows = (wsCount + m_gridCols - 1) / m_gridCols;
        
        // Assign grid positions to workspaces
        for (uint32_t i = 0; i < wsCount; i++) {
            m_workspaces[i].gridX = i % m_gridCols;
            m_workspaces[i].gridY = i / m_gridCols;
            
            // Assign grid positions to windows within workspace
            int winCols = 3;
            for (size_t j = 0; j < m_workspaces[i].windows.size(); j++) {
                m_workspaces[i].windows[j].gridX = j % winCols;
                m_workspaces[i].windows[j].gridY = j / winCols;
            }
        }
    }
    
    void navigate(int dx, int dy) {
        if (m_workspaces.empty()) return;

        OverviewWorkspace& ws = m_workspaces[m_selectedWorkspace];

        // If workspace has windows and we're navigating within it
        if (!ws.windows.empty() && m_selectedWindow >= 0) {
            OverviewWindow& win = ws.windows[m_selectedWindow];

            int newCol = win.gridX + dx;
            int newRow = win.gridY + dy;

            // Clamp to valid range
            if (newCol < 0) newCol = 0;
            if (newRow < 0) newRow = 0;

            // Find window at new position
            int newIndex = -1;
            for (size_t i = 0; i < ws.windows.size(); i++) {
                if (ws.windows[i].gridX == newCol && ws.windows[i].gridY == newRow) {
                    newIndex = (int)i;
                    break;
                }
            }

            if (newIndex >= 0) {
                m_selectedWindow = newIndex;
                printf("[Overview] Window: %s\n", ws.windows[m_selectedWindow].title.c_str());
                return;
            }
        }

        // Navigate workspaces (or enter workspace if no window selected)
        OverviewWorkspace& current = m_workspaces[m_selectedWorkspace];
        int newCol = current.gridX + dx;
        int newRow = current.gridY + dy;

        // Clamp to valid range
        if (newCol < 0) newCol = 0;
        if (newRow < 0) newRow = 0;

        // Find workspace at new position
        for (size_t i = 0; i < m_workspaces.size(); i++) {
            if (m_workspaces[i].gridX == newCol && m_workspaces[i].gridY == newRow) {
                m_selectedWorkspace = (int)i;
                m_selectedWindow = -1;  // Reset window selection when changing workspace
                printf("[Overview] Workspace: %d\n", m_selectedWorkspace + 1);
                return;
            }
        }
    }
    
    void select() {
        if (m_selectedWorkspace < 0 || m_selectedWorkspace >= (int)m_workspaces.size()) {
            hide();
            return;
        }

        OverviewWorkspace& ws = m_workspaces[m_selectedWorkspace];

        if (m_selectedWindow >= 0 && m_selectedWindow < (int)ws.windows.size()) {
            // Select specific window
            OverviewWindow& win = ws.windows[m_selectedWindow];
            printf("[Overview] Selecting window: %s\n", win.title.c_str());

            if (win.viewPtr) {
                m_api->focusView((View*)win.viewPtr);
            }

            // Switch to workspace
            if (ws.id != m_api->getActiveWorkspace()) {
                m_api->setActiveWorkspace(ws.id);
            }
        } else {
            // Select workspace
            printf("[Overview] Selecting workspace: %d\n", ws.id + 1);
            m_api->setActiveWorkspace(ws.id);
        }

        hide();
    }
    
    void renderOverlay(void* rendererPtr) override {
        if (!rendererPtr) return;
        if (!m_visible && m_animOpacity <= 0.0f) return;
        
        OverlayRenderer* renderer = static_cast<OverlayRenderer*>(rendererPtr);
        
        // Apply animation opacity
        float alpha = m_animOpacity;
        if (alpha <= 0.01f) return;  // Too transparent to see

        // The overview covers the monitor the cursor is on. renderOverlay
        // runs once per output pass; skip the passes for other monitors.
        CompositorAPI::OutputTarget out = m_api->currentOutputTarget();
        if (!out.underCursor) return;
        const int originX = out.x, originY = out.y;
        const int screenWidth = out.w;
        const int screenHeight = out.h;

        // Draw semi-transparent background with animation opacity
        renderer->drawRect((float)originX, (float)originY,
                          (float)screenWidth, (float)screenHeight,
                          Color(0.0f, 0.0f, 0.0f, 0.8f * alpha));

        // Calculate workspace grid layout
        int wsCount = (int)m_workspaces.size();
        int gridCols = 3;
        int gridRows = (wsCount + gridCols - 1) / gridCols;

        int wsWidth = (screenWidth - 100) / gridCols;
        int wsHeight = (screenHeight - 100) / gridRows;
        int spacing = 20;

        // Draw each workspace
        for (size_t i = 0; i < m_workspaces.size(); i++) {
            OverviewWorkspace& ws = m_workspaces[i];
            int col = ws.gridX;
            int row = ws.gridY;
            int x = originX + 50 + col * (wsWidth + spacing);
            int y = originY + 50 + row * (wsHeight + spacing);

            bool isSelected = ((int)i == m_selectedWorkspace);

            // Draw workspace background
            Color bgColor = isSelected ? Color(0.2f, 0.3f, 0.4f, 0.9f) : Color(0.15f, 0.15f, 0.15f, 0.8f);
            renderer->drawRect((float)x, (float)y, (float)wsWidth, (float)wsHeight, bgColor);

            // Draw border
            Color borderColor = isSelected ? Color(1.0f, 1.0f, 1.0f, 1.0f) : Color(0.4f, 0.4f, 0.4f, 0.5f);
            renderer->drawBorder(FloatRect((float)x, (float)y, (float)wsWidth, (float)wsHeight), borderColor, isSelected ? 3.0f : 2.0f);

            // Draw workspace number
            char wsLabel[16];
            snprintf(wsLabel, sizeof(wsLabel), "Workspace %d", ws.id + 1);
            renderer->drawTextCentered(wsLabel, (float)(x + wsWidth/2), (float)(y + 20), 16.0f, Color(0.8f, 0.8f, 0.8f, 1.0f));

            // Draw window thumbnails (miniature rectangles)
            if (!ws.windows.empty()) {
                int winCols = 3;
                int winRows = (ws.windows.size() + winCols - 1) / winCols;
                int winWidth = (wsWidth - 20) / winCols;
                int winHeight = (wsHeight - 60) / winRows;
                int winSpacing = 5;

                for (size_t j = 0; j < ws.windows.size(); j++) {
                    OverviewWindow& win = ws.windows[j];
                    int winCol = win.gridX;
                    int winRow = win.gridY;
                    int winX = x + 10 + winCol * (winWidth + winSpacing);
                    int winY = y + 40 + winRow * (winHeight + winSpacing);

                    bool isWinSelected = ((int)j == m_selectedWindow && isSelected);

                    // Draw window thumbnail
                    Color winColor;
                    if (isWinSelected) {
                        winColor = Color(0.4f, 0.5f, 0.6f, 0.9f);  // Bright blue for selected
                    } else if (isSelected) {
                        winColor = Color(0.35f, 0.4f, 0.45f, 0.85f);  // Lighter when workspace selected
                    } else {
                        winColor = Color(0.3f, 0.3f, 0.35f, 0.8f);  // Normal
                    }
                    if (!win.thumbPixels.empty()) {
                        renderer->drawPixels(win.thumbPixels.data(),
                                             win.thumbW, win.thumbH,
                                             (float)winX, (float)winY,
                                             (float)winWidth, (float)winHeight);
                    }
                    if (win.thumbPixels.empty()) {
                        // Placeholder rect for uncapturable clients
                        Color ph;
                        if (isWinSelected) ph = Color(0.4f, 0.5f, 0.6f, 0.9f);
                        else if (isSelected) ph = Color(0.35f, 0.4f, 0.45f, 0.85f);
                        else ph = Color(0.3f, 0.3f, 0.35f, 0.8f);
                        renderer->drawRect((float)winX, (float)winY, (float)winWidth, (float)winHeight, ph);
                    }
                    // Draw window border
                    Color winBorder;
                    float borderWidth;
                    if (isWinSelected) {
                        winBorder = Color(1.0f, 1.0f, 1.0f, 1.0f);  // White border for selected
                        borderWidth = 3.0f;
                    } else if (isSelected) {
                        winBorder = Color(0.7f, 0.7f, 0.7f, 0.7f);  // Light border when workspace selected
                        borderWidth = 2.0f;
                    } else {
                        winBorder = Color(0.5f, 0.5f, 0.5f, 0.5f);  // Dim border
                        borderWidth = 1.0f;
                    }
                    renderer->drawBorder(FloatRect((float)winX, (float)winY, (float)winWidth, (float)winHeight), winBorder, borderWidth);
                }
            }

            // Draw window count
            if (!ws.windows.empty()) {
                char winLabel[32];
                snprintf(winLabel, sizeof(winLabel), "%zu window(s)", ws.windows.size());
                renderer->drawTextCentered(winLabel, (float)(x + wsWidth/2), (float)(y + wsHeight - 20), 12.0f, Color(0.6f, 0.6f, 0.6f, 1.0f));
            }
        }

        // Draw instruction text
        const char* instruction = "Arrows: Navigate | Space: Window/Workspace | Enter: Select | Esc: Cancel";
        float textWidth = strlen(instruction) * 10.0f;
        float textX = (screenWidth - textWidth) / 2.0f;
        renderer->drawText(instruction, textX, (float)(screenHeight - 40), 14.0f, Color(0.8f, 0.8f, 0.8f, 1.0f));
    }
};

// Plugin factory
Plugin* create_overview_plugin() {
    return new OverviewPlugin();
}

} // namespace havel
