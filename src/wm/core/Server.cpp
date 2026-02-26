#include <wm/Server.hpp>
#include <wm/Layout.hpp>
#include <Logger.h>
#include <algorithm>
#include <cstdlib>
#include <unistd.h>
#include <sys/types.h>
#include <cstdio>

namespace havel {

Server::Server() {
    LOG_DEBUG("Server constructor");
    // Initialize workspaces
    for (uint32_t i = 0; i < WORKSPACE_COUNT; ++i) {
        m_workspaces[i] = std::make_unique<Workspace>(i);
    }
}

Server::~Server() {
    LOG_INFO("Server destructor");
}

Workspace* Server::activeWorkspace() const {
    return m_workspaces[m_activeWorkspace].get();
}

void Server::setActiveWorkspace(uint32_t id) {
    if (id >= WORKSPACE_COUNT) return;

    LOG_INFO("Switching to workspace %u", id);

    // Hide all workspaces on primary output, show only the active one
    if (g_workspace_set_active) {
        g_workspace_set_active(id);
    }

    m_activeWorkspace = id;
    arrangeWorkspace(id);
}

void Server::workspaceStep(bool backwards) {
    uint32_t next = backwards
        ? (m_activeWorkspace + WORKSPACE_COUNT - 1) % WORKSPACE_COUNT
        : (m_activeWorkspace + 1) % WORKSPACE_COUNT;
    LOG_DEBUG("Workspace step: %u -> %u", m_activeWorkspace, next);
    setActiveWorkspace(next);
}

void Server::workspaceStepTo(uint32_t target) {
    if (target >= WORKSPACE_COUNT) return;
    LOG_DEBUG("Workspace step to: %u", target);
    setActiveWorkspace(target);
}

void Server::workspaceToggleTiling() {
    auto* ws = activeWorkspace();
    if (ws) {
        bool newState = !ws->isTilingEnabled();
        LOG_INFO("Tiling %s for workspace %u", newState ? "enabled" : "disabled", m_activeWorkspace);
        ws->setTilingEnabled(newState);
        arrangeWorkspace(m_activeWorkspace);
    }
}

View* Server::createXdgView(void* xdgSurface) {
    // Note: View is owned by C layer. We create it but C manages lifetime.
    // Return raw pointer - C will store and manage it.
    auto* view = new View();
    view->setWorkspaceId(m_activeWorkspace);
    view->setNativeHandle(xdgSurface);

    auto* ws = m_workspaces[m_activeWorkspace].get();
    if (ws) {
        ws->addView(view);
    }

    // Register with window manager for taskbar integration
    uint64_t windowId = m_windowManager.registerWindow(view);
    view->setWindowId(windowId);

    LOG_DEBUG("Created XDG view, workspace=%u windowId=%lu", m_activeWorkspace, windowId);
    return view;
}

View* Server::createXwaylandView(void* xwaylandSurface) {
    auto* view = new View();
    view->setWorkspaceId(m_activeWorkspace);
    view->setNativeHandle(xwaylandSurface);
    view->setFloating(true); // Xwayland defaults to floating

    auto* ws = m_workspaces[m_activeWorkspace].get();
    if (ws) {
        ws->addView(view);
    }

    // Register with window manager for taskbar integration
    uint64_t windowId = m_windowManager.registerWindow(view, "xwayland", "XWayland Window");
    view->setWindowId(windowId);

    LOG_DEBUG("Created XWayland view, workspace=%u windowId=%lu", m_activeWorkspace, windowId);
    return view;
}

void Server::onViewMapped(View* view) {
    if (!view) return;

    view->setMapped(true);
    LOG_INFO("View mapped, workspace=%u", view->workspaceId());

    // Update window manager with app_id and title from XDG surface
    // (Would be called from C layer when surface is ready)
    
    focusView(view);

    auto* ws = activeWorkspace();
    if (ws && ws->isTilingEnabled()) {
        arrangeWorkspace(view->workspaceId());
    }
}

void Server::onViewUnmapped(View* view) {
    if (!view) return;

    LOG_DEBUG("View unmapped");
    view->setMapped(false);

    auto* ws = m_workspaces[view->workspaceId()].get();
    if (ws && ws->activeView() == view) {
        ws->setActiveView(nullptr);
    }

    if (m_workspaces[view->workspaceId()]->isTilingEnabled()) {
        arrangeWorkspace(view->workspaceId());
    }
}

void Server::onViewDestroyed(View* view) {
    if (!view) return;

    LOG_INFO("View destroyed");

    // Unregister from window manager
    if (view->windowId() != 0) {
        m_windowManager.unregisterWindow(view->windowId());
    }

    // CRITICAL: Cancel animations for this view to prevent UAF
    m_animator.cancelAll();
    m_viewAnimState.erase(view);

    m_focusManager.remove(view);

    auto* ws = m_workspaces[view->workspaceId()].get();
    if (ws) {
        ws->removeView(view);
    }

    if (m_grab.view == view) {
        m_grab.view = nullptr;
        m_grab.button = 0;
    }
}

bool Server::handleKey(uint32_t keycode, bool pressed, uint32_t modifiers) {
    if (!pressed) return false;

    // Modifier masks (matching wlroots/xkbcommon)
    constexpr uint32_t MOD_ALT = 1 << 3;      // Mod1
    constexpr uint32_t MOD_LOGO = 1 << 6;     // Mod4
    constexpr uint32_t MOD_SHIFT = 1 << 0;    // Shift
    constexpr uint32_t MOD_CTRL = 1 << 2;     // Mod3/Control

    bool alt = (modifiers & MOD_ALT) != 0;
    bool meta = (modifiers & MOD_LOGO) != 0;
    bool shift = (modifiers & MOD_SHIFT) != 0;
    bool ctrl = (modifiers & MOD_CTRL) != 0;

    LOG_DEBUG("Key event: keycode=%u mod=%s%s%s%s", keycode,
                    alt ? "Alt+" : "",
                    meta ? "Meta+" : "",
                    shift ? "Shift+" : "",
                    ctrl ? "Ctrl+" : "");

    // ========================================================================
    // Ctrl+Meta combinations (highest priority)
    // ========================================================================
    
    // Ctrl+Meta+F4: Quit compositor
    if (ctrl && meta && keycode == 111) {  // F4
        LOG_INFO("Quit requested (Ctrl+Meta+F4)");
        quit();
        return true;
    }
    
    // Ctrl+Meta+Return: Show rofi
    if (ctrl && meta && keycode == 28) {  // Return
        LOG_INFO("Launching rofi (Ctrl+Meta+Return)");
        spawnRofi();
        return true;
    }

    // ========================================================================
    // Meta (Super/Windows key) combinations
    // ========================================================================
    
    if (meta) {
        // Meta+Tab or Meta+PgUp/PgDn: Workspace switch
        if (keycode == 23) {  // Tab
            LOG_INFO("Workspace switch (Meta+Tab)");
            workspaceStep(shift);
            return true;
        }
        if (keycode == 104) {  // PgUp
            LOG_INFO("Workspace switch (Meta+PgUp)");
            workspaceStep(false);
            return true;
        }
        if (keycode == 105) {  // PgDn
            LOG_INFO("Workspace switch (Meta+PgDn)");
            workspaceStep(true);
            return true;
        }
        
        // Meta+1/2/3/4/5/6/7/8/9/0: Direct workspace switch
        if (keycode >= 10 && keycode <= 19) {
            uint32_t ws = (keycode == 19) ? 9 : (keycode - 10);  // 0 = workspace 9
            LOG_INFO("Switch to workspace %u (Meta+%u)", ws, keycode - 9);
            workspaceStepTo(ws);
            return true;
        }
        
        // Meta+Shift+1/2/3/4/5/6/7/8/9/0: Move window to workspace
        if (shift && keycode >= 10 && keycode <= 19) {
            uint32_t ws = (keycode == 19) ? 9 : (keycode - 10);
            LOG_INFO("Move view to workspace %u (Meta+Shift+%u)", ws, keycode - 9);
            moveViewToWorkspace(ws);
            return true;
        }
        
        // Meta+y: Toggle tiling
        if (keycode == 21) {  // y
            LOG_INFO("Toggle tiling (Meta+y)");
            workspaceToggleTiling();
            return true;
        }

        // Meta+g: Toggle grayscale effect
        if (keycode == 34) {  // g
            LOG_INFO("Toggle grayscale (Meta+g)");
            toggleGrayscale();
            return true;
        }

        // Meta+n: Toggle negative/invert effect
        if (keycode == 39) {  // n
            LOG_INFO("Toggle negative (Meta+n)");
            toggleNegative();
            return true;
        }

        // Meta+Return: Spawn terminal (alacritty/foot)
        if (keycode == 28) {  // Return
            LOG_INFO("Spawn terminal (Meta+Return)");
            spawnTerminal();
            return true;
        }
        
        // Meta+h/l: Focus first/last view
        if (keycode == 35) {  // h
            LOG_DEBUG("Focus first view (Meta+h)");
            focusFirstLastView(true);
            return true;
        }
        if (keycode == 38) {  // l
            LOG_DEBUG("Focus last view (Meta+l)");
            focusFirstLastView(false);
            return true;
        }
        
        // Meta+b: Open default browser
        if (keycode == 30) {  // b
            LOG_INFO("Open browser (Meta+b)");
            spawnBrowser();
            return true;
        }
        
        // Meta+e: Open default file explorer
        if (keycode == 18) {  // e
            LOG_INFO("Open file explorer (Meta+e)");
            spawnFileManager();
            return true;
        }
        
        // Meta+q: Close window
        if (keycode == 16) {  // q
            LOG_INFO("Close window (Meta+q)");
            closeFocusedWindow();
            return true;
        }
        
        // Meta+m: Toggle maximize
        if (keycode == 31) {  // m
            LOG_INFO("Toggle maximize (Meta+m)");
            toggleMaximize();
            return true;
        }
        
        // Meta+j/k: Focus next/prev view
        if (keycode == 30) {  // j
            LOG_DEBUG("Focus next view (Meta+j)");
            focusNextMru(false);
            return true;
        }
        if (keycode == 31) {  // k
            LOG_DEBUG("Focus prev view (Meta+k)");
            focusNextMru(true);
            return true;
        }
        
        // Meta+space: Toggle floating
        if (keycode == 57) {  // space
            LOG_INFO("Toggle floating (Meta+space)");
            toggleFloating();
            return true;
        }
        
        // Meta+Insert: Minimize window
        if (keycode == 118) {  // Insert
            LOG_INFO("Minimize window (Meta+Insert)");
            minimizeWindow();
            return true;
        }
        
        // Meta+F: Toggle fullscreen
        if (keycode == 33) {  // F
            LOG_INFO("Toggle fullscreen (Meta+F)");
            toggleFullscreen();
            return true;
        }
        
        // Meta+A: Toggle always-on-top
        if (keycode == 30) {  // A
            LOG_INFO("Toggle always-on-top (Meta+A)");
            toggleAlwaysOnTop();
            return true;
        }
    }

    // ========================================================================
    // Alt combinations
    // ========================================================================
    
    if (alt) {
        // Alt+Tab: Show overlay or navigate
        if (keycode == 23) {  // Tab
            if (isAltTabVisible()) {
                altTabNext();
            } else {
                showAltTab(shift);  // Shift reverses direction
            }
            return true;
        }

        // Alt+PgUp/PgDn: Move window to next/previous workspace
        if (keycode == 104) {  // PgUp
            LOG_INFO("Move view to prev workspace (Alt+PgUp)");
            moveViewToWorkspaceRelative(false);
            return true;
        }
        if (keycode == 105) {  // PgDn
            LOG_INFO("Move view to next workspace (Alt+PgDn)");
            moveViewToWorkspaceRelative(true);
            return true;
        }

        // Alt+Return: Spawn terminal (fallback)
        if (keycode == 28) {  // Return
            LOG_INFO("Spawn terminal (Alt+Return)");
            spawnTerminal();
            return true;
        }
        
        // Alt+F4: Close window (standard WM binding)
        if (keycode == 111) {  // F4
            LOG_INFO("Close window (Alt+F4)");
            closeFocusedWindow();
            return true;
        }
    }
    
    // Key not consumed by compositor, forward to client
    return false;
}

void Server::handlePointerButton(uint32_t button, bool pressed, double x, double y) {
    if (!pressed) {
        if (m_grab.button == button) {
            LOG_DEBUG("Pointer grab released");
            m_grab.view = nullptr;
            m_grab.button = 0;
        }
        return;
    }
    
    LOG_DEBUG("Pointer button press: button=%u x=%.0f y=%.0f", button, x, y);
    
    // For now, just track the grab start
    m_grab.startX = x;
    m_grab.startY = y;
}

void Server::handlePointerMotion(double x, double y) {
    if (!m_grab.view) return;
    
    double dx = x - m_grab.startX;
    double dy = y - m_grab.startY;
    
    if (m_grab.button == 0x110) { // BTN_LEFT - move
        int newX = m_grab.startViewX + (int)dx;
        int newY = m_grab.startViewY + (int)dy;
        setViewPosition(m_grab.view, newX, newY, true);
        
        // Update floating geometry if not tiling
        auto* ws = m_workspaces[m_grab.view->workspaceId()].get();
        if (ws && !ws->isTilingEnabled()) {
            Rect fg = m_grab.view->floatGeom();
            fg.x = newX;
            fg.y = newY;
            m_grab.view->setFloatGeom(fg);
        }
    } else if (m_grab.button == 0x111) { // BTN_RIGHT - resize
        int newW = m_grab.startViewW + (int)dx;
        int newH = m_grab.startViewH + (int)dy;
        if (newW < 50) newW = 50;
        if (newH < 50) newH = 50;
        setViewSize(m_grab.view, newW, newH, true);
        
        // Update floating geometry if not tiling
        auto* ws = m_workspaces[m_grab.view->workspaceId()].get();
        if (ws && !ws->isTilingEnabled()) {
            Rect fg = m_grab.view->floatGeom();
            fg.w = newW;
            fg.h = newH;
            m_grab.view->setFloatGeom(fg);
        }
    }
}

void Server::focusView(View* view) {
    if (!view) return;

    m_focusManager.promote(view);

    auto* ws = m_workspaces[view->workspaceId()].get();
    if (ws) {
        ws->setActiveView(view);
    }

    // Update window manager focus state
    if (view->windowId() != 0) {
        m_windowManager.focusWindow(view->windowId());
    }

    // Raise and focus the view
    raiseView(view);
    focusViewNative(view);
}

void Server::focusNextMru(bool backwards) {
    auto current = activeWorkspace()->activeView();
    auto next = m_focusManager.nextMru(current, backwards);
    if (next) {
        LOG_DEBUG("Focus MRU view");
        focusView(next);
    }
}

void Server::arrangeWorkspace(uint32_t id) {
    if (id >= WORKSPACE_COUNT) return;

    auto* ws = m_workspaces[id].get();
    if (!ws || !ws->isTilingEnabled()) return;

    // Use tiledViews() to exclude floating windows from tiling layout
    auto views = ws->tiledViews();
    if (views.empty()) return;

    Rect geom = outputGeometry(id);
    if (!geom.isValid()) return;

    LOG_DEBUG("Arranging workspace %u with %zu tiled views", id, views.size());

    Layout::arrangeMasterStack(views, geom);

    // Apply the layout by setting positions/sizes
    for (auto* view : views) {
        Rect vGeom = view->geom();
        setViewPosition(view, vGeom.x, vGeom.y, true);
        setViewSize(view, vGeom.w, vGeom.h, true);
    }
}

void Server::setOutputGeometry(uint32_t workspaceId, const Rect& geom) {
    if (workspaceId < WORKSPACE_COUNT) {
        m_outputGeoms[workspaceId] = geom;
    }
}

Rect Server::outputGeometry(uint32_t workspaceId) const {
    if (workspaceId < WORKSPACE_COUNT) {
        auto it = m_outputGeoms.find(workspaceId);
        if (it != m_outputGeoms.end()) {
            return it->second;
        }
    }
    return {};
}

// ============================================================================
// Action helpers
// ============================================================================

void Server::spawnTerminal() {
    const char* terminals[] = {"alacritty", "foot", nullptr};

    for (int i = 0; terminals[i] != nullptr; i++) {
        // Check if terminal exists
        char checkCmd[256];
        snprintf(checkCmd, sizeof(checkCmd), "command -v %s", terminals[i]);
        FILE* f = popen(checkCmd, "r");
        if (f) {
            char path[256];
            if (fgets(path, sizeof(path), f) != nullptr) {
                pclose(f);
                // Terminal found, spawn it
                LOG_INFO("Spawning terminal: %s", terminals[i]);
                if (g_server_spawn) {
                    g_server_spawn(terminals[i]);
                } else {
                    // Fallback to fork/exec
                    pid_t pid = fork();
                    if (pid == 0) {
                        execlp(terminals[i], terminals[i], (char*)NULL);
                        _exit(127);
                    }
                }
                return;
            }
            pclose(f);
        }
    }

    // Fallback to foot
    LOG_INFO("Spawning fallback terminal: foot");
    if (g_server_spawn) {
        g_server_spawn("foot");
    } else {
        pid_t pid = fork();
        if (pid == 0) {
            execlp("foot", "foot", (char*)NULL);
            _exit(127);
        }
    }
}

void Server::spawnRofi() {
    if (g_server_spawn) {
        g_server_spawn("rofi -show drun");
    }
}

void Server::spawnBrowser() {
    const char* browsers[] = {"firefox", "chromium", "google-chrome", "brave", nullptr};
    
    for (int i = 0; browsers[i] != nullptr; i++) {
        char cmd[256];
        snprintf(cmd, sizeof(cmd), "command -v %s > /dev/null 2>&1", browsers[i]);
        FILE* f = popen(cmd, "r");
        if (f) {
            int result = pclose(f);
            if (result == 0 && g_server_spawn) {
                g_server_spawn(browsers[i]);
                LOG_INFO("Spawned browser: %s", browsers[i]);
                return;
            }
        }
    }
}

void Server::spawnFileManager() {
    const char* managers[] = {"nautilus", "dolphin", "thunar", "pcmanfm", nullptr};
    
    for (int i = 0; managers[i] != nullptr; i++) {
        char cmd[256];
        snprintf(cmd, sizeof(cmd), "command -v %s > /dev/null 2>&1", managers[i]);
        FILE* f = popen(cmd, "r");
        if (f) {
            int result = pclose(f);
            if (result == 0 && g_server_spawn) {
                g_server_spawn(managers[i]);
                LOG_INFO("Spawned file manager: %s", managers[i]);
                return;
            }
        }
    }
}

void Server::closeFocusedWindow() {
    auto* ws = activeWorkspace();
    if (ws && ws->activeView()) {
        View* view = ws->activeView();
        LOG_INFO("Closing focused window");
        // Send close request through wlroots callback
        if (g_view_close) {
            g_view_close(view->nativeHandle());
        }
    }
}

void Server::toggleMaximize() {
    auto* ws = activeWorkspace();
    if (ws && ws->activeView()) {
        View* view = ws->activeView();
        
        // Check if already maximized (compare with output geometry)
        Rect outputGeom = outputGeometry(m_activeWorkspace);
        Rect viewGeom = getViewGeometry(view);
        
        bool isMaximized = (viewGeom.x == outputGeom.x && 
                           viewGeom.y == outputGeom.y &&
                           viewGeom.w == outputGeom.w && 
                           viewGeom.h == outputGeom.h);
        
        if (isMaximized) {
            // Restore to floating geometry if available
            if (view->hasFloatGeom()) {
                Rect fg = view->floatGeom();
                setViewPosition(view, fg.x, fg.y, false);
                setViewSize(view, fg.w, fg.h, false);
                LOG_INFO("Restore window from maximize");
            }
        } else {
            // Store current geometry before maximizing
            if (!view->hasFloatGeom()) {
                view->setFloatGeom(viewGeom);
            }
            // Maximize to fill output
            setViewPosition(view, outputGeom.x, outputGeom.y, false);
            setViewSize(view, outputGeom.w, outputGeom.h, false);
            LOG_INFO("Maximize window");
        }
    }
}

void Server::toggleFloating() {
    auto* ws = activeWorkspace();
    if (ws && ws->activeView()) {
        View* view = ws->activeView();
        view->setFloating(!view->isFloating());
        LOG_INFO("View floating state: %s", view->isFloating() ? "on" : "off");
        if (view->isFloating() && !view->hasFloatGeom()) {
            Rect geom = getViewGeometry(view);
            view->setFloatGeom(geom);
        }
    }
}

void Server::minimizeWindow() {
    auto* ws = activeWorkspace();
    if (ws && ws->activeView()) {
        View* view = ws->activeView();
        LOG_INFO("Minimize window");
        
        // Hide the view in scene graph (proper minimize, not unmap)
        if (g_view_minimize) {
            g_view_minimize(view->nativeHandle());
        }
        
        // Focus next available view
        focusNextMru(false);
    }
}

void Server::toggleFullscreen() {
    auto* ws = activeWorkspace();
    if (ws && ws->activeView()) {
        View* view = ws->activeView();
        LOG_INFO("Toggle fullscreen");
        // Toggle fullscreen through wlroots callback
        // Store state to track current fullscreen status
        static View* lastFullscreenView = nullptr;
        
        if (lastFullscreenView == view) {
            // Exit fullscreen
            if (g_view_set_fullscreen) {
                g_view_set_fullscreen(view->nativeHandle(), false);
            }
            lastFullscreenView = nullptr;
        } else {
            // Enter fullscreen
            if (lastFullscreenView && g_view_set_fullscreen) {
                g_view_set_fullscreen(lastFullscreenView->nativeHandle(), false);
            }
            if (g_view_set_fullscreen) {
                g_view_set_fullscreen(view->nativeHandle(), true);
            }
            lastFullscreenView = view;
        }
    }
}

void Server::toggleAlwaysOnTop() {
    auto* ws = activeWorkspace();
    if (ws && ws->activeView()) {
        View* view = ws->activeView();
        LOG_INFO("Toggle always-on-top");
        // Raise the view to top
        raiseView(view);
        // In a full implementation, this would set a persistent "on top" flag
        // and ensure the view stays on top even when other windows are focused
    }
}

void Server::focusFirstLastView(bool first) {
    auto* ws = activeWorkspace();
    if (ws) {
        auto views = ws->mappedViews();
        if (!views.empty()) {
            if (first) {
                focusView(views.front());
            } else {
                focusView(views.back());
            }
        }
    }
}

void Server::moveViewToWorkspace(uint32_t ws) {
    auto* currentWs = activeWorkspace();
    if (currentWs && currentWs->activeView()) {
        View* view = currentWs->activeView();
        // Remove from current workspace
        currentWs->removeView(view);
        view->setWorkspaceId(ws);
        // Add to target workspace
        auto* targetWs = m_workspaces[ws].get();
        if (targetWs) {
            targetWs->addView(view);
            LOG_INFO("Moved view to workspace %u", ws);
            arrangeWorkspace(ws);
        }
    }
}

void Server::moveViewToWorkspaceRelative(bool next) {
    auto* ws = activeWorkspace();
    if (ws && ws->activeView()) {
        uint32_t target = next 
            ? (m_activeWorkspace + 1) % WORKSPACE_COUNT
            : (m_activeWorkspace + WORKSPACE_COUNT - 1) % WORKSPACE_COUNT;
        moveViewToWorkspace(target);
    }
}

void Server::quit() {
    if (g_server_quit) {
        g_server_quit();
    }
}

// ============================================================================
// Animation Control
// ============================================================================

void Server::setAnimationsEnabled(bool enabled) {
    m_animator.setEnabled(enabled);
    LOG_INFO("Animations %s", enabled ? "enabled" : "disabled");
}

void Server::updateAnimations() {
    m_animator.update();
    m_animator.cleanup();
}

// ============================================================================
// Effect Control (Global)
// ============================================================================

void Server::setGrayscaleEnabled(bool enabled) {
    // Would apply to all outputs in multi-monitor setup
    printf("[Server] Grayscale %s\n", enabled ? "enabled" : "disabled");
}

void Server::setNegativeEnabled(bool enabled) {
    printf("[Server] Negative %s\n", enabled ? "enabled" : "disabled");
}

bool Server::isGrayscaleEnabled() const {
    return false;  // Would track state
}

bool Server::isNegativeEnabled() const {
    return false;  // Would track state
}

void Server::toggleGrayscale() {
    setGrayscaleEnabled(!isGrayscaleEnabled());
}

void Server::toggleNegative() {
    setNegativeEnabled(!isNegativeEnabled());
}

// ============================================================================
// Alt-Tab Overlay
// ============================================================================

void Server::showAltTab(bool reverse) {
    // Get all windows from window manager
    auto windows = m_windowManager.getAllWindows();
    
    if (windows.empty()) {
        printf("[AltTab] No windows to show\n");
        return;
    }
    
    // Convert to thumbnails
    std::vector<WindowThumbnail> thumbnails;
    for (const auto& win : windows) {
        WindowThumbnail thumb;
        thumb.windowId = win.id;
        thumb.appId = win.appId;
        thumb.title = win.title;
        thumb.isFocused = hasFlag(win.flags, WindowFlags::Focused);
        thumbnails.push_back(thumb);
    }
    
    m_altTabOverlay.show(thumbnails);
    m_altTabOverlay.setSelectCallback([this](uint64_t windowId) {
        // Focus the selected window
        // Would need to find view by windowId and call focusView()
        printf("[AltTab] Would focus window %lu\n", windowId);
    });
    
    if (reverse) {
        altTabPrevious();
    }
}

void Server::hideAltTab() {
    m_altTabOverlay.hide();
}

void Server::altTabNext() {
    if (m_altTabOverlay.isVisible()) {
        m_altTabOverlay.next();
    }
}

void Server::altTabPrevious() {
    if (m_altTabOverlay.isVisible()) {
        m_altTabOverlay.previous();
    }
}

void Server::altTabSelect() {
    if (m_altTabOverlay.isVisible()) {
        m_altTabOverlay.select();
    }
}

void Server::altTabCancel() {
    if (m_altTabOverlay.isVisible()) {
        m_altTabOverlay.cancel();
    }
}

bool Server::isAltTabVisible() const {
    return m_altTabOverlay.isVisible();
}

// ============================================================================
// View Manipulation with Animation
// ============================================================================

void Server::setViewPosition(View* view, int x, int y, bool animate) {
    if (!view) return;
    
    // Initialize animation state if needed
    auto& state = m_viewAnimState[view];
    
    if (animate && m_animator.isEnabled()) {
        // Check if we have a valid starting position
        if (state.currentX == 0 && state.currentY == 0 && 
            view->nativeHandle() != nullptr) {
            // Get current position from C layer
            Rect geo = getViewGeometry(view);
            state.currentX = geo.x;
            state.currentY = geo.y;
        }
        
        int fromX = state.currentX;
        int fromY = state.currentY;
        
        // Only animate if position actually changed
        if (fromX != x || fromY != y) {
            animateViewMove(view, fromX, fromY, x, y);
        } else {
            // Just update state
            state.currentX = x;
            state.currentY = y;
        }
    } else {
        state.currentX = x;
        state.currentY = y;
        if (g_view_set_position) {
            g_view_set_position(view->nativeHandle(), x, y);
        }
    }
}

void Server::setViewSize(View* view, int w, int h, bool animate) {
    if (!view) return;
    
    auto& state = m_viewAnimState[view];
    
    if (animate && m_animator.isEnabled()) {
        // Initialize from current if needed
        if (state.currentW == 0 && state.currentH == 0 && 
            view->nativeHandle() != nullptr) {
            Rect geo = getViewGeometry(view);
            state.currentW = geo.w;
            state.currentH = geo.h;
        }
        
        int fromW = state.currentW;
        int fromH = state.currentH;
        
        if (fromW != w || fromH != h) {
            animateViewResize(view, fromW, fromH, w, h);
        } else {
            state.currentW = w;
            state.currentH = h;
        }
    } else {
        state.currentW = w;
        state.currentH = h;
        if (g_view_set_size) {
            g_view_set_size(view->nativeHandle(), w, h);
        }
    }
}

void Server::focusViewNative(View* view) {
    if (!view || !g_view_focus) return;
    g_view_focus(view->nativeHandle());
}

void Server::raiseView(View* view) {
    if (!view || !g_view_raise) return;
    g_view_raise(view->nativeHandle());
}

Rect Server::getViewGeometry(View* view) {
    Rect result;
    if (g_view_get_geometry) {
        g_view_get_geometry(view->nativeHandle(), &result.x, &result.y, &result.w, &result.h);
    }
    return result;
}

// ============================================================================
// Animation Helpers
// ============================================================================

void Server::animateViewFade(View* view, float from, float to) {
    auto& state = m_viewAnimState[view];
    
    m_animator.fade(from, to,
        [this, view, &state](float alpha) {
            state.currentAlpha = alpha;
            // Fade would control opacity via wlroots surface alpha
            // Placeholder for future compositor-level opacity support
        });
}

void Server::animateViewMove(View* view, int fromX, int fromY, int toX, int toY) {
    auto& state = m_viewAnimState[view];
    
    m_animator.move(fromX, fromY, toX, toY, 
        [this, view, &state](int x, int y) {
            state.currentX = x;
            state.currentY = y;
            if (g_view_set_position) {
                g_view_set_position(view->nativeHandle(), x, y);
            }
        });
}

void Server::animateViewResize(View* view, int fromW, int fromH, int toW, int toH) {
    auto& state = m_viewAnimState[view];
    
    m_animator.resize(fromW, fromH, toW, toH,
        [this, view, &state](int w, int h) {
            state.currentW = w;
            state.currentH = h;
            if (g_view_set_size) {
                g_view_set_size(view->nativeHandle(), w, h);
            }
        });
}

void Server::animateViewScale(View* view, float from, float to) {
    auto& state = m_viewAnimState[view];
    
    m_animator.scale(from, to,
        [this, view, &state](float scale) {
            state.currentScale = scale;
            // Scale would be applied via surface transformation
            // Placeholder for future implementation
        });
}

} // namespace havel
