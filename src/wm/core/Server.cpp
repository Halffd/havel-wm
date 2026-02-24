#include <wm/Server.hpp>
#include <wm/Layout.hpp>
#include <algorithm>
#include <cstdlib>
#include <unistd.h>
#include <sys/types.h>
#include <cstdio>

namespace havel {

Server::Server() {
    // Initialize workspaces
    for (uint32_t i = 0; i < WORKSPACE_COUNT; ++i) {
        m_workspaces[i] = std::make_unique<Workspace>(i);
    }
}

Server::~Server() = default;

Workspace* Server::activeWorkspace() const {
    return m_workspaces[m_activeWorkspace].get();
}

void Server::setActiveWorkspace(uint32_t id) {
    if (id >= WORKSPACE_COUNT) return;
    
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
    setActiveWorkspace(next);
}

void Server::workspaceToggleTiling() {
    auto* ws = activeWorkspace();
    if (ws) {
        ws->setTilingEnabled(!ws->isTilingEnabled());
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
    
    return view;
}

void Server::onViewMapped(View* view) {
    if (!view) return;
    
    view->setMapped(true);
    focusView(std::shared_ptr<View>(view, [](View*){})); // Non-owning promote
    
    auto* ws = activeWorkspace();
    if (ws && ws->isTilingEnabled()) {
        arrangeWorkspace(view->workspaceId());
    }
}

void Server::onViewUnmapped(View* view) {
    if (!view) return;
    
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
    constexpr uint32_t MOD_ALT = 1 << 3;    // Mod1
    constexpr uint32_t MOD_LOGO = 1 << 6;   // Mod4
    constexpr uint32_t MOD_SHIFT = 1 << 0;  // Shift
    
    bool alt = (modifiers & MOD_ALT) != 0;
    bool meta = (modifiers & MOD_LOGO) != 0;
    bool shift = (modifiers & MOD_SHIFT) != 0;
    
    // Keycode to keysym mapping (evdev codes + 8 = XKB keycode)
    // Common keys: Tab=23, Return=28, space=57, q=16, y=21
    // j=30, k=31, h=35, l=38
    
    // Meta+Tab: Switch workspace
    if (meta && keycode == 23) {
        workspaceStep(shift);
        return;
    }
    
    // Alt+Tab: Switch focus (MRU)
    if (alt && keycode == 23) {
        focusNextMru(shift);
        return;
    }
    
    // Meta+y: Toggle tiling
    if (meta && keycode == 21) {
        workspaceToggleTiling();
        return;
    }
    
    // Alt+Return: Spawn terminal
    if (alt && keycode == 28) {
        spawnTerminal();
        return;
    }
    
    // Alt+q: Quit compositor
    if (alt && (keycode == 16 || keycode == 30)) {
        quit();
        return;
    }
    
    // Alt+h/l: Focus first/last view in workspace
    if (alt && (keycode == 35 || keycode == 38)) {
        auto* ws = activeWorkspace();
        if (ws) {
            auto views = ws->mappedViews();
            if (!views.empty()) {
                if (keycode == 35) { // h - first
                    focusView(views.front());
                } else { // l - last
                    focusView(views.back());
                }
            }
        }
        return;
    }
    
    // Alt+j/k: Focus next/prev view
    if (alt && (keycode == 30 || keycode == 31)) {
        if (shift) {
            // Swap with next/prev - would need implementation
        } else {
            focusNextMru(keycode == 31); // k = backwards
        }
        return;
    }
    
    // Alt+space: Toggle floating for focused view
    if (alt && keycode == 57) {
        auto* ws = activeWorkspace();
        if (ws && ws->activeView()) {
            View* view = ws->activeView();
            view->setFloating(!view->isFloating());
            if (view->isFloating() && !view->hasFloatGeom()) {
                // Store current geom as float geom
                Rect geom = getViewGeometry(view);
                view->setFloatGeom(geom);
            }
        }
        return;
    }
}

void Server::handlePointerButton(uint32_t button, bool pressed, double x, double y) {
    if (!pressed) {
        if (m_grab.button == button) {
            m_grab.view = nullptr;
            m_grab.button = 0;
        }
        return;
    }
    
    // For now, just track the grab start
    // Full implementation would need hit testing from C side
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
        setViewPosition(m_grab.view, newX, newY);
        
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
        setViewSize(m_grab.view, newW, newH);
        
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
    
    Layout::arrangeMasterStack(views, geom);
    
    // Apply the layout by setting positions/sizes
    for (const auto& view : views) {
        Rect vGeom = view->geom();
        setViewPosition(view.get(), vGeom.x, vGeom.y);
        setViewSize(view.get(), vGeom.w, vGeom.h);
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

void Server::spawnTerminal() {
    if (g_server_spawn) {
        g_server_spawn("foot");
    } else {
        pid_t pid = fork();
        if (pid < 0) return;
        if (pid == 0) {
            execlp("foot", "foot", nullptr);
            _exit(127);
        }
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
    // Fade animation would control opacity
    // For now, wlroots doesn't have built-in opacity support
    // This is a placeholder for future compositor-level opacity
    (void)view;
    (void)from;
    (void)to;
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
