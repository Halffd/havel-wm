// OverlayController implementation (extracted from Server, phase 3).
// Server wires the collectWindows/focusWindow/switchWorkspace/spawnApp
// callbacks; this class owns overlay state machine + input routing.

#include <wm/OverlayController.hpp>
#include <Logger.h>

namespace havel {

void OverlayController::showAltTab(bool reverse) {
    if (!collectWindows) return;

    auto windows = collectWindows();
    if (windows.empty()) {
        LOG_DEBUG("[AltTab] No windows to show");
        return;
    }

    // Convert to thumbnails
    std::vector<WindowThumbnail> thumbnails;
    thumbnails.reserve(windows.size());
    for (const auto& win : windows) {
        WindowThumbnail thumb;
        thumb.windowId = win.id;
        thumb.appId = win.appId;
        thumb.title = win.title;
        thumb.isFocused = win.isFocused;
        thumbnails.push_back(std::move(thumb));
    }

    m_altTab.show(thumbnails);
    m_altTab.setSelectCallback([this](uint64_t windowId) {
        if (focusWindow) focusWindow(windowId);
    });

    if (reverse) {
        m_altTab.previous();
    }
}

void OverlayController::showOverview(uint32_t activeWorkspace, uint32_t workspaceCount) {
    m_overview.show(static_cast<int>(workspaceCount), static_cast<int>(activeWorkspace));
    m_overview.setWorkspaceCallback([this](uint32_t wsId) {
        if (switchWorkspace) switchWorkspace(wsId);
    });
}

void OverlayController::overviewNavigate(int dx, int dy) {
    if (dx < 0) m_overview.navigateLeft();
    if (dx > 0) m_overview.navigateRight();
    if (dy < 0) m_overview.navigateUp();
    if (dy > 0) m_overview.navigateDown();
}

void OverlayController::showLauncher() {
    m_launcher.scanApplications();
    m_launcher.show();
    m_launcher.setLaunchCallback([this](const std::string& exec) {
        if (spawnApp) spawnApp(exec);
    });
}

void OverlayController::launcherInput(char key) {
    if (m_launcher.isVisible()) {
        std::string current = m_launcher.searchText();
        current += key;
        m_launcher.setSearchText(current);
    }
}

void OverlayController::launcherBackspace() {
    m_launcher.backspace();
}

void OverlayController::launcherNavigate(int dy) {
    if (dy < 0) m_launcher.navigateUp();
    if (dy > 0) m_launcher.navigateDown();
}

} // namespace havel
