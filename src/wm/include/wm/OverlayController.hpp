#pragma once

// Overlay controller extracted from Server (phase 3).
// Owns the three UI overlays (alt-tab, overview, app launcher) and their
// input/visibility control surface. Server forwards its public overlay
// methods here; the data it needs from the rest of the compositor
// (window list, focus switching, workspace switching, spawning) is
// injected as callbacks so this class has no Server dependency.

#include <wm/overlay/AltTabOverlay.hpp>
#include <wm/overlay/OverviewOverlay.hpp>
#include <wm/overlay/AppLauncherOverlay.hpp>
#include <functional>
#include <string>
#include <vector>

namespace havel {

class OverlayController {
public:
    // Window thumbnail description, filled by the collectWindows callback.
    struct WindowDesc {
        uint64_t id;
        std::string appId;
        std::string title;
        bool isFocused;
    };

    // Callbacks into the rest of the compositor (wired by Server).
    std::function<std::vector<WindowDesc>()> collectWindows;
    std::function<void(uint64_t)> focusWindow;
    std::function<void(uint32_t)> switchWorkspace;
    std::function<void(const std::string&)> spawnApp;

    // Alt-Tab
    void showAltTab(bool reverse = false);
    void hideAltTab() { m_altTab.hide(); }
    void altTabNext() { if (m_altTab.isVisible()) m_altTab.next(); }
    void altTabPrevious() { if (m_altTab.isVisible()) m_altTab.previous(); }
    void altTabSelect() { if (m_altTab.isVisible()) m_altTab.select(); }
    void altTabCancel() { if (m_altTab.isVisible()) m_altTab.cancel(); }
    bool isAltTabVisible() const { return m_altTab.isVisible(); }

    // Workspace overview
    void showOverview(uint32_t activeWorkspace, uint32_t workspaceCount);
    void hideOverview() { m_overview.hide(); }
    void overviewNavigate(int dx, int dy);
    void overviewSelect() { m_overview.select(); }
    bool isOverviewVisible() const { return m_overview.isVisible(); }

    // App launcher
    void showLauncher();
    void hideLauncher() { m_launcher.hide(); }
    void launcherInput(char key);
    void launcherBackspace();
    void launcherNavigate(int dy);
    void launcherSelect() { m_launcher.select(); }
    bool isLauncherVisible() const { return m_launcher.isVisible(); }

private:
    AltTabOverlay m_altTab;
    OverviewOverlay m_overview;
    AppLauncherOverlay m_launcher;
};

} // namespace havel
