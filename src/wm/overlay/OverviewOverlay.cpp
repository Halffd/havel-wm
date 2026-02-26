#include "OverviewOverlay.hpp"
#include <cstdio>
#include <cmath>

namespace havel {

OverviewOverlay::OverviewOverlay() = default;

OverviewOverlay::~OverviewOverlay() {
    hide();
}

void OverviewOverlay::show(int numWorkspaces, int activeWorkspace) {
    if (numWorkspaces <= 0) {
        printf("[Overview] No workspaces to show\n");
        return;
    }
    
    m_state.workspaces.clear();
    
    // Create workspace previews
    for (int i = 0; i < numWorkspaces; i++) {
        WorkspacePreview preview;
        preview.workspaceId = i;
        preview.isActive = (i == activeWorkspace);
        preview.windowCount = 0;  // Would be populated from window manager
        m_state.workspaces.push_back(preview);
    }
    
    m_state.visible = true;
    m_state.selectedWorkspace = activeWorkspace;
    m_state.hoveredWindow = -1;
    
    printf("[Overview] Showing %d workspaces\n", numWorkspaces);
}

void OverviewOverlay::hide() {
    if (m_state.visible) {
        m_state.visible = false;
        m_state.workspaces.clear();
        m_state.selectedWorkspace = -1;
        printf("[Overview] Hidden\n");
    }
}

void OverviewOverlay::toggle(int numWorkspaces, int activeWorkspace) {
    if (m_state.visible) {
        hide();
    } else {
        show(numWorkspaces, activeWorkspace);
    }
}

void OverviewOverlay::navigateUp() {
    if (!m_state.visible) return;
    
    if (m_state.selectedWorkspace >= m_state.gridCols) {
        m_state.selectedWorkspace -= m_state.gridCols;
    }
    printf("[Overview] Selected workspace %d\n", m_state.selectedWorkspace + 1);
}

void OverviewOverlay::navigateDown() {
    if (!m_state.visible) return;
    
    if (m_state.selectedWorkspace + m_state.gridCols < static_cast<int>(m_state.workspaces.size())) {
        m_state.selectedWorkspace += m_state.gridCols;
    }
    printf("[Overview] Selected workspace %d\n", m_state.selectedWorkspace + 1);
}

void OverviewOverlay::navigateLeft() {
    if (!m_state.visible) return;
    
    if (m_state.selectedWorkspace > 0) {
        m_state.selectedWorkspace--;
    }
    printf("[Overview] Selected workspace %d\n", m_state.selectedWorkspace + 1);
}

void OverviewOverlay::navigateRight() {
    if (!m_state.visible) return;
    
    if (m_state.selectedWorkspace + 1 < static_cast<int>(m_state.workspaces.size())) {
        m_state.selectedWorkspace++;
    }
    printf("[Overview] Selected workspace %d\n", m_state.selectedWorkspace + 1);
}

void OverviewOverlay::select() {
    if (!m_state.visible || m_state.selectedWorkspace < 0) return;
    
    uint32_t selectedId = m_state.workspaces[m_state.selectedWorkspace].workspaceId;
    printf("[Overview] Selected workspace %u\n", selectedId);
    
    hide();
    
    if (m_workspaceCallback) {
        m_workspaceCallback(selectedId);
    }
}

void OverviewOverlay::cancel() {
    printf("[Overview] Cancelled\n");
    hide();
}

void OverviewOverlay::handleMouseMove(int x, int y) {
    if (!m_state.visible) return;
    
    // Would check if mouse is over a workspace/window preview
    (void)x;
    (void)y;
}

void OverviewOverlay::handleMouseClick(int x, int y) {
    if (!m_state.visible) return;
    
    // Would check which workspace/window was clicked
    (void)x;
    (void)y;
}

void OverviewOverlay::render(void* renderer, int screenWidth, int screenHeight) {
    if (!m_state.visible || !renderer) return;
    
    // Layout grid
    layoutGrid(screenWidth, screenHeight);
    
    // Draw semi-transparent background
    drawBackground(screenWidth, screenHeight);
    
    // Draw workspace previews
    for (const auto& ws : m_state.workspaces) {
        bool selected = (ws.workspaceId == static_cast<uint32_t>(m_state.selectedWorkspace));
        drawWorkspace(renderer, ws, selected);
    }
}

void OverviewOverlay::layoutGrid(int screenWidth, int screenHeight) {
    int numWorkspaces = m_state.workspaces.size();
    
    // Calculate grid dimensions
    m_state.gridCols = static_cast<int>(std::ceil(std::sqrt(numWorkspaces)));
    m_state.gridRows = static_cast<int>(std::ceil(static_cast<float>(numWorkspaces) / m_state.gridCols));
    
    // Calculate preview size
    int availableWidth = screenWidth - 2 * m_state.padding;
    int availableHeight = screenHeight - 2 * m_state.padding;
    
    int totalHSpacing = (m_state.gridCols > 1) ? (m_state.gridCols - 1) * m_state.spacing : 0;
    int totalVSpacing = (m_state.gridRows > 1) ? (m_state.gridRows - 1) * m_state.spacing : 0;
    
    int previewWidth = (availableWidth - totalHSpacing) / m_state.gridCols;
    int previewHeight = (availableHeight - totalVSpacing) / m_state.gridRows;
    
    // Clamp preview size
    previewWidth = std::min(previewWidth, 400);
    previewHeight = std::min(previewHeight, 300);
    
    // Position each workspace preview
    for (int i = 0; i < numWorkspaces; i++) {
        int col = i % m_state.gridCols;
        int row = i / m_state.gridCols;
        
        WorkspacePreview& ws = m_state.workspaces[i];
        ws.x = m_state.padding + col * (previewWidth + m_state.spacing);
        ws.y = m_state.padding + row * (previewHeight + m_state.spacing);
        ws.w = previewWidth;
        ws.h = previewHeight;
    }
}

void OverviewOverlay::drawWorkspace(void* renderer, const WorkspacePreview& ws, bool selected) {
    // Would draw:
    // 1. Workspace background (highlighted if selected/active)
    // 2. Workspace number label
    // 3. Window thumbnails within workspace
    // 4. Window count indicator
    
    (void)renderer;
    (void)ws;
    (void)selected;
    
    // Stub for now - actual rendering requires GLES2
}

void OverviewOverlay::drawBackground(int screenWidth, int screenHeight) {
    // Would draw semi-transparent dark overlay
    // For now, this is a stub
    (void)screenWidth;
    (void)screenHeight;
}

} // namespace havel
