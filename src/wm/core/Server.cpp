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

std::shared_ptr<View> Server::createXdgView(void* xdgSurface) {
    auto view = std::make_shared<View>();
    view->setWorkspaceId(m_activeWorkspace);
    view->setNativeHandle(xdgSurface);
    
    auto* ws = m_workspaces[m_activeWorkspace].get();
    if (ws) {
        ws->addView(view);
    }
    
    LOG_DEBUG("Created XDG view, workspace=%u", m_activeWorkspace);
    return view;
}

std::shared_ptr<View> Server::createXwaylandView(void* xwaylandSurface) {
    auto view = std::make_shared<View>();
    view->setWorkspaceId(m_activeWorkspace);
    view->setNativeHandle(xwaylandSurface);
    view->setFloating(true); // Xwayland defaults to floating
    
    auto* ws = m_workspaces[m_activeWorkspace].get();
    if (ws) {
        ws->addView(view);
    }
    
    LOG_DEBUG("Created XWayland view, workspace=%u", m_activeWorkspace);
    return view;
}

void Server::onViewMapped(View* view) {
    if (!view) return;
    
    view->setMapped(true);
    LOG_INFO("View mapped, workspace=%u", view->workspaceId());
    
    focusView(std::shared_ptr<View>(view, [](View*){})); // Non-owning promote
    
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

void Server::handleKey(uint32_t keycode, bool pressed, uint32_t modifiers) {
    if (!pressed) return;
    
    // Modifier masks (matching wlroots/xkbcommon)
    constexpr uint32_t MOD_ALT = 1 << 3;      // Mod1
    constexpr uint32_t MOD_LOGO = 1 << 6;     // Mod4
    constexpr uint32_t MOD_SHIFT = 1 << 0;    // Shift
    constexpr uint32_t MOD_CTRL = 1 << 2;     // Mod3/Control
    
    bool alt = (modifiers & MOD_ALT) != 0;
    bool meta = (modifiers & MOD_LOGO) != 0;
    bool shift = (modifiers & MOD_SHIFT) != 0;
    bool ctrl = (modifiers & MOD_CTRL) != 0;
    
    LOG_DEBUG("Key event: keycode={} mod={}{}{}{}", keycode, 
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
        return;
    }
    
    // Ctrl+Meta+Return: Show rofi
    if (ctrl && meta && keycode == 28) {  // Return
        LOG_INFO("Launching rofi (Ctrl+Meta+Return)");
        spawnRofi();
        return;
    }

    // ========================================================================
    // Meta (Super/Windows key) combinations
    // ========================================================================
    
    if (meta) {
        // Meta+Tab or Meta+PgUp/PgDn: Workspace switch
        if (keycode == 23) {  // Tab
            LOG_INFO("Workspace switch (Meta+Tab)");
            workspaceStep(shift);
            return;
        }
        if (keycode == 104) {  // PgUp
            LOG_INFO("Workspace switch (Meta+PgUp)");
            workspaceStep(false);
            return;
        }
        if (keycode == 105) {  // PgDn
            LOG_INFO("Workspace switch (Meta+PgDn)");
            workspaceStep(true);
            return;
        }
        
        // Meta+1/2/3/4/5/6/7/8/9/0: Direct workspace switch
        if (keycode >= 10 && keycode <= 19) {
            uint32_t ws = (keycode == 19) ? 9 : (keycode - 10);  // 0 = workspace 9
            LOG_INFO("Switch to workspace %u (Meta+%u)", ws, keycode - 9);
            workspaceStepTo(ws);
            return;
        }
        
        // Meta+Shift+1/2/3/4/5/6/7/8/9/0: Move window to workspace
        if (shift && keycode >= 10 && keycode <= 19) {
            uint32_t ws = (keycode == 19) ? 9 : (keycode - 10);
            LOG_INFO("Move view to workspace %u (Meta+Shift+%u)", ws, keycode - 9);
            moveViewToWorkspace(ws);
            return;
        }
        
        // Meta+y: Toggle tiling
        if (keycode == 21) {  // y
            LOG_INFO("Toggle tiling (Meta+y)");
            workspaceToggleTiling();
            return;
        }
        
        // Meta+Return: Spawn terminal (alacritty/foot)
        if (keycode == 28) {  // Return
            LOG_INFO("Spawn terminal (Meta+Return)");
            spawnTerminal();
            return;
        }
        
        // Meta+h/l: Focus first/last view
        if (keycode == 35) {  // h
            LOG_DEBUG("Focus first view (Meta+h)");
            focusFirstLastView(true);
            return;
        }
        if (keycode == 38) {  // l
            LOG_DEBUG("Focus last view (Meta+l)");
            focusFirstLastView(false);
            return;
        }
        
        // Meta+b: Open default browser
        if (keycode == 30) {  // b
            LOG_INFO("Open browser (Meta+b)");
            spawnBrowser();
            return;
        }
        
        // Meta+e: Open default file explorer
        if (keycode == 18) {  // e
            LOG_INFO("Open file explorer (Meta+e)");
            spawnFileManager();
            return;
        }
        
        // Meta+q: Close window
        if (keycode == 16) {  // q
            LOG_INFO("Close window (Meta+q)");
            closeFocusedWindow();
            return;
        }
        
        // Meta+m: Toggle maximize
        if (keycode == 31) {  // m
            LOG_INFO("Toggle maximize (Meta+m)");
            toggleMaximize();
            return;
        }
        
        // Meta+j/k: Focus next/prev view
        if (keycode == 30) {  // j
            LOG_DEBUG("Focus next view (Meta+j)");
            focusNextMru(false);
            return;
        }
        if (keycode == 31) {  // k
            LOG_DEBUG("Focus prev view (Meta+k)");
            focusNextMru(true);
            return;
        }
        
        // Meta+space: Toggle floating
        if (keycode == 57) {  // space
            LOG_INFO("Toggle floating (Meta+space)");
            toggleFloating();
            return;
        }
    }

    // ========================================================================
    // Alt combinations
    // ========================================================================
    
    if (alt) {
        // Alt+Tab: Focus MRU switch
        if (keycode == 23) {  // Tab
            LOG_DEBUG("Focus MRU switch (Alt+Tab)");
            focusNextMru(shift);
            return;
        }
        
        // Alt+PgUp/PgDn: Move window to next/previous workspace
        if (keycode == 104) {  // PgUp
            LOG_INFO("Move view to prev workspace (Alt+PgUp)");
            moveViewToWorkspaceRelative(false);
            return;
        }
        if (keycode == 105) {  // PgDn
            LOG_INFO("Move view to next workspace (Alt+PgDn)");
            moveViewToWorkspaceRelative(true);
            return;
        }
        
        // Alt+Return: Spawn terminal (fallback)
        if (keycode == 28) {  // Return
            LOG_INFO("Spawn terminal (Alt+Return)");
            spawnTerminal();
            return;
        }
    }
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

void Server::focusView(std::shared_ptr<View> view) {
    if (!view) return;
    
    m_focusManager.promote(view);
    
    auto* ws = m_workspaces[view->workspaceId()].get();
    if (ws) {
        ws->setActiveView(view.get());
    }
    
    // Raise and focus the view
    raiseView(view.get());
    focusViewNative(view.get());
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
    
    auto views = ws->mappedViews();
    if (views.empty()) return;
    
    Rect geom = outputGeometry(id);
    if (!geom.isValid()) return;
    
    LOG_DEBUG("Arranging workspace %u with %zu views", id, views.size());
    
    Layout::arrangeMasterStack(views, geom);
    
    // Apply the layout by setting positions/sizes
    for (const auto& view : views) {
        Rect vGeom = view->geom();
        setViewPosition(view.get(), vGeom.x, vGeom.y, true);
        setViewSize(view.get(), vGeom.w, vGeom.h, true);
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
        char cmd[256];
        snprintf(cmd, sizeof(cmd), "command -v %s > /dev/null 2>&1 && exec %s", terminals[i], terminals[i]);
        FILE* f = popen(cmd, "r");
        if (f) {
            pclose(f);
            if (g_server_spawn) {
                g_server_spawn(terminals[i]);
                LOG_INFO("Spawned terminal: %s", terminals[i]);
            }
            return;
        }
    }
    
    // Fallback
    if (g_server_spawn) {
        g_server_spawn("foot");
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
        // Send close request through wlroots
        // This would need a callback to C layer
    }
}

void Server::toggleMaximize() {
    auto* ws = activeWorkspace();
    if (ws && ws->activeView()) {
        View* view = ws->activeView();
        LOG_INFO("Toggle maximize");
        // Toggle maximize state
        // Would need implementation
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
            targetWs->addView(std::shared_ptr<View>(view, [](View*){}));
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
