#pragma once

#include <wm/Types.hpp>
#include <vector>
#include <functional>

namespace havel {

/**
 * Workspace preview for overview overlay
 */
struct WorkspacePreview {
    uint32_t workspaceId = 0;
    int x = 0, y = 0, w = 0, h = 0;
    int windowCount = 0;
    bool isActive = false;
    std::vector<uint64_t> windowIds;
};

/**
 * Overview (Exposé) overlay state
 */
struct OverviewState {
    bool visible = false;
    int selectedWorkspace = -1;
    int hoveredWindow = -1;
    std::vector<WorkspacePreview> workspaces;
    
    // Layout
    int gridCols = 3;
    int gridRows = 4;
    int spacing = 20;
    int padding = 40;
};

/**
 * Workspace Overview (Exposé) renderer
 * 
 * Shows grid of all workspaces with window thumbnails.
 * Allows quick workspace switching and window selection.
 */
class OverviewOverlay {
public:
    OverviewOverlay();
    ~OverviewOverlay();
    
    // Show/hide overlay
    void show(int numWorkspaces, int activeWorkspace);
    void hide();
    void toggle(int numWorkspaces, int activeWorkspace);
    bool isVisible() const { return m_state.visible; }
    
    // Navigation
    void navigateUp();
    void navigateDown();
    void navigateLeft();
    void navigateRight();
    void select();
    void cancel();
    
    // Mouse interaction
    void handleMouseMove(int x, int y);
    void handleMouseClick(int x, int y);
    
    // Callbacks
    using WorkspaceCallback = std::function<void(uint32_t workspaceId)>;
    using WindowCallback = std::function<void(uint64_t windowId)>;
    
    void setWorkspaceCallback(WorkspaceCallback cb) { m_workspaceCallback = cb; }
    void setWindowCallback(WindowCallback cb) { m_windowCallback = cb; }
    
    // Get state
    const OverviewState& state() const { return m_state; }
    OverviewState& state() { return m_state; }
    
    // Render overlay
    void render(void* renderer, int screenWidth, int screenHeight);
    
private:
    void layoutGrid(int screenWidth, int screenHeight);
    void drawWorkspace(void* renderer, const WorkspacePreview& ws, bool selected);
    void drawBackground(int screenWidth, int screenHeight);
    
    OverviewState m_state;
    WorkspaceCallback m_workspaceCallback;
    WindowCallback m_windowCallback;
    
    bool m_initialized = false;
};

} // namespace havel
