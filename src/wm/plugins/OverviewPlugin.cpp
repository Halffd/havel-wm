// Workspace Overview Plugin - Phase 4.2
// Exposé-style workspace and window overview

#include <wm/plugins/Plugin.hpp>
#include <wm/plugins/CompositorAPI.hpp>
#include <wm/render/OverlayRenderer.hpp>
#include <wm/View.hpp>
#include <cstdio>
#include <cstring>
#include <vector>
#include <string>

namespace havel {

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
        printf("[Overview] Config loaded\n");
    }
    
    bool onKey(const KeyEvent& event) override {
        constexpr uint32_t MOD_LOGO = 1 << 6;
        
        // Meta+W toggles overview
        if (event.pressed && (event.modifiers & MOD_LOGO) && event.keycode == 32) {
            toggle();
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
                
            case 103:  // Up
                navigate(0, -1);
                return true;
                
            case 108:  // Down
                navigate(0, 1);
                return true;
                
            case 105:  // Left
                navigate(-1, 0);
                return true;
                
            case 106:  // Right
                navigate(1, 0);
                return true;
        }
        
        return false;
    }
    
private:
    CompositorAPI* m_api = nullptr;
    bool m_visible;
    int m_selectedWorkspace;
    int m_selectedWindow;
    std::vector<OverviewWorkspace> m_workspaces;
    int m_gridCols = 3;
    int m_gridRows = 4;
    
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
        
        // Collect all workspaces and windows
        collectWorkspaces();
        
        // Calculate grid layout
        calculateGridLayout();
        
        printf("[Overview] Showing (%d workspaces)\n", 
               (int)m_workspaces.size());
    }
    
    void hide() {
        m_visible = false;
        m_workspaces.clear();
        m_selectedWorkspace = 0;
        m_selectedWindow = -1;
        printf("[Overview] Hidden\n");
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
        
        if (m_selectedWindow >= 0 && m_selectedWindow < (int)m_workspaces[m_selectedWorkspace].windows.size()) {
            // Navigate windows within workspace
            OverviewWorkspace& ws = m_workspaces[m_selectedWorkspace];
            OverviewWindow& win = ws.windows[m_selectedWindow];
            
            int newCol = win.gridX + dx;
            int newRow = win.gridY + dy;
            
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
        
        // Navigate workspaces
        OverviewWorkspace& current = m_workspaces[m_selectedWorkspace];
        int newCol = current.gridX + dx;
        int newRow = current.gridY + dy;
        
        // Find workspace at new position
        for (size_t i = 0; i < m_workspaces.size(); i++) {
            if (m_workspaces[i].gridX == newCol && m_workspaces[i].gridY == newRow) {
                m_selectedWorkspace = (int)i;
                m_selectedWindow = -1;  // Reset window selection
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
        if (!m_visible || !rendererPtr) return;

        OverlayRenderer* renderer = static_cast<OverlayRenderer*>(rendererPtr);

        int screenWidth = renderer->getScreenWidth();
        int screenHeight = renderer->getScreenHeight();

        // Draw semi-transparent background
        renderer->drawRect(0, 0, screenWidth, screenHeight, Color(0.0f, 0.0f, 0.0f, 0.8f));

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
            int x = 50 + col * (wsWidth + spacing);
            int y = 50 + row * (wsHeight + spacing);

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
                    Color winColor = isWinSelected ? Color(0.4f, 0.5f, 0.6f, 0.9f) : Color(0.3f, 0.3f, 0.35f, 0.8f);
                    renderer->drawRect((float)winX, (float)winY, (float)winWidth, (float)winHeight, winColor);

                    // Draw window border
                    Color winBorder = isWinSelected ? Color(1.0f, 1.0f, 1.0f, 1.0f) : Color(0.5f, 0.5f, 0.5f, 0.5f);
                    renderer->drawBorder(FloatRect((float)winX, (float)winY, (float)winWidth, (float)winHeight), winBorder, isWinSelected ? 2.0f : 1.0f);
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
        const char* instruction = "Arrows: Navigate | Enter: Select | Esc: Cancel";
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
