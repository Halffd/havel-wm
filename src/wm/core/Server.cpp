#include <wm/Server.hpp>
#include <wm/render/AppIconLoader.hpp>
#include <wm/desktop/DesktopManager.hpp>
#include <wm/desktop/FileManager.hpp>
#include <ui/lock/ScreenLock.hpp>
#include <wm/Layout.hpp>
#include <wm/plugins/Plugins.hpp>
#include <wm/plugins/ServerDecorationPlugin.hpp>
#include <wm/core/WindowGroupManager.hpp>
#include <shell/IPCServer.hpp>
#include "core/CoreWindowManager.hpp"
#include "core/NotificationDaemon.hpp"
#include "scene/SceneGraph.hpp"
#include <nlohmann/json.hpp>
#include <Logger.h>
#include <xkbcommon/xkbcommon-keysyms.h>
#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <filesystem>
#include <unistd.h>
#include <sys/types.h>
#include <climits>
#ifdef HAVE_HAVEL_LANG
#include <wm/havel/HavelHost.hpp>
#endif

namespace havel {

using json = nlohmann::json;
using JsonObject = nlohmann::ordered_json;

Server::Server() {
    LOG_DEBUG("Server constructor");
    // Initialize workspaces
    for (uint32_t i = 0; i < WORKSPACE_COUNT; ++i) {
        m_workspaces[i] = std::make_unique<Workspace>(i);
    }

    // Attach the shell/IPC window projection to the real window store
    // and route its mutations back into the compositor (phase 4).
    WindowManager::ServerActions actions;
    actions.focus = [this](uint64_t id) { m_coreWindowManager.focusWindow(id); };
    actions.minimize = [this](uint64_t id) {
        if (Window* w = m_coreWindowManager.getWindow(id)) m_coreWindowManager.minimizeWindow(w);
    };
    actions.maximize = [this](uint64_t id) {
        if (Window* w = m_coreWindowManager.getWindow(id)) m_coreWindowManager.maximizeWindow(w);
    };
    actions.restore = [this](uint64_t id) {
        if (Window* w = m_coreWindowManager.getWindow(id)) m_coreWindowManager.toggleMaximize(w);
    };
    // closeWindow's m_onClose callback sends the actual WM_DELETE/wl close
    // through the view's native handle; nothing ever registered it, so IPC
    // close only untracked the window (harness proved it: an X client
    // survived IPC close). Wire it here next to the other actions.
    m_coreWindowManager.setOnClose([this](Window* window) {
        if (!window) return;
        if (View* view = window->view()) {
            havel_wlr_close_view(view->nativeHandle());
        }
    });
    actions.close = [this](uint64_t id) { m_coreWindowManager.closeWindow(id); };
    actions.move = [this](uint64_t id, int x, int y) {
        if (Window* w = m_coreWindowManager.getWindow(id)) m_coreWindowManager.moveWindow(w, x, y);
    };
    actions.resize = [this](uint64_t id, int width, int height) {
        if (Window* w = m_coreWindowManager.getWindow(id)) m_coreWindowManager.resizeWindow(w, width, height);
    };
    actions.setFloating = [this](uint64_t id, bool floating) {
        Window* w = m_coreWindowManager.getWindow(id);
        LOG_INFO("[Config] set_floating id=%lu -> %d (window %s)", (unsigned long)id,
                 floating ? 1 : 0, w ? "found" : "NULL");
        if (w) m_coreWindowManager.setFloating(w, floating);
    };
    actions.switchWorkspace = [this](uint32_t ws) { setActiveWorkspace(ws); };
    m_windowManager.attach(&m_coreWindowManager, std::move(actions));

    // Load plugin configuration
    const char* home = getenv("HOME");
    if (home) {
        char configPath[512];
        snprintf(configPath, sizeof(configPath), "%s/.config/havel-wm/plugins.json", home);
        m_pluginManager.loadConfig(configPath);
    }

    // Initialize plugin manager (will skip disabled plugins)
    m_pluginManager.initialize(this);

    // Register built-in plugins
    registerPlugin(std::unique_ptr<Plugin>(create_scale_plugin()));
    registerPlugin(std::unique_ptr<Plugin>(create_wallpaper_plugin()));
    registerPlugin(std::unique_ptr<Plugin>(create_notifications_plugin()));
    registerPlugin(std::unique_ptr<Plugin>(create_custom_layouts_plugin()));
    registerPlugin(std::unique_ptr<Plugin>(create_window_snap_plugin()));
    registerPlugin(std::unique_ptr<Plugin>(create_hot_corners_plugin()));
    registerPlugin(std::unique_ptr<Plugin>(create_gamma_plugin()));
    registerPlugin(std::unique_ptr<Plugin>(create_app_launcher_plugin()));
    registerPlugin(std::unique_ptr<Plugin>(create_alt_tab_plugin()));
    registerPlugin(std::unique_ptr<Plugin>(create_overview_plugin()));
    registerPlugin(std::unique_ptr<Plugin>(create_server_decoration_plugin()));
    registerPlugin(std::unique_ptr<Plugin>(create_draw_plugin()));
    registerPlugin(std::unique_ptr<Plugin>(create_fps_plugin()));
    registerPlugin(std::unique_ptr<Plugin>(create_zoom_plugin()));
    registerPlugin(std::unique_ptr<Plugin>(create_workspace_info_bar()));

    // Register built-in keybindings
    registerKeybindings();

    // Apply persisted settings after plugins exist (plugin.* keys need
    // their plugins registered to apply live)
    settingsLoad();

    // Initialize text input manager (IME support)
    // This must be done after wl_display is created
    // Will be initialized in wlr_bridge.c after display creation
    m_textInputManager = nullptr;

    // Initialize window group manager
    m_windowGroupManager = nullptr;  // Will be initialized in wlr_bridge.c

    // Initialize window manager and set server pointer
    setCoreWindowManagerServer(this);

    // Create scene graph
    m_sceneGraph = scene_create();
    if (m_sceneGraph) {
        LOG_INFO("[Scene] Scene graph created (pool=%d nodes)", SCENE_NODE_POOL_SIZE);

        // Load saved layout if exists
        const char* home = getenv("HOME");
        if (home) {
            char layoutPath[512];
            snprintf(layoutPath, sizeof(layoutPath), "%s/.config/havel-wm/scene_layout.json", home);
            scene_graph_load_layout(m_sceneGraph, layoutPath);
        }
    }

    LOG_INFO("Plugins initialized (%d plugins)", m_pluginManager.plugins().size());
}

Server::~Server() {
    LOG_INFO("Server destructor");
    m_pluginManager.shutdown();

    // Save scene graph layout before destroying
    if (m_sceneGraph) {
        const char* home = getenv("HOME");
        if (home) {
            char layoutPath[512];
            snprintf(layoutPath, sizeof(layoutPath), "%s/.config/havel-wm/scene_layout.json", home);
            scene_graph_save_layout(m_sceneGraph, layoutPath);
        }

        scene_destroy(m_sceneGraph);
        m_sceneGraph = nullptr;
        LOG_INFO("[Scene] Scene graph destroyed");
    }

    // TextInputManager / GestureRecognizer are NOT owned by Server: they
    // are created with new in bridge.cpp (havel_cpp_*_init) and deleted in
    // havel_cpp_server_destroy BEFORE ~Server runs. WindowGroupManager /
    // DesktopManager are process-wide singletons owned by their own .cpp
    // globals; havel_cpp_server_destroy nulls them via their destroy
    // helpers. Do not add deletes for any of these here — double-free.
}

void Server::registerPlugin(std::unique_ptr<Plugin> plugin) {
    m_pluginManager.registerPlugin(std::move(plugin));
}

Workspace* Server::activeWorkspace() const {
    return m_workspaces[m_activeWorkspace].get();
}

void Server::setActiveWorkspace(uint32_t id) {
    if (id >= workspaceCount()) return;

    LOG_INFO("Switching to workspace %u", id);

    // Hide all workspaces on primary output, show only the active one
    if (g_workspace_set_active) {
        g_workspace_set_active(id);
    }

    m_activeWorkspace = id;
    arrangeWorkspace(id);
    
    // Broadcast workspace changed event via IPC
    if (m_ipcServer) {
        JsonObject params;
        params["workspace"] = (int)id;
        m_ipcServer->broadcastEvent(IPCServer::EventType::WorkspaceChanged, m_ipcServer->jsonToString(params));
    }
}

void Server::workspaceStep(bool backwards) {
    uint32_t next = backwards
        ? (m_activeWorkspace + workspaceCount() - 1) % workspaceCount()
        : (m_activeWorkspace + 1) % workspaceCount();
    LOG_DEBUG("Workspace step: %u -> %u", m_activeWorkspace, next);
    setActiveWorkspace(next);
}

void Server::workspaceStepTo(uint32_t target) {
    if (target >= workspaceCount()) return;
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

View* Server::createXdgView(void* c_view, uint32_t workspace_id, const char* appId, const char* title) {
    // Note: View is owned by C++ layer. C stores opaque pointer.
    // Return raw pointer - C stores it but never dereferences.
    auto* view = new View();
    
    // C++ owns workspace_id - single source of truth
    view->setWorkspaceId(workspace_id);
    view->setNativeHandle(c_view);
    
    // Set window metadata from XDG surface
    if (appId) view->setAppId(std::string(appId));
    if (title) view->setTitle(std::string(title));
    
    LOG_INFO("[Server] createXdgView: View=%p, workspace=%u, appId=%s, title=%s", 
             (void*)view, workspace_id, appId ? appId : "unknown", title ? title : "unknown");

    auto* ws = m_workspaces[workspace_id].get();
    if (ws) {
        ws->addView(view);
    }

    // Register with window manager for taskbar integration
    uint64_t windowId = m_windowManager.registerWindow(view);
    view->setWindowId(windowId);

    LOG_DEBUG("Created XDG view, workspace=%u windowId=%lu", workspace_id, windowId);
    return view;
}

void Server::onViewMapped(View* view) {

    LOG_INFO("[Server] onViewMapped: %p (workspace=%u, windowId=%lu)",
             (void*)view, view->workspaceId(), view->windowId());

    view->setMapped(true);
    LOG_INFO("[Server] View mapped=true");

    // Add to window manager for tracking
    m_coreWindowManager.addWindow(view);
    LOG_INFO("[Server] Added to WindowManager");

    // Broadcast window created event via IPC
    if (m_ipcServer) {
        JsonObject params;
        params["id"] = (int)view->windowId();
        params["app_id"] = view->appId();
        params["title"] = view->title();
        params["workspace"] = (int)view->workspaceId();
        m_ipcServer->broadcastEvent(IPCServer::EventType::WindowCreated, m_ipcServer->jsonToString(params));
    }

    // Dispatch to plugins
    ViewEvent event;
    event.view = view;
    event.appId = view->appId().c_str();
    event.title = view->title().c_str();
    event.workspace = view->workspaceId();
    event.x = 0; event.y = 0; event.width = 0; event.height = 0;
    m_pluginManager.dispatchViewMap(event);
    LOG_INFO("[Server] Plugin dispatch complete");

    // Window open animation (scale + fade)
    if (m_animator.isEnabled()) {
        // Start slightly smaller and fade in
        view->setOpacity(0.0f);
        m_animator.fade(0.0f, 1.0f,
            [view](float value) {
                view->setOpacity(value);
            });
        
        // Scale animation would require scene graph support
        // For now, just use opacity fade
    }
    
    // Capture initial thumbnail texture for Alt-Tab
    uint32_t texId = havel_wlr_capture_view_texture(view->nativeHandle());
    if (texId != 0) {
        view->setTextureId(texId);
        // Texture dimensions would be captured by the capture function
        LOG_INFO("[Server] Captured view texture %u for Alt-Tab", texId);
    }

    focusView(view);
    LOG_INFO("[Server] focusView complete");

    auto* ws = activeWorkspace();
    if (ws && ws->isTilingEnabled()) {
        arrangeWorkspace(view->workspaceId());
    } else if (ws) {
        // Stacking spawn: each new floating window gets the next offset
        // in a down-right cascade so successive windows do not stack
        // identically. Wraps after the eighth diagonal; an xwayland
        // window that set its own position overrides this (default -
        //1,-1). 24px is the decoration titlebar height plus a margin.
        placeWindow(view);
    }
}

// Place a newly-mapped floating window according to the configured
// placement mode. Modes: cascade (running offset clamped to the
// workarea), center, pointer (centred under cursor, clamped).
void Server::placeWindow(View* view) {
    int w = view->geom().w;
    int h = view->geom().h;

    switch (m_placementMode) {
        case PlacementMode::Center: {
            int x = (getOutputWidth() - w) / 2;
            int y = (getOutputHeight() - h) / 2;
            if (x < 0) x = 0;
            if (y < 0) y = 0;
            setViewPosition(view, x, y, false);
            break;
        }
        case PlacementMode::Pointer: {
            int px = (int)cursorX() - w / 2;
            int py = (int)cursorY() - h / 2;
            int workW = getOutputWidth(), workH = getOutputHeight();
    int workW2 = getOutputWidth(), workH2 = getOutputHeight();
            if (px < 0) px = 0;
            if (py < 0) py = 0;
            if (px + w > workW) px = workW - w;
            if (py + h > workH) py = workH - h;
            setViewPosition(view, px, py, false);
            break;
        }
        case PlacementMode::Cascade:
        default: {
            int workW = getOutputWidth(), workH = getOutputHeight();
    int workW2 = getOutputWidth(), workH2 = getOutputHeight();
            if (m_cascadeX + w > workW || m_cascadeY + h > workH) {
                m_cascadeX = 40;
                m_cascadeY = 40;
            }
            setViewPosition(view, m_cascadeX, m_cascadeY, false);
            LOG_INFO("[Server] cascade spawn at (%d,%d)", m_cascadeX, m_cascadeY);
            m_cascadeX += workW * 3 / 100;
            m_cascadeY += workH * 3 / 100;
            break;
        }
    }
}

void Server::onViewUnmapped(View* view) {
    if (!view) return;

    LOG_DEBUG("View unmapped");

    // Window close animation (fade out)
    if (m_animator.isEnabled()) {
        float currentOpacity = view->opacity();
        m_animator.fade(currentOpacity, 0.0f,
            [view](float value) {
                view->setOpacity(value);
            });
        
        // Delay actual unmapping until animation completes
        // For now, just set opacity and let the animation run
        // The view will be fully hidden when opacity reaches 0
    }
    
    view->setMapped(false);

    // Dispatch to plugins
    ViewEvent event;
    event.view = view;
    event.appId = "";
    event.title = "";
    event.workspace = view->workspaceId();
    event.x = 0; event.y = 0; event.width = 0; event.height = 0;
    m_pluginManager.dispatchViewUnmap(event);

    auto* ws = m_workspaces[view->workspaceId()].get();
    if (ws && ws->activeView() == view) {
        ws->setActiveView(nullptr);
    }

    if (m_workspaces[view->workspaceId()]->isTilingEnabled()) {
        arrangeWorkspace(view->workspaceId());
    }
}

void Server::onViewAppIdChanged(View* view, const std::string& appId) {
    if (!view || appId.empty()) return;

    view->setAppId(appId);

    // Keep the core Window wrapper in sync — the shell projection and
    // IPC queries read from it (floating rules match against this).
    if (Window* window = m_coreWindowManager.getWindow(view)) {
        window->setAppId(appId);
    }
}

void Server::onViewTitleChanged(View* view, const std::string& title) {
    if (!view || title.empty()) return;

    view->setTitle(title);

    // Keep the core Window wrapper in sync — the shell projection and
    // IPC queries read from it (Window::setTitle also re-syncs the View).
    if (Window* window = m_coreWindowManager.getWindow(view)) {
        window->setTitle(title);
    }

    // Re-broadcast to IPC listeners
    if (m_ipcServer) {
        JsonObject params;
        params["id"] = (int)view->windowId();
        params["title"] = title;
        m_ipcServer->broadcastEvent(IPCServer::EventType::WindowTitleChanged, m_ipcServer->jsonToString(params));
    }
}

void Server::onViewDestroyed(View* view) {
    if (!view) return;

    LOG_INFO("View destroyed");

    // Broadcast window destroyed event via IPC
    if (m_ipcServer) {
        JsonObject params;
        params["id"] = (int)view->windowId();
        m_ipcServer->broadcastEvent(IPCServer::EventType::WindowDestroyed, m_ipcServer->jsonToString(params));
    }

    // Dispatch to plugins first (before removing from internal state)
    ViewEvent event;
    event.view = view;
    event.appId = view->appId().c_str();
    event.title = view->title().c_str();
    event.workspace = view->workspaceId();
    event.x = 0; event.y = 0; event.width = 0; event.height = 0;
    m_pluginManager.dispatchViewDestroy(event);

    // Unregister from window managers
    if (view->windowId() != 0) {
        m_windowManager.unregisterWindow(view->windowId());
    }
    m_coreWindowManager.removeWindow(view);

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

bool Server::handleKey(uint32_t keycode, bool pressed, uint32_t modifiers, uint32_t keysym, char key_char, const char* utf8) {
    m_lastInputTimeMs = (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();

    // Screen lock intercepts ALL keyboard input when active — before
    // keybindings and plugins, so the lock screen is impenetrable.
    if (m_screenLock && m_screenLock->isLocked()) {
        std::string utf8Str = utf8 ? utf8 : "";
        m_screenLock->handleKey(keysym, pressed, utf8Str);
        return true;  // consumed: no keybinding, no plugin, no client
    }

    // Keybindings are press-only
    if (pressed && m_keybindingManager.handleKey(keysym, pressed, modifiers)) {
        return true;
    }

    // Then, give plugins a chance to handle the key (presses and releases;
    // plugins that track press state need release events too)
    KeyEvent event;
    event.keycode = keycode;
    event.modifiers = modifiers;
    event.pressed = pressed;
    event.keysym = keysym;
    event.key_char = key_char;

    // Copy UTF-8 string (up to 7 bytes + null terminator)
    if (utf8) {
        strncpy(event.utf8, utf8, 7);
        event.utf8[7] = '\0';
    } else {
        event.utf8[0] = '\0';
    }

    if (m_pluginManager.dispatchKey(event)) {
        // Plugin consumed the event
        return true;
    }

    // Modifier masks (matching wlroots/xkbcommon)
    constexpr uint32_t MOD_ALT = 1 << 3;      // Mod1
    constexpr uint32_t MOD_LOGO = 1 << 6;     // Mod4
    constexpr uint32_t MOD_SHIFT = 1 << 0;    // Shift
    constexpr uint32_t MOD_CTRL = 1 << 2;     // Mod3/Control

    bool alt = (modifiers & MOD_ALT) != 0;
    bool meta = (modifiers & MOD_LOGO) != 0;
    bool shift = (modifiers & MOD_SHIFT) != 0;
    bool ctrl = (modifiers & MOD_CTRL) != 0;

    // Deterministic logging: always log keycode, keysym, raw modifiers, and decoded flags
    LOG_DEBUG("[KEY] keycode=%u keysym=0x%04X char='%c' raw_mods=0x%02X decoded=%s%s%s%s",
              keycode, keysym, key_char ? key_char : ' ', modifiers,
              alt ? "Alt+" : "",
              meta ? "Meta+" : "",
              shift ? "Shift+" : "",
              ctrl ? "Ctrl+" : "");

    // Key not consumed by keybindings or plugins, return false
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
    m_lastInputTimeMs = (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    // Update cursor position for HotCorners and other plugins
    m_cursorX = x;
    m_cursorY = y;

    // Handle draw mode
    if (m_grab.mode == GrabMode::Draw) {
        // Find draw plugin and handle motion
        auto& plugins = m_pluginManager.plugins();
        for (const auto& plugin : plugins) {
            if (strcmp(plugin->name(), "draw") == 0) {
                // Cast to DrawPlugin and call handler
                // For now, use a simple approach - in production would use proper plugin messaging
                printf("[Server] Draw mode: motion at (%.0f, %.0f)\n", x, y);
                break;
            }
        }
        return;
    }

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

    LOG_INFO("[Server] focusView: %p (windowId=%lu)", (void*)view, view->windowId());

    m_focusManager.promote(view);

    auto* ws = m_workspaces[view->workspaceId()].get();
    if (ws) {
        ws->setActiveView(view);
    }

    // Focus state is derived by the shell projection from core state
    // (WindowManager::focusedWindow queries getFocusedWindow); no
    // mirror-update needed here since phase 4.

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

// Window enumeration implementation
std::vector<View*> Server::getAllViews() const {
    std::vector<View*> allViews;
    for (uint32_t ws = 0; ws < workspaceCount(); ++ws) {
        auto* workspace = m_workspaces[ws].get();
        if (workspace) {
            auto wsViews = workspace->views();
            allViews.insert(allViews.end(), wsViews.begin(), wsViews.end());
        }
    }
    return allViews;
}

std::vector<View*> Server::getViewsInWorkspace(uint32_t workspaceId) const {
    if (workspaceId >= workspaceCount()) return {};
    auto* workspace = m_workspaces[workspaceId].get();
    return workspace ? workspace->views() : std::vector<View*>{};
}

View* Server::getFocusedView() const {
    auto* ws = activeWorkspace();
    return ws ? ws->activeView() : nullptr;
}

View* Server::getViewById(uint64_t id) const {
    // Search all workspaces for view with matching windowId
    for (uint32_t ws = 0; ws < workspaceCount(); ++ws) {
        auto* workspace = m_workspaces[ws].get();
        if (workspace) {
            for (View* view : workspace->views()) {
                if (view && view->windowId() == id) {
                    return view;
                }
            }
        }
    }
    return nullptr;
}

// Window metadata implementation
std::string Server::getViewAppId(View* view) const {
    if (!view) return "";
    return view->appId();
}

std::string Server::getViewTitle(View* view) const {
    if (!view) return "";
    return view->title();
}

void Server::arrangeWorkspace(uint32_t id) {
    if (id >= workspaceCount()) return;

    auto* ws = m_workspaces[id].get();
    if (!ws || !ws->isTilingEnabled()) return;

    // Use tiledViews() to exclude floating windows from tiling layout
    auto views = ws->tiledViews();
    if (views.empty()) return;

    Rect geom = outputGeometry(id);
    if (!geom.isValid()) return;

    LOG_DEBUG("Arranging workspace %u with %zu tiled views", id, views.size());

    // Update scene graph layout if available
    if (m_sceneGraph) {
        SceneOutput* output = scene_output_get_primary(m_sceneGraph);
        if (output) {
            SceneWorkspace* scene_ws = scene_workspace_get(output, id);
            if (scene_ws) {
                scene_layout_workspace(scene_ws, geom.x, geom.y, geom.w, geom.h);
                LOG_DEBUG("[Scene] Layout updated for workspace %u", id);
            }
        }
    }

    Layout::arrangeMasterStack(views, geom);

    // Apply the layout by setting positions/sizes
    for (auto* view : views) {
        Rect vGeom = view->geom();
        setViewPosition(view, vGeom.x, vGeom.y, true);
        setViewSize(view, vGeom.w, vGeom.h, true);
    }
}

void Server::setOutputGeometry(uint32_t workspaceId, const Rect& geom) {
    if (workspaceId < workspaceCount()) {
        m_outputGeoms[workspaceId] = geom;
    }
}

Rect Server::outputGeometry(uint32_t workspaceId) const {
    if (workspaceId < workspaceCount()) {
        auto it = m_outputGeoms.find(workspaceId);
        if (it != m_outputGeoms.end()) {
            return it->second;
        }
    }
    return {};
}

int Server::getOutputWidth() const {
    // Return primary output width from m_outputGeoms
    auto it = m_outputGeoms.begin();
    if (it != m_outputGeoms.end()) {
        return it->second.w;
    }
    return 1920;  // Default fallback
}

int Server::getOutputHeight() const {
    // Return primary output height from m_outputGeoms
    auto it = m_outputGeoms.begin();
    if (it != m_outputGeoms.end()) {
        return it->second.h;
    }
    return 1080;  // Default fallback
}

// ============================================================================
// Action helpers
// ============================================================================

void Server::spawnCommand(const char* cmd) {
    if (!cmd || !*cmd) return;
    if (g_server_spawn) {
        g_server_spawn(cmd);
        return;
    }
    // Fork+exec, no shell (same as havel_cpp_server_spawn's body)
    char* cmd_copy = strdup(cmd);
    if (!cmd_copy) return;
    char* argv[64];
    int argc = 0;
    char* token = strtok(cmd_copy, " \t\n");
    while (token && argc < 63) {
        argv[argc++] = token;
        token = strtok(NULL, " \t\n");
    }
    argv[argc] = NULL;
    if (argc > 0) {
        pid_t pid = fork();
        if (pid == 0) {
            execvp(argv[0], argv);
            _exit(127);
        } else if (pid > 0) {
            LOG_INFO("[Server] Spawned: %s (PID: %d)", cmd, pid);
        }
    }
    free(cmd_copy);
}

void Server::spawnTerminal() {
    const char* terminals[] = {"alacritty", "foot", nullptr};

    for (int i = 0; terminals[i] != nullptr; i++) {
        // Check if terminal exists using access()
        char checkPath[256];
        snprintf(checkPath, sizeof(checkPath), "/usr/bin/%s", terminals[i]);
        if (access(checkPath, X_OK) == 0) {
            // Terminal found, spawn it
            LOG_INFO("Spawning terminal: %s", terminals[i]);
            if (g_server_spawn) {
                g_server_spawn(terminals[i]);
            } else {
                pid_t pid = fork();
                if (pid == 0) {
                    execlp(terminals[i], terminals[i], (char*)NULL);
                    _exit(127);
                }
            }
            return;
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
        char checkPath[256];
        snprintf(checkPath, sizeof(checkPath), "/usr/bin/%s", browsers[i]);
        if (access(checkPath, X_OK) == 0) {
            if (g_server_spawn) {
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
        char checkPath[256];
        snprintf(checkPath, sizeof(checkPath), "/usr/bin/%s", managers[i]);
        if (access(checkPath, X_OK) == 0) {
            if (g_server_spawn) {
                g_server_spawn(managers[i]);
                LOG_INFO("Spawned file manager: %s", managers[i]);
                return;
            }
        }
    }
}

void Server::beginViewDrag(View* view, int mode) {
    if (!view || !view->nativeHandle()) return;
    havel_wlr_begin_view_drag(view->nativeHandle(), mode);
}

void Server::closeFocusedWindow() {
    auto* ws = activeWorkspace();
    if (ws && ws->activeView()) {
        View* view = ws->activeView();
        LOG_INFO("Closing focused window");
        // Send close request through wlroots callback
        havel_wlr_close_view(view->nativeHandle());
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
        havel_wlr_minimize_view(view->nativeHandle());

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
            havel_wlr_set_view_fullscreen(view->nativeHandle(), false);
            lastFullscreenView = nullptr;
        } else {
            // Enter fullscreen
            if (lastFullscreenView) {
                havel_wlr_set_view_fullscreen(lastFullscreenView->nativeHandle(), false);
            }
            havel_wlr_set_view_fullscreen(view->nativeHandle(), true);
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
        moveViewToWorkspace(currentWs->activeView(), ws);
    }
}

void Server::moveViewToWorkspace(View* view, uint32_t ws) {
    if (!view || ws >= workspaceCount()) return;
    if (view->workspaceId() == ws) return;

    // Move the compositor-side view between workspaces (this is what
    // actually relocates the window on screen)...
    auto* currentWs = m_workspaces[view->workspaceId()].get();
    if (currentWs) currentWs->removeView(view);
    view->setWorkspaceId(ws);
    if (auto* targetWs = m_workspaces[ws].get()) {
        targetWs->addView(view);
        LOG_INFO("Moved view %p to workspace %u", (void*)view, ws);
        arrangeWorkspace(ws);
    }
    // ...and reparent the scene node into the target workspace's tree -
    // the model move alone left the node in the old tree, so the window
    // never appeared on the target workspace (and stayed visible on the
    // old one) until an unrelated arrange happened to fix nothing.
    if (view->nativeHandle()) {
        havel_wlr_reparent_view_workspace(view->nativeHandle(), ws);
    }

    // ...and keep the core window model in sync. The IPC window list
    // (get_windows) reports Window::workspace(), while the screen follows
    // the View; updating only one of them left the two disagreeing - the
    // grid's move button relocated the window but get_windows still said
    // workspace 0 (observed live), and the IPC move_to_workspace command
    // changed the model without moving anything.
    if (Window* window = m_coreWindowManager.getWindow(view)) {
        window->setWorkspace(ws);
    }
}

void Server::moveViewToWorkspaceRelative(bool next) {
    auto* ws = activeWorkspace();
    if (ws && ws->activeView()) {
        uint32_t target = next 
            ? (m_activeWorkspace + 1) % workspaceCount()
            : (m_activeWorkspace + workspaceCount() - 1) % workspaceCount();
        moveViewToWorkspace(target);
    }
}

void Server::quit() {
    if (g_server_quit) {
        g_server_quit();
    }
}

void Server::setBackgroundColor(float r, float g, float b) {
    m_bgColorR = r;
    m_bgColorG = g;
    m_bgColorB = b;
    LOG_INFO("Background color set to (%.2f, %.2f, %.2f)", r, g, b);
}

void Server::getBackgroundColor(float* r, float* g, float* b) const {
    if (r) *r = m_bgColorR;
    if (g) *g = m_bgColorG;
    if (b) *b = m_bgColorB;
}

void Server::setGamma(float gamma) {
    m_displaySettings.setGamma(gamma);
}

void Server::setTemperature(int kelvin) {
    m_displaySettings.setTemperature(kelvin);
}

void Server::setBrightness(float brightness) {
    m_displaySettings.setBrightness(brightness);
}

// ============================================================================
// Zoom (fullscreen scale + lens/strip/custom magnifier).
// Targets live here; the ZoomPlugin animates applied values toward them
// and draws the magnifier overlays. See ZoomPlugin.cpp for the display
// geometry and source-region math.
// ============================================================================

static Server::ZoomTargets& zoomTargets(std::map<int, Server::ZoomTargets>& m, int idx) {
    return m[idx];  // default-inserts 1.0/1.0/off
}

void Server::zoomConfigure(const std::string& json) {
    // Control parameters only; magnifier display geometry is plugin-side
    // (plugins.json "zoom" keys) - Server never draws, so echoing geometry
    // here would drift from what the plugin actually renders.
    try {
        auto j = nlohmann::json::parse(json);
        // Degenerate values would break the zoom math outright (division
        // by step, levels below 1 clamp immediately), so reject them
        // instead of storing.
        if (j.contains("step")) {
            float v = j["step"].get<float>();
            if (v <= 1.0f) {
                LOG_WARN("[Zoom] step must be > 1.0, got %.2f - ignored", v);
            } else {
                m_zoomConfig.step = v;
            }
        }
        if (j.contains("max_level")) {
            float v = j["max_level"].get<float>();
            if (v < 1.0f) {
                LOG_WARN("[Zoom] max_level must be >= 1.0, got %.2f - ignored", v);
            } else {
                m_zoomConfig.maxLevel = v;
            }
        }
        if (j.contains("max_mag")) {
            float v = j["max_mag"].get<float>();
            if (v <= 0.0f) {
                LOG_WARN("[Zoom] max_mag must be > 0, got %.2f - ignored", v);
            } else {
                m_zoomConfig.maxMag = v;
            }
        }
        if (j.contains("toggle_level")) {
            float v = j["toggle_level"].get<float>();
            if (v <= 0.0f) {
                LOG_WARN("[Zoom] toggle_level must be > 0, got %.2f - ignored", v);
            } else {
                m_zoomConfig.toggleLevel = v;
            }
        }
        LOG_INFO("[Zoom] Configured (step %.2f toggle %.2f)",
                 m_zoomConfig.step, m_zoomConfig.toggleLevel);
    } catch (const std::exception& e) {
        LOG_WARN("[Zoom] Bad configure json: %s", e.what());
    }
}

void Server::zoomIn(int outputIndex) {
    auto& t = zoomTargets(m_zoomTargets, outputIndex);
    if ((ZoomMode)m_zoomConfig.mode == ZoomMode::Fullscreen)
        t.level = std::min(m_zoomConfig.maxLevel, std::max(1.0f, t.level) * m_zoomConfig.step);
    else
        t.mag = std::min(m_zoomConfig.maxMag, std::max(1.0f, t.mag) * m_zoomConfig.step);
    t.active = true;
}

void Server::zoomOut(int outputIndex) {
    auto& t = zoomTargets(m_zoomTargets, outputIndex);
    if ((ZoomMode)m_zoomConfig.mode == ZoomMode::Fullscreen)
        t.level = std::max(1.0f, t.level / m_zoomConfig.step);
    else
        t.mag = std::max(1.0f, t.mag / m_zoomConfig.step);
    if (t.level <= 1.0f && t.mag <= 1.0f) t.active = false;
}

void Server::zoomReset(int outputIndex) {
    auto& t = zoomTargets(m_zoomTargets, outputIndex);
    t.level = 1.0f;
    t.mag = 1.0f;
    t.active = false;
}

void Server::zoomToggle(int outputIndex) {
    auto& t = zoomTargets(m_zoomTargets, outputIndex);
    t.active = !t.active;
    if (t.active) {
        if ((ZoomMode)m_zoomConfig.mode == ZoomMode::Fullscreen)
            t.level = std::min(m_zoomConfig.maxLevel, std::max(1.0f, m_zoomConfig.toggleLevel));
        else
            t.mag = std::min(m_zoomConfig.maxMag, std::max(1.0f, m_zoomConfig.toggleLevel));
    } else {
        t.level = 1.0f;
        t.mag = 1.0f;
    }
}

void Server::zoomPushStart(int outputIndex) {
    auto& t = zoomTargets(m_zoomTargets, outputIndex);
    t.active = true;
    if ((ZoomMode)m_zoomConfig.mode == ZoomMode::Fullscreen)
        t.level = std::min(m_zoomConfig.maxLevel, std::max(1.0f, m_zoomConfig.toggleLevel));
    else
        t.mag = std::min(m_zoomConfig.maxMag, std::max(1.0f, m_zoomConfig.toggleLevel));
}

void Server::zoomPushEnd(int outputIndex) {
    zoomReset(outputIndex);
}

void Server::zoomSetMode(int mode) {
    ZoomMode m = (mode >= 0 && mode <= 3) ? (ZoomMode)mode : ZoomMode::Fullscreen;
    if (m == m_zoomConfig.mode) return;
    m_zoomConfig.mode = m;
    // Mode switches clear every output: fullscreen scale and magnifier
    // overlays are different mechanisms, a silent carry-over would leave
    // one stuck at a stale scale.
    m_zoomTargets.clear();
    LOG_INFO("[Zoom] Mode -> %s", m == ZoomMode::Fullscreen ? "fullscreen" :
                                  m == ZoomMode::Lens ? "lens" :
                                  m == ZoomMode::Strip ? "strip" : "custom");
}

void Server::zoomCycleMode() {
    zoomSetMode(((int)m_zoomConfig.mode + 1) % 4);
}

float Server::zoomTargetLevel(int outputIndex) const {
    auto it = m_zoomTargets.find(outputIndex);
    return it == m_zoomTargets.end() ? 1.0f : it->second.level;
}

float Server::zoomTargetMag(int outputIndex) const {
    auto it = m_zoomTargets.find(outputIndex);
    return it == m_zoomTargets.end() ? 1.0f : it->second.mag;
}

bool Server::zoomActive(int outputIndex) const {
    auto it = m_zoomTargets.find(outputIndex);
    return it != m_zoomTargets.end() && it->second.active;
}

std::string Server::zoomStatusJson() const {
    nlohmann::json j;
    const char* modes[] = {"fullscreen", "lens", "strip", "custom"};
    j["mode"] = modes[(int)m_zoomConfig.mode];
    j["step"] = m_zoomConfig.step;
    j["toggle_level"] = m_zoomConfig.toggleLevel;
    nlohmann::json outputs = nlohmann::json::array();
    for (const auto& [idx, t] : m_zoomTargets) {
        outputs.push_back({{"output", idx},
                           {"active", t.active},
                           {"level", t.level},
                           {"mag", t.mag}});
    }
    j["outputs"] = outputs;
    return j.dump();
}

void Server::zoomRequestCapture(int outputIndex, float cx, float cy, int w, int h) {
    if (outputIndex < 0 || outputIndex >= kMaxZoomOutputs) return;
    auto& r = m_zoomCaptureReq[outputIndex];
    r.pending = true;
    r.cx = cx;
    r.cy = cy;
    r.w = w;
    r.h = h;
}

bool Server::zoomTakeCaptureRequest(int outputIndex, float* cx, float* cy, int* w, int* h) {
    if (outputIndex < 0 || outputIndex >= kMaxZoomOutputs) return false;
    auto& r = m_zoomCaptureReq[outputIndex];
    if (!r.pending) return false;
    r.pending = false;
    *cx = r.cx;
    *cy = r.cy;
    *w = r.w;
    *h = r.h;
    return true;
}

void Server::zoomFeedCapture(int outputIndex, const uint32_t* pixels, int w, int h,
                             float srcX, float srcY) {
    if (outputIndex < 0 || outputIndex >= kMaxZoomOutputs || !pixels || w <= 0 || h <= 0)
        return;
    auto& c = m_zoomCapture[outputIndex];
    c.pixels.assign(pixels, pixels + (size_t)w * h);
    c.w = w;
    c.h = h;
    c.srcX = srcX;
    c.srcY = srcY;
    c.seq++;
}

bool Server::zoomGetCapture(int outputIndex, const uint32_t** pixels, int* w, int* h,
                            float* srcX, float* srcY, uint64_t* seq) const {
    if (outputIndex < 0 || outputIndex >= kMaxZoomOutputs) return false;
    const auto& c = m_zoomCapture[outputIndex];
    if (c.seq == 0 || c.pixels.empty()) return false;
    // Null out-params are allowed: the zoom plugin probes the sequence
    // number alone to detect fresh captures for its persistent tree.
    if (pixels) *pixels = c.pixels.data();
    if (w) *w = c.w;
    if (h) *h = c.h;
    if (srcX) *srcX = c.srcX;
    if (srcY) *srcY = c.srcY;
    if (seq) *seq = c.seq;
    return true;
}

// ============================================================================
// Settings store (persisted, live-applied)
// ============================================================================

namespace {
std::string settingsStorePath() {
    const char* home = getenv("HOME");
    if (!home || !*home) return "";
    return std::string(home) + "/.config/havel-wm/settings.json";
}
} // namespace

// Apply one setting immediately where a handler exists. Unknown keys are
// simply stored - scripts and the REPL read them via settings_get.
bool Server::settingsSet(const std::string& key, const std::string& value) {
    // Validate BEFORE storing: a bad value must not poison the live map
    // or get persisted for the next boot.
    if (key == "gamma" || key == "brightness") {
        float f = 0.0f;
        try {
            f = std::stof(value);
        } catch (const std::exception&) {
            LOG_WARN("[Settings] %s wants a float, got '%s'",
                     key.c_str(), value.c_str());
            return false;
        }
        if (key == "gamma" && (f <= 0.0f || f > 10.0f)) {
            LOG_WARN("[Settings] gamma %s out of range (0,10]", value.c_str());
            return false;
        }
        if (key == "brightness" && (f < 0.0f || f > 1.0f)) {
            LOG_WARN("[Settings] brightness %s out of range [0,1]", value.c_str());
            return false;
        }
    } else if (key == "temperature") {
        int t = 0;
        try {
            t = std::stoi(value);
        } catch (const std::exception&) {
            LOG_WARN("[Settings] temperature wants an int, got '%s'", value.c_str());
            return false;
        }
        if (t < 1000 || t > 10000) {
            LOG_WARN("[Settings] temperature %d out of range [1000,10000]", t);
            return false;
        }
    }

    m_settings[key] = value;

    if (key == "gamma") {
        setGamma(std::stof(value));
    } else if (key == "temperature") {
        setTemperature(std::stoi(value));
    } else if (key == "brightness") {
        setBrightness(std::stof(value));
    } else if (key == "animations") {
        setAnimationsEnabled(value == "true" || value == "1");
    } else if (key == "background_color") {
        float r = 0.1f, g = 0.1f, b = 0.15f;
        if (sscanf(value.c_str(), "%f,%f,%f", &r, &g, &b) == 3) {
            setBackgroundColor(r, g, b);
        } else {
            LOG_WARN("[Settings] background_color wants r,g,b floats, got '%s'", value.c_str());
        }
    } else if (key.rfind("plugin.", 0) == 0) {
        // plugin.<name>.<key> -> live plugin setting
        size_t second = key.find('.', 7);
        if (second != std::string::npos && second > 7) {
            std::string pluginName = key.substr(7, second - 7);
            std::string pluginKey = key.substr(second + 1);
            PluginConfig::getInstance().setValue(pluginName, pluginKey, value);
            m_pluginManager.reloadPluginSettings(pluginName);
            LOG_INFO("[Settings] plugin %s.%s = %s", pluginName.c_str(),
                     pluginKey.c_str(), value.c_str());
        } else {
            LOG_WARN("[Settings] plugin settings want plugin.<name>.<key>, got '%s'", key.c_str());
        }
    }

    // Persist atomically: write a sibling temp file, flush it, then rename
    // over the target so a crash mid-write never leaves a truncated
    // settings.json behind.
    std::string path = settingsStorePath();
    if (!path.empty()) {
        std::error_code ec;
        std::filesystem::create_directories(
            std::filesystem::path(path).parent_path(), ec);
        nlohmann::json j = nlohmann::json::object();
        for (const auto& [k, v] : m_settings) j[k] = v;
        std::filesystem::path target(path);
        std::filesystem::path tmp = target;
        tmp += ".tmp";
        {
            std::ofstream out(tmp, std::ios::trunc);
            if (!out) {
                LOG_WARN("[Settings] Cannot write %s", tmp.string().c_str());
                return true;  // live value still applied
            }
            out << j.dump(2);
            out.flush();
            if (!out) {
                LOG_WARN("[Settings] Short write to %s", tmp.string().c_str());
                std::filesystem::remove(tmp, ec);
                return true;
            }
        }  // close (and flush) before the rename
        std::filesystem::rename(tmp, target, ec);
        if (ec) {
            LOG_WARN("[Settings] Atomic replace of %s failed: %s",
                     target.string().c_str(), ec.message().c_str());
            std::filesystem::remove(tmp, ec);
        }
    }
    return true;
}

std::string Server::settingsGet(const std::string& key) const {
    auto it = m_settings.find(key);
    return it == m_settings.end() ? std::string() : it->second;
}

void Server::settingsLoad() {
    std::string path = settingsStorePath();
    if (path.empty()) return;
    std::ifstream in(path);
    if (!in) return;
    try {
        auto j = nlohmann::json::parse(in);
        if (!j.is_object()) return;
        int applied = 0;
        for (auto it = j.begin(); it != j.end(); ++it) {
            if (!it.value().is_string()) continue;
            // One bad persisted value must not abort the remaining keys:
            // each apply (and its re-save of the store) is isolated here.
            try {
                settingsSet(it.key(), it.value().get<std::string>());
                applied++;
            } catch (const std::exception& e) {
                LOG_WARN("[Settings] Skipping %s: %s",
                         it.key().c_str(), e.what());
            }
        }
        LOG_INFO("[Settings] Loaded %d persisted setting(s)", applied);
    } catch (const std::exception& e) {
        LOG_WARN("[Settings] Could not load %s: %s", path.c_str(), e.what());
    }
}

std::string Server::ipcSettings(const std::string& args) {
    try {
        auto j = nlohmann::json::parse(args);
        std::string action = j.value("action", "get");
        if (action == "set") {
            std::string key = j.value("key", "");
            if (key.empty() || !j.contains("value"))
                return m_ipcServer->createErrorResponse(-5, "Missing key or value");
            nlohmann::json v = j["value"];
            if (!v.is_string())
                return m_ipcServer->createErrorResponse(-2, "value must be a string");
            if (!settingsSet(key, v.get<std::string>()))
                return m_ipcServer->createErrorResponse(-6, "Invalid value for " + key);
            nlohmann::json r;
            r["success"] = true;
            r["key"] = key;
            r["value"] = v;
            return r.dump();
        }
        if (action == "get") {
            std::string key = j.value("key", "");
            std::string value = settingsGet(key);
            if (value.empty())
                return m_ipcServer->createErrorResponse(-3, "No such setting: " + key);
            nlohmann::json r;
            r["key"] = key;
            r["value"] = value;
            return r.dump();
        }
        if (action == "list") {
            nlohmann::json r = nlohmann::json::object();
            for (const auto& [k, v] : m_settings) r[k] = v;
            return r.dump();
        }
        return m_ipcServer->createErrorResponse(-4, "Unknown action: " + action);
    } catch (const std::exception& e) {
        return m_ipcServer->createErrorResponse(-1, e.what());
    }
}

// ============================================================================
// Declarative configuration (config.hv statements, applied live)
// ============================================================================

void Server::setGaps(int px) {
    if (px < 0) px = 0;
    if (px > 200) px = 200;
    Layout::setGaps(px);
    LOG_INFO("[Config] gaps -> %d (tiling enabled)", px);
    // Gaps are a tiling concept: declaring them opts the workspaces into
    // tiling (tiling is opt-in otherwise; default placement is cascade).
    for (uint32_t ws = 0; ws < m_workspaceCount; ws++) {
        if (m_workspaces[ws] && !m_workspaces[ws]->isTilingEnabled())
            m_workspaces[ws]->setTilingEnabled(true);
        arrangeWorkspace(ws);
    }
}

void Server::setBorderWidth(int px) {
    if (px < 0) px = 0;
    if (px > 40) px = 40;
    DecorationTheme::BORDER_WIDTH = px;
    LOG_INFO("[Config] border_size -> %d", px);
    // Decorations redraw from the theme each pass; nothing to invalidate.
}

void Server::addFloatingRule(const std::string& appId) {
    if (appId.empty()) return;
    auto* mgr = static_cast<WindowGroupManager*>(m_windowGroupManager);
    if (!mgr) {
        LOG_WARN("[Config] floating_rule: window group manager not up yet");
        return;
    }
    WindowRule rule;
    rule.appId = appId;
    rule.floating = true;
    mgr->addWindowRule(rule);
}

void Server::setDefaultLayout(const std::string& name) {
    if (name.empty()) return;
    m_defaultLayout = name;
    // The custom-layouts plugin owns the alternative arrangements; route
    // through the plugin-command path so it applies its own state.
    if (name != "master")
        m_pluginManager.pluginCommand("custom_layouts", "layout", name);
    LOG_INFO("[Config] default_layout -> %s", name.c_str());
}

void Server::setWorkspaceCount(uint32_t n) {
    if (n < 1) n = 1;
    if (n > WORKSPACE_COUNT) n = WORKSPACE_COUNT;
    if (n == m_workspaceCount) return;

    // Shrinking rehomes every view of the removed workspaces into the
    // last surviving one first - dropping them would strand the windows.
    if (n < m_workspaceCount) {
        for (uint32_t ws = n; ws < m_workspaceCount; ws++) {
            for (auto* view : m_workspaces[ws]->views()) {
                if (!view) continue;
                moveViewToWorkspace(view, n - 1);
            }
        }
    }
    m_workspaceCount = n;
    if (m_activeWorkspace >= n) {
        setActiveWorkspace(n - 1);
    }
    arrangeWorkspace(n - 1);
    LOG_INFO("[Config] workspace_count -> %u", n);
}

void Server::addMonitorRule(const std::string& name, int x, int y) {
    if (name.empty()) return;
    m_monitorRules[name] = {x, y};
    LOG_INFO("[Config] monitor rule: %s at (%d,%d)", name.c_str(), x, y);
    // Outputs added later consult the rule at placement time; one already
    // running gets moved now, so config.hv and runtime calls behave the same.
    if (m_nativeHandle) {
        havel_wlr_place_output((havel_wlr_server_t*)m_nativeHandle, name.c_str(), x, y);
    }
}

bool Server::monitorRuleFor(const std::string& name, int* x, int* y) const {
    auto it = m_monitorRules.find(name);
    if (it == m_monitorRules.end()) return false;
    if (x) *x = it->second.first;
    if (y) *y = it->second.second;
    return true;
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
// Compositing Control
// ============================================================================

void Server::setCompositingEnabled(bool enabled) {
    m_compositingEnabled = enabled;
    if (m_nativeHandle) {
        havel_wlr_set_compositing((struct havel_wlr_server*)m_nativeHandle, enabled);
    }
    LOG_INFO("[Compositing] forced scene rendering %s",
             enabled ? "enabled" : "disabled");
}

bool Server::toggleCompositing() {
    setCompositingEnabled(!m_compositingEnabled);
    return m_compositingEnabled;
}

// ============================================================================
// Effect Control (Global)
// ============================================================================

void Server::setGrayscaleEnabled(bool enabled) {
    m_displaySettings.setGrayscaleEnabled(enabled);
    LOG_INFO("Grayscale %s", enabled ? "enabled" : "disabled");
}

void Server::setNegativeEnabled(bool enabled) {
    m_displaySettings.setNegativeEnabled(enabled);
    LOG_INFO("Negative %s", enabled ? "enabled" : "disabled");
}

bool Server::isGrayscaleEnabled() const {
    return m_displaySettings.isGrayscaleEnabled();
}

bool Server::isNegativeEnabled() const {
    return m_displaySettings.isNegativeEnabled();
}

void Server::toggleGrayscale() {
    m_displaySettings.toggleGrayscale();
}

void Server::toggleNegative() {
    m_displaySettings.toggleNegative();
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
        // Call C layer directly
        LOG_DEBUG("[VIEW] direct position: (%d,%d) handle=%p", x, y, view->nativeHandle());
        havel_wlr_set_view_position(view->nativeHandle(), x, y);
    }
}

void Server::setViewOpacity(View* view, float alpha) {
    if (!view || !view->nativeHandle()) return;
    // wlroots 0.20 exposes per-BUFFER opacity (wlr_scene_buffer_set_
    // opacity); the C side applies it to every buffer under the view's
    // scene tree and re-applies on later commits. Floor 0.05 so the
    // window can never become fully invisible/uninputtable by accident.
    if (alpha < 0.05f) alpha = 0.05f;
    if (alpha > 1.0f) alpha = 1.0f;
    if (Window* w = m_coreWindowManager.getWindow(view)) {
        w->setOpacity(alpha);
    }
    havel_wlr_set_view_opacity(view->nativeHandle(), alpha);
}

void Server::setViewGeometry(View* view, int x, int y, int w, int h) {
    if (!view) return;
    
    // Set both position and size
    setViewPosition(view, x, y, false);
    setViewSize(view, w, h, false);
    
    LOG_DEBUG("[Server] View geometry set: (%d,%d) %dx%d", x, y, w, h);
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
        // Call C layer directly
        havel_wlr_set_view_size(view->nativeHandle(), w, h);
    }
}

void Server::focusViewNative(View* view) {
    LOG_INFO("[Server] focusViewNative: %p", (void*)view);
    if (!view || !view->nativeHandle()) return;
    // Focus the view through wlroots
    havel_wlr_focus_view(view->nativeHandle());
}

void Server::raiseView(View* view) {
    LOG_INFO("[Server] raiseView: %p", (void*)view);
    if (!view || !view->nativeHandle()) return;
    // Raise through scene graph
    havel_wlr_raise_view(view->nativeHandle());
}

Rect Server::getViewGeometry(View* view) {
    Rect result;
    // Get from View directly
    if (view) {
        result = view->geom();
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
            havel_wlr_set_view_position(view->nativeHandle(), x, y);
        });
}

void Server::animateViewResize(View* view, int fromW, int fromH, int toW, int toH) {
    auto& state = m_viewAnimState[view];

    m_animator.resize(fromW, fromH, toW, toH,
        [this, view, &state](int w, int h) {
            state.currentW = w;
            state.currentH = h;
            havel_wlr_set_view_size(view->nativeHandle(), w, h);
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

// ============================================================================
// Keybinding Registration
// ============================================================================

void Server::registerKeybindings() {
    using namespace std::placeholders;

    LOG_INFO("[Keybindings] Registering keybindings...");

    // Ctrl+Meta+F4: Quit compositor
    m_keybindingManager.registerKeybinding(
        KeybindingManager::MOD_CTRL | KeybindingManager::MOD_LOGO, XKB_KEY_F4,
        "quit", [this]() { quit(); }
    );

    // Ctrl+Meta+Return: Show rofi
    m_keybindingManager.registerKeybinding(
        KeybindingManager::MOD_CTRL | KeybindingManager::MOD_LOGO, XKB_KEY_Return,
        "show_rofi", [this]() { spawnRofi(); }
    );

    // Meta+Return: Spawn terminal
    m_keybindingManager.registerKeybinding(
        KeybindingManager::MOD_LOGO, XKB_KEY_Return,
        "spawn_terminal", [this]() { spawnTerminal(); }
    );

    // Meta+Tab: Workspace switch forward
    m_keybindingManager.registerKeybinding(
        KeybindingManager::MOD_LOGO, XKB_KEY_Tab,
        "workspace_next", [this]() { workspaceStep(false); }
    );

    // Meta+Shift+Tab: Workspace switch backward
    m_keybindingManager.registerKeybinding(
        KeybindingManager::MOD_LOGO | KeybindingManager::MOD_SHIFT, XKB_KEY_Tab,
        "workspace_prev", [this]() { workspaceStep(true); }
    );

    // Meta+PgUp: Workspace switch backward
    m_keybindingManager.registerKeybinding(
        KeybindingManager::MOD_LOGO, XKB_KEY_Page_Up,
        "workspace_prev_pgup", [this]() { workspaceStep(false); }
    );

    // Meta+PgDn: Workspace switch forward
    m_keybindingManager.registerKeybinding(
        KeybindingManager::MOD_LOGO, XKB_KEY_Page_Down,
        "workspace_next_pgdn", [this]() { workspaceStep(true); }
    );

    // Meta+W / Meta+D are owned by OverviewPlugin / AppLauncherPlugin:
    // the plugin implementations are the driven, complete UIs. The Server
    // overlay-controller variants had no navigation/Escape path (undrivable
    // once shown), so their keybindings only shadowed the plugins.

    // Meta+Y: Toggle tiling
    m_keybindingManager.registerKeybinding(
        KeybindingManager::MOD_LOGO, XKB_KEY_y,
        "toggle_tiling", [this]() { workspaceToggleTiling(); }
    );

    // Meta+G is owned by CustomLayoutsPlugin (grid layout): the grayscale
    // flag it toggled here was state-only — no renderer consumed it.
    // Meta+N is owned by GammaPlugin (night mode): same state-only story.

    // Meta+Shift+Q: Close focused window
    m_keybindingManager.registerKeybinding(
        KeybindingManager::MOD_LOGO | KeybindingManager::MOD_SHIFT, XKB_KEY_q,
        "close_window", [this]() { closeFocusedWindow(); }
    );

    // Meta+F: Toggle fullscreen
    m_keybindingManager.registerKeybinding(
        KeybindingManager::MOD_LOGO, XKB_KEY_f,
        "toggle_fullscreen", [this]() { toggleFullscreen(); }
    );

    // Meta+Shift+S: Print scene graph tree (debug)
    m_keybindingManager.registerKeybinding(
        KeybindingManager::MOD_LOGO | KeybindingManager::MOD_SHIFT, XKB_KEY_s,
        "scene_graph_print", [this]() {
            if (m_sceneGraph) {
                printf("\n=== Scene Graph Tree ===\n");
                scene_print_tree(m_sceneGraph);
                printf("\n=== Scene Graph Stats ===\n");
                scene_print_stats(m_sceneGraph);
                printf("========================\n\n");
            }
        }
    );

    // Meta+Shift+L: Save scene graph layout
    m_keybindingManager.registerKeybinding(
        KeybindingManager::MOD_LOGO | KeybindingManager::MOD_SHIFT, XKB_KEY_l,
        "scene_graph_save", [this]() {
            if (m_sceneGraph) {
                const char* home = getenv("HOME");
                if (home) {
                    char layoutPath[512];
                    snprintf(layoutPath, sizeof(layoutPath), "%s/.config/havel-wm/scene_layout.json", home);
                    if (scene_graph_save_layout(m_sceneGraph, layoutPath)) {
                        printf("[Debug] Layout saved to %s\n", layoutPath);
                    }
                }
            }
        }
    );

    // Meta+Shift+G: Print scene graph update (debug)
    m_keybindingManager.registerKeybinding(
        KeybindingManager::MOD_LOGO | KeybindingManager::MOD_SHIFT, XKB_KEY_g,
        "scene_graph_update", [this]() {
            if (m_sceneGraph) {
                scene_graph_update(m_sceneGraph);
                printf("[Debug] Scene graph updated\n");
            }
        }
    );

    // Meta+Space: Toggle floating
    m_keybindingManager.registerKeybinding(
        KeybindingManager::MOD_LOGO, XKB_KEY_space,
        "toggle_floating", [this]() { toggleFloating(); }
    );

    // Meta+0 is workspace 10 (matches the Meta+1-9 pattern). Minimize has
    // no documented keybinding; IPC minimize covers it.

    // Meta+Shift+V: Toggle always on top. Meta+Shift+F is the documented
    // FPS-overlay binding (owned by FPSPlugin); previously this binding
    // silently shadowed it.
    m_keybindingManager.registerKeybinding(
        KeybindingManager::MOD_LOGO | KeybindingManager::MOD_SHIFT, XKB_KEY_v,
        "toggle_always_on_top", [this]() { toggleAlwaysOnTop(); }
    );

    // Meta+Shift+R: Reload configuration (hot-reload)
    m_keybindingManager.registerKeybinding(
        KeybindingManager::MOD_LOGO | KeybindingManager::MOD_SHIFT, XKB_KEY_r,
        "reload_config", [this]() {
            LOG_INFO("Reloading configuration...");
            m_pluginManager.reloadConfig();
        }
    );

    // Meta+Shift+P: Toggle forced scene composition. When disabled,
    // wlroots may use direct scan-out for an eligible fullscreen scene.
    m_keybindingManager.registerKeybinding(
        KeybindingManager::MOD_LOGO | KeybindingManager::MOD_SHIFT, XKB_KEY_p,
        "toggle_compositing", [this]() { toggleCompositing(); }
    );

    // Meta+1 through Meta+9: Direct workspace switch
    for (uint32_t i = 0; i < 9; i++) {
        m_keybindingManager.registerKeybinding(
            KeybindingManager::MOD_LOGO, XKB_KEY_1 + i,
            ("workspace_" + std::to_string(i + 1)).c_str(),
            [this, i]() { workspaceStepTo(i); }
        );
    }

    // Meta+0: Workspace 10
    m_keybindingManager.registerKeybinding(
        KeybindingManager::MOD_LOGO, XKB_KEY_0,
        "workspace_10", [this]() { workspaceStepTo(9); }
    );

    // Meta+Shift+1 through Meta+Shift+9: Move window to workspace
    for (uint32_t i = 0; i < 9; i++) {
        m_keybindingManager.registerKeybinding(
            KeybindingManager::MOD_LOGO | KeybindingManager::MOD_SHIFT, XKB_KEY_1 + i,
            ("move_to_workspace_" + std::to_string(i + 1)).c_str(),
            [this, i]() { moveViewToWorkspace(i); }
        );
    }

    // Meta+Shift+0: Move window to workspace 10
    m_keybindingManager.registerKeybinding(
        KeybindingManager::MOD_LOGO | KeybindingManager::MOD_SHIFT, XKB_KEY_0,
        "move_to_workspace_10", [this]() { moveViewToWorkspace(9); }
    );
    
    // Log all registered bindings
    m_keybindingManager.listBindings();
    LOG_INFO("[Keybindings] Registration complete (%zu bindings)", m_keybindingManager.getBindingCount());
}

// ============================================================================
// IPC Server Implementation
// ============================================================================

bool Server::startIPCServer(const std::string& socketPath) {
    if (m_ipcServer) {
        LOG_WARN("IPC server already running");
        return false;
    }

    m_ipcServer = std::make_unique<IPCServer>(m_windowManager);
    m_screenLock = new ScreenLock();

    // Cursor control: IPCServer owns the handlers (they lived as stubs in
    // the command table), but only Server has the compositor handle, so
    // the real wlr_cursor warp/query lives here and the handlers call
    // back through these.
    m_ipcServer->setCursorWarpCallback([this](int x, int y) -> bool {
        if (!m_nativeHandle) return false;
        return havel_wlr_warp_pointer((struct havel_wlr_server*)m_nativeHandle,
                                       (double)x, (double)y);
    });
    m_ipcServer->setCursorVisibilityCallback([this](bool visible) {
        if (!m_nativeHandle) return;
        if (visible) {
            havel_wlr_show_cursor((struct havel_wlr_server*)m_nativeHandle);
        } else {
            havel_wlr_hide_cursor((struct havel_wlr_server*)m_nativeHandle);
        }
    });
    m_ipcServer->setCursorThemeCallback([this](const std::string& theme, int size) {
        if (!m_nativeHandle) return;
        havel_wlr_set_cursor_theme((struct havel_wlr_server*)m_nativeHandle,
                                   theme.c_str(), size);
    });
    m_ipcServer->setCursorQueryCallback([this](int* x, int* y) -> bool {
        if (!m_nativeHandle || !x || !y) return false;
        double px = -1, py = -1;
        if (!havel_wlr_get_pointer((struct havel_wlr_server*)m_nativeHandle, &px, &py))
            return false;
        *x = (int)px;
        *y = (int)py;
        return true;
    });
    m_ipcServer->setPointerClickCallback([this](uint32_t button, bool pressed) -> bool {
        if (!m_nativeHandle) return false;
        return havel_wlr_click_pointer((struct havel_wlr_server*)m_nativeHandle,
                                       button, pressed);
    });

    // Commands that are fully implemented inside IPCServer. The table pairs
    // each method name with its handler; args are passed through unchanged.
    struct SelfCommand {
        const char* name;
        std::string (IPCServer::*handler)();
        std::string (IPCServer::*handlerWithArgs)(const std::string&);
    };
    static const SelfCommand kSelfCommands[] = {
        {"get_windows",            &IPCServer::handleGetWindows,       nullptr},
        {"get_focused",            &IPCServer::handleGetFocused,      nullptr},
        {"get_workspace",          &IPCServer::handleGetWorkspace,    nullptr},
        {"ping",                   &IPCServer::handlePing,            nullptr},
        {"focus",                  nullptr, &IPCServer::handleFocus},
        {"minimize",               nullptr, &IPCServer::handleMinimize},
        {"maximize",               nullptr, &IPCServer::handleMaximize},
        {"restore",                nullptr, &IPCServer::handleRestore},
        {"close",                  nullptr, &IPCServer::handleClose},
        {"move",                   nullptr, &IPCServer::handleMove},
        {"resize",                 nullptr, &IPCServer::handleResize},
        {"set_floating",           nullptr, &IPCServer::handleSetFloating},
        {"workspace",              nullptr, &IPCServer::handleWorkspace},
        {"spawn",                  nullptr, &IPCServer::handleSpawn},
        {"screenshot",             nullptr, &IPCServer::handleScreenshot},
        {"portal_screenshot",      nullptr, &IPCServer::handlePortalScreenshot},
        {"screenshot_window",      nullptr, &IPCServer::handleScreenshotWindow},
        {"screenshot_region",      nullptr, &IPCServer::handleScreenshotRegion},
        {"audio_record",           nullptr, &IPCServer::handleAudioRecord},
        {"video_record",           nullptr, &IPCServer::handleVideoRecord},
        {"stop_recording",         nullptr, &IPCServer::handleStopRecording},
        {"get_cursor_position",    nullptr, &IPCServer::handleGetCursorPosition},
        {"warp_cursor",            nullptr, &IPCServer::handleWarpCursor},
        {"click_pointer",          nullptr, &IPCServer::handleClickPointer},
        {"set_cursor_theme",       nullptr, &IPCServer::handleSetCursorTheme},
        {"set_cursor_size",        nullptr, &IPCServer::handleSetCursorSize},
        {"hide_cursor",            nullptr, &IPCServer::handleHideCursor},
        {"show_cursor",            nullptr, &IPCServer::handleShowCursor},
        {"subscribe",              nullptr, &IPCServer::handleSubscribe},
        {"unsubscribe",            nullptr, &IPCServer::handleUnsubscribe},
    };
    for (const auto& cmd : kSelfCommands) {
        if (cmd.handler) {
            std::string (IPCServer::*h)() = cmd.handler;
            m_ipcServer->registerCommand(cmd.name, [this, h](const std::string&) -> std::string {
                return (m_ipcServer.get()->*h)();
            });
        } else {
            std::string (IPCServer::*h)(const std::string&) = cmd.handlerWithArgs;
            m_ipcServer->registerCommand(cmd.name, [this, h](const std::string& args) -> std::string {
                return (m_ipcServer.get()->*h)(args);
            });
        }
    }

    // ========================================================================
    // Output/Monitor Commands
    // ========================================================================
    // NOTE: get_outputs / set_output_scale removed in the IPC honesty pass
    // (phase 1). The C bridge exposes no output iteration or scale-setting
    // entry points, so these commands had no honest implementation path.
    // Re-add them when bridge support exists.

    // ========================================================================
    // Display Settings Commands
    // ========================================================================
    // Window placement mode (cascade/center/pointer)
    {
        std::string mode = getenv("HAVEL_PLACEMENT") ? getenv("HAVEL_PLACEMENT") : "";
        if (mode == "center") m_placementMode = PlacementMode::Center;
        else if (mode == "pointer") m_placementMode = PlacementMode::Pointer;
        else m_placementMode = PlacementMode::Cascade;
    }

    m_ipcServer->registerCommand("set_gamma", [this](const std::string& args) -> std::string {
        return ipcSetGamma(args);
    });

    m_ipcServer->registerCommand("set_temperature", [this](const std::string& args) -> std::string {
        return ipcSetTemperature(args);
    });

    m_ipcServer->registerCommand("set_brightness", [this](const std::string& args) -> std::string {
        return ipcSetBrightness(args);
    });

    m_ipcServer->registerCommand("zoom", [this](const std::string& args) -> std::string {
        return ipcZoom(args);
    });

    m_ipcServer->registerCommand("settings", [this](const std::string& args) -> std::string {
        return ipcSettings(args);
    });

    // Single reload_config handler: refreshes plugins.json AND the havel
    // script config (config.hv) in one shot. (There used to be a second
    // registration further down that only did plugins.json; since dispatch
    // finds the first match it was dead code - and each half-reload left
    // the other file stale.)
    m_ipcServer->registerCommand("reload_config", [this](const std::string& args) -> std::string {
        (void)args;
        std::string hvErr = reloadHavelConfig();
        // A missing config.hv (first boot) or a build without the havel
        // embed is a normal state, not a reload failure - same policy as a
        // missing plugins.json (defaults kept, not an error).
        bool hvAbsent = hvErr.rfind("no config at ", 0) == 0 ||
                        hvErr == "built without the havel embed";
        if (!hvErr.empty() && !hvAbsent)
            return m_ipcServer->createErrorResponse(-3, hvErr);
        bool pluginsOk = m_pluginManager.reloadConfig();
        JsonObject r;
        r["success"] = true;
        std::string msg = hvAbsent ? "config.hv absent; " : "config.hv reloaded; ";
        msg += pluginsOk ? "plugins.json reloaded"
                         : "no plugins.json (defaults kept)";
        r["message"] = msg;
        return m_ipcServer->jsonToString(r);
    });

    // Introspection: outputs (geometry from the C layout) and keybindings
    m_ipcServer->registerCommand("get_outputs", [this](const std::string& args) -> std::string {
        (void)args;
        auto* native = (havel_wlr_server_t*)nativeHandle();
        nlohmann::json arr = nlohmann::json::array();
        if (native) {
            for (int i = 0; i < 16; i++) {
                int x = 0, y = 0, w = 0, h = 0, primary = 0;
                char name[64] = {0};
                if (!havel_wlr_get_output_info(native, i, &x, &y, &w, &h,
                                               name, sizeof(name), &primary))
                    break;
                arr.push_back({{"index", i},
                               {"name", std::string(name)},
                               {"x", x}, {"y", y}, {"width", w}, {"height", h},
                               {"primary", primary != 0}});
            }
        }
        nlohmann::json r;
        r["outputs"] = arr;
        r["count"] = arr.size();
        return r.dump();
    });

    m_ipcServer->registerCommand("get_keybindings", [this](const std::string& args) -> std::string {
        (void)args;
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& b : m_keybindingManager.bindings()) {
            arr.push_back({{"name", b.name},
                           {"modifiers", b.modifiers},
                           {"keysym", b.keysym}});
        }
        nlohmann::json r;
        r["keybindings"] = arr;
        r["count"] = arr.size();
        return r.dump();
    });

    // File manager integration (validated paths, no shell)
    m_ipcServer->registerCommand("file_list", [this](const std::string& args) -> std::string {
        return ipcFileList(args);
    });
    m_ipcServer->registerCommand("file_info", [this](const std::string& args) -> std::string {
        return ipcFileInfo(args);
    });
    auto fileOp = [this](const std::string& op) {
        return [this, op](const std::string& args) -> std::string {
            return ipcFileOp(op, args);
        };
    };
    m_ipcServer->registerCommand("file_copy", fileOp("copy"));
    m_ipcServer->registerCommand("file_move", fileOp("move"));
    m_ipcServer->registerCommand("file_rename", fileOp("rename"));
    m_ipcServer->registerCommand("file_delete", fileOp("delete"));
    m_ipcServer->registerCommand("file_mkdir", fileOp("mkdir"));
    m_ipcServer->registerCommand("open_path", [this](const std::string& args) -> std::string {
        return ipcOpenPath(args);
    });

    m_ipcServer->registerCommand("get_display_settings", [this](const std::string& args) -> std::string {
        return ipcGetDisplaySettings();
    });

    // ========================================================================
    // Plugin Commands
    // ========================================================================
    m_ipcServer->registerCommand("plugin_command", [this](const std::string& args) -> std::string {
        // {"plugin":"app_launcher","cmd":"open","arg":""}
        try {
            auto j = nlohmann::json::parse(args);
            std::string plugin = j.value("plugin", "");
            std::string cmd = j.value("cmd", "");
            std::string arg = j.value("arg", "");
            if (plugin.empty() || cmd.empty())
                return m_ipcServer->createErrorResponse(-5, "Missing plugin or cmd");
            bool ok = m_pluginManager.pluginCommand(plugin, cmd, arg);
            JsonObject r;
            r["success"] = ok;
            return m_ipcServer->jsonToString(r);
        } catch (const std::exception& e) {
            return m_ipcServer->createErrorResponse(-1, e.what());
        }
    });

    m_ipcServer->registerCommand("get_plugins", [this](const std::string& args) -> std::string {
        return ipcGetPlugins();
    });

    // enable_plugin / disable_plugin / configure_plugin removed in the IPC
    // honesty pass (phase 1): PluginConfig is a read-only parsed config with
    // no runtime enable/disable mechanism, so these had no honest path.
    // Toggling plugins is done by editing ~/.config/havel-wm/plugins.json
    // and calling reload_config.

    // ========================================================================
    // Notification Commands
    // ========================================================================
    m_ipcServer->registerCommand("notify", [this](const std::string& args) -> std::string {
        return ipcNotify(args);
    });

    m_ipcServer->registerCommand("close_notification", [this](const std::string& args) -> std::string {
        return ipcCloseNotification(args);
    });

    // ========================================================================
    // Screenshot Commands are registered in the kSelfCommands table above.
    // ========================================================================
    // ========================================================================
    // Configuration Commands
    // ========================================================================

    m_ipcServer->registerCommand("get_config", [this](const std::string& args) -> std::string {
        return ipcGetConfig();
    });

    // ========================================================================
    // Window Query Commands (Enhanced)
    // ========================================================================
    m_ipcServer->registerCommand("get_window", [this](const std::string& args) -> std::string {
        return ipcGetWindow(args);
    });

    m_ipcServer->registerCommand("get_windows_by_app", [this](const std::string& args) -> std::string {
        return ipcGetWindowsByApp(args);
    });

    m_ipcServer->registerCommand("set_window_opacity", [this](const std::string& args) -> std::string {
        return ipcSetWindowOpacity(args);
    });

    m_ipcServer->registerCommand("set_window_fullscreen", [this](const std::string& args) -> std::string {
        return ipcSetWindowFullscreen(args);
    });

    m_ipcServer->registerCommand("set_window_always_on_top", [this](const std::string& args) -> std::string {
        return ipcSetWindowAlwaysOnTop(args);
    });

    // ========================================================================
    // Workspace Commands (Enhanced)
    // ========================================================================
    m_ipcServer->registerCommand("get_workspaces", [this](const std::string& args) -> std::string {
        return ipcGetWorkspaces();
    });

    m_ipcServer->registerCommand("workspace_next", [this](const std::string& args) -> std::string {
        return ipcWorkspaceStep(+1);
    });

    m_ipcServer->registerCommand("workspace_prev", [this](const std::string& args) -> std::string {
        return ipcWorkspaceStep(-1);
    });

    m_ipcServer->registerCommand("move_to_workspace", [this](const std::string& args) -> std::string {
        return ipcMoveToWorkspace(args);
    });

    // Compositing: "enabled" forces wlroots to render the scene and
    // disables direct scan-out. Disabling it permits direct scan-out when
    // the current scene is eligible; Wayland still composites otherwise.
    m_ipcServer->registerCommand("get_compositing", [this](const std::string& args) -> std::string {
        (void)args;
        JsonObject r;
        r["success"] = true;
        r["compositing"] = compositingEnabled();
        return m_ipcServer->jsonToString(r);
    });

    m_ipcServer->registerCommand("set_compositing", [this](const std::string& args) -> std::string {
        bool enabled = m_ipcServer->extractJsonBool(args, "enabled", compositingEnabled());
        setCompositingEnabled(enabled);
        JsonObject r;
        r["success"] = true;
        r["compositing"] = compositingEnabled();
        return m_ipcServer->jsonToString(r);
    });

    m_ipcServer->registerCommand("toggle_compositing", [this](const std::string& args) -> std::string {
        (void)args;
        toggleCompositing();
        JsonObject r;
        r["success"] = true;
        r["compositing"] = compositingEnabled();
        return m_ipcServer->jsonToString(r);
    });

    // Cursor mode: "auto" lets wlroots use a hardware cursor plane when
    // the backend has one, "software" forces the cursor to be composited
    // into the framebuffer (visible on backends that only advertise a
    // plane, and in captures).
    m_ipcServer->registerCommand("set_cursor_mode", [this](const std::string& args) -> std::string {
        std::string mode = m_ipcServer->extractJsonString(args, "mode", "auto");
        bool software = (mode == "software" || mode == "soft");
        if (!m_nativeHandle) return m_ipcServer->createErrorResponse(-5, "no compositor handle");
        requestSoftwareCursor(software);
        JsonObject r;
        r["success"] = true;
        r["mode"] = software ? "software" : "auto";
        return m_ipcServer->jsonToString(r);
    });

    // Screen lock: PAM-authenticated overlay that intercepts all input
    m_ipcServer->registerCommand("lock_screen", [this](const std::string& args) -> std::string {
        (void)args;
        lockScreen();
        JsonObject r;
        r["success"] = true;
        r["locked"] = true;
        return m_ipcServer->jsonToString(r);
    });

    m_ipcServer->registerCommand("unlock_screen", [this](const std::string& args) -> std::string {
        (void)args;
        unlockScreen();
        JsonObject r;
        r["success"] = true;
        r["locked"] = false;
        return m_ipcServer->jsonToString(r);
    });

    m_ipcServer->registerCommand("is_screen_locked", [this](const std::string& args) -> std::string {
        (void)args;
        JsonObject r;
        r["success"] = true;
        r["locked"] = isScreenLocked();
        return m_ipcServer->jsonToString(r);
    });

    // Idle auto-lock: screen locks after timeout_ms of no input (0 = off)
    m_ipcServer->registerCommand("set_idle_lock", [this](const std::string& args) -> std::string {
        int timeoutMs = m_ipcServer->extractJsonInt(args, "timeout_ms", 0);
        setIdleLockTimeout((uint32_t)timeoutMs);
        JsonObject r;
        r["success"] = true;
        r["timeout_ms"] = timeoutMs;
        return m_ipcServer->jsonToString(r);
    });

    // Wallpaper slideshow: set a list of PNGs and an interval (seconds)
    m_ipcServer->registerCommand("set_wallpaper_slideshow", [this](const std::string& args) -> std::string {
        std::vector<std::string> paths;
        int interval = 300;
        try {
            auto j = nlohmann::json::parse(args);
            if (j.contains("paths") && j["paths"].is_array()) {
                for (const auto& e : j["paths"]) {
                    if (!e.is_string()) continue;
                    std::string p = e.get<std::string>();
                    if (!p.empty() && p[0] == '/' &&
                        std::filesystem::is_regular_file(p)) {
                        paths.push_back(p);
                    }
                }
            }
            if (j.contains("interval"))
                interval = j["interval"].get<int>();
        } catch (const std::exception& e) {
            return m_ipcServer->createErrorResponse(-1,
                std::string("Invalid JSON: ") + e.what());
        }
        if (interval < 1) {
            return m_ipcServer->createErrorResponse(-2,
                "interval must be >= 1 second");
        }
        if (paths.empty()) {
            return m_ipcServer->createErrorResponse(-5, "no valid wallpaper paths");
        }
        auto& dm = havel::getDesktopManager();
        dm.setWallpaperSlideshow(paths, interval);
        JsonObject r;
        r["success"] = true;
        r["count"] = (int)paths.size();
        r["interval"] = interval;
        return m_ipcServer->jsonToString(r);
    });

    // Wallpaper image (PNG), drawn behind every window. Empty path
    // clears back to the solid background colour.
    m_ipcServer->registerCommand("set_wallpaper", [this](const std::string& args) -> std::string {
        std::string path = m_ipcServer->extractJsonString(args, "path", "");
        if (!setWallpaperFromFile(path)) {
            return m_ipcServer->createErrorResponse(-5, "cannot decode wallpaper file");
        }
        JsonObject r;
        r["success"] = true;
        r["path"] = path;
        return m_ipcServer->jsonToString(r);
    });

    // Script/test helper: feed a scroll tick through the plugin dispatch
    // path (the same route a real wheel takes via the C bridge).
    m_ipcServer->registerCommand("scroll_pointer", [this](const std::string& args) -> std::string {
        double dx = m_ipcServer->extractJsonFloat(args, "dx", 0.0f);
        double dy = m_ipcServer->extractJsonFloat(args, "dy", 0.0f);
        if (!m_nativeHandle) return m_ipcServer->createErrorResponse(-5, "no compositor handle");
        havel_wlr_scroll_pointer((struct havel_wlr_server*)m_nativeHandle, dx, dy);
        JsonObject r;
        r["success"] = true;
        r["dx"] = dx;
        r["dy"] = dy;
        return m_ipcServer->jsonToString(r);
    });

    // ========================================================================
    // System Commands
    // ========================================================================
    m_ipcServer->registerCommand("get_version", [this](const std::string& args) -> std::string {
        return ipcGetVersion();
    });

    m_ipcServer->registerCommand("get_stats", [this](const std::string& args) -> std::string {
        return ipcGetStats();
    });

    m_ipcServer->registerCommand("debug_info", [this](const std::string& args) -> std::string {
        JsonObject debug;
        debug["version"] = "0.1.0";
        debug["socket"] = m_ipcServer->getSocketPath();
        debug["clients"] = (int)m_ipcServer->getClientCount();
        return m_ipcServer->jsonToString(debug);
    });

    m_ipcServer->registerCommand("ping", [this](const std::string& args) -> std::string {
        return m_ipcServer->handlePing();
    });

    m_ipcServer->registerCommand("subscribe", [this](const std::string& args) -> std::string {
        return m_ipcServer->handleSubscribe(args);
    });

    m_ipcServer->registerCommand("unsubscribe", [this](const std::string& args) -> std::string {
        return m_ipcServer->handleUnsubscribe(args);
    });

    m_ipcServer->registerCommand("quit", [this](const std::string& args) -> std::string {
        return m_ipcServer->handleQuit();
    });

#ifdef HAVE_HAVEL_LANG
    // Run a Havel script: {"file": "/path/to/script.hv"} or
    // {"source": "spawn(\"foot\")"}
    m_ipcServer->registerCommand("havel_run", [this](const std::string& args) -> std::string {
        if (!m_havelHost) {
            return m_ipcServer->createErrorResponse(-5, "Havel host not initialized");
        }
        std::string file = m_ipcServer->extractJsonString(args, "file");
        std::string source = m_ipcServer->extractJsonString(args, "source");
        bool ok = false;
        std::string err;
        if (!file.empty()) {
            ok = m_havelHost->runFile(file);
            err = m_havelHost->lastError();
        } else if (!source.empty()) {
            ok = m_havelHost->runString(source, "ipc_snippet");
            err = m_havelHost->lastError();
        } else {
            return m_ipcServer->createErrorResponse(-5, "havel_run: needs file or source");
        }
        if (!ok) {
            return m_ipcServer->createErrorResponse(-1, err.empty() ? "script failed" : err);
        }
        return m_ipcServer->createSuccessResponse("script executed");
    });

    // Evaluate an expression and return its serialized value (REPL path).
    // {"source": "1 + 2"} -> {"success": true, "result": "3"}
    m_ipcServer->registerCommand("havel_eval", [this](const std::string& args) -> std::string {
        if (!m_havelHost) {
            return m_ipcServer->createErrorResponse(-5, "Havel host not initialized");
        }
        std::string source = m_ipcServer->extractJsonString(args, "source");
        if (source.empty()) {
            return m_ipcServer->createErrorResponse(-5, "havel_eval: needs source");
        }
        std::string result;
        if (!m_havelHost->evalString(source, &result)) {
            std::string err = m_havelHost->lastError();
            return m_ipcServer->createErrorResponse(-1, err.empty() ? "eval failed" : err);
        }
        JsonObject r;
        r["success"] = true;
        r["result"] = result;
        return m_ipcServer->jsonToString(r);
    });
#endif

    const bool ipcStarted = m_ipcServer->start(socketPath);
    if (ipcStarted) {
        LOG_INFO("IPC server started on: %s", socketPath.c_str());
    } else {
        LOG_ERROR("Failed to start IPC server on: %s", socketPath.c_str());
        m_ipcServer.reset();
    }

#ifdef HAVE_HAVEL_LANG
    // Embed the Havel language and run the startup config (config.hv).
    // Scripts register keybindings via keybind() and run quick
    // compositor commands; they execute before the frame loop starts.
    // This must not depend on the IPC server starting: a second
    // compositor instance with the default socket path fails IPC by
    // design (live-socket check refuses to steal), but its config.hv
    // keybind() closures are compositor features, not IPC features.
    if (initHavelHost()) {
        const char* home = getenv("HOME");
        if (home) {
            char configPath[512];
            snprintf(configPath, sizeof(configPath), "%s/.config/havel-wm/config.hv", home);
            if (access(configPath, R_OK) == 0) {
                m_havelHost->runFile(configPath);
            } else {
                LOG_INFO("[Havel] No config.hv at %s (scripts optional)", configPath);
            }
        }
    }
#endif

    return ipcStarted;
}

void Server::stopIPCServer() {
    if (m_ipcServer) {
        m_ipcServer->stop();
        LOG_INFO("IPC server stopped");
        m_ipcServer.reset();
    }
}

bool Server::isIPCServerRunning() const {
    return m_ipcServer && m_ipcServer->isRunning();
}

void Server::processIPCEvents() {
    if (m_ipcServer) {
        m_ipcServer->processEvents();
    }
}

int Server::collectIpcFds(int* out, int max) {
    if (!m_ipcServer) return 0;
    return static_cast<int>(m_ipcServer->collectIpcFds(out, static_cast<size_t>(max)));
}

// ============================================================================
// IPC command handlers (phase 2 extraction from startIPCServer lambdas).
// Each returns the JSON-RPC result string for its command.
// ============================================================================

#ifdef HAVE_HAVEL_LANG
bool Server::initHavelHost() {
    if (m_havelHost && m_havelHost->isInitialized()) return true;
    if (!m_havelHost) {
        m_havelHost = new HavelHost();
    }
    if (!m_havelHost->init(this)) {
        LOG_ERROR("[Havel] Failed to initialize language host: %s",
                  m_havelHost->lastError().c_str());
        return false;
    }
    return true;
}

bool Server::registerHavelKeybinding(uint32_t modifiers, uint32_t keysym,
                                      const char* name, std::function<void()> callback) {
    return m_keybindingManager.registerKeybinding(modifiers, keysym, name, callback);
}
#endif  // HAVE_HAVEL_LANG

// Re-run config.hv: drop the script-registered bindings first (register
// refuses duplicate combos, so a plain re-run would silently disable
// every script hotkey), then execute the file. A failure re-registers
// the saved bindings - a broken config must leave the running setup
// untouched. Returns an empty string on success or the interpreter error.
std::string Server::reloadHavelConfig() {
#ifdef HAVE_HAVEL_LANG
    if (!m_havelHost) return "havel host not initialized";
    const char* home = getenv("HOME");
    if (!home) return "no HOME";
    std::string path = std::string(home) + "/.config/havel-wm/config.hv";
    if (access(path.c_str(), R_OK) != 0) return "no config at " + path;

    std::vector<Keybinding> saved;
    m_keybindingManager.unregisterByPrefixSave("havel:", &saved);
    if (!m_havelHost->runFile(path)) {
        std::string err = m_havelHost->lastError();
        // The partially-executed config may have registered some of its
        // own "havel:" bindings already. Drop everything the script
        // namespace owns before restoring, or registerKeybinding's
        // duplicate-combo refusal would silently discard saved bindings.
        m_keybindingManager.unregisterByPrefixSave("havel:", nullptr);
        for (auto& b : saved) {
            m_keybindingManager.registerKeybinding(b.modifiers, b.keysym,
                                                   b.name.c_str(), b.callback);
        }
        LOG_WARN("[Havel] reload failed, %zu binding(s) restored", saved.size());
        return err;
    }
    return "";
#else
    return "built without the havel embed";
#endif
}

std::string Server::ipcSetGamma(const std::string& args) {
    float gamma = m_ipcServer->extractJsonFloat(args, "gamma", 1.0f);
    setGamma(gamma);
    return m_ipcServer->createSuccessResponse("Gamma set to " + std::to_string(gamma));
}

std::string Server::ipcSetTemperature(const std::string& args) {
    int kelvin = m_ipcServer->extractJsonInt(args, "temperature", 6500);
    setTemperature(kelvin);
    return m_ipcServer->createSuccessResponse("Temperature set to " + std::to_string(kelvin) + "K");
}

std::string Server::ipcSetBrightness(const std::string& args) {
    float brightness = m_ipcServer->extractJsonFloat(args, "brightness", 1.0f);
    setBrightness(brightness);
    return m_ipcServer->createSuccessResponse("Brightness set to " + std::to_string(brightness));
}

std::string Server::ipcGetDisplaySettings() {
    JsonObject settings;
    settings["gamma"] = m_displaySettings.gamma();
    settings["temperature"] = m_displaySettings.temperature();
    settings["brightness"] = m_displaySettings.brightness();
    settings["grayscale"] = m_displaySettings.isGrayscaleEnabled();
    settings["negative"] = m_displaySettings.isNegativeEnabled();
    return m_ipcServer->jsonToString(settings);
}

// ============================================================================
// File manager integration (uses havel::FileManager; paths are validated:
// no traversal, no shell metacharacters - spawn never uses a shell either)
// ============================================================================

namespace {
// Expand a leading ~ to HOME, validate: non-empty, no "..", no shell
// metacharacters. Returns false for anything that would escape or inject.
bool fileSafePath(const std::string& in, std::string* out) {
    std::string p = in;
    if (p.empty()) return false;
    if (p[0] == '~') {
        const char* home = getenv("HOME");
        if (!home) return false;
        p = std::string(home) + p.substr(1);
    }
    if (p.find("..") != std::string::npos) return false;
    static const char* bad = "|&;$(){}[]<>*'\"\\`\n\r";
    if (p.find_first_of(bad) != std::string::npos) return false;
    if (p.empty() || p[0] != '/') return false;
    *out = p;
    return true;
}

// fork+exec with a real argv (paths may contain spaces; no shell)
void spawnArgv(const std::vector<std::string>& argv) {
    if (argv.empty()) return;
    pid_t pid = fork();
    if (pid == 0) {
        std::vector<char*> cargv;
        for (const auto& a : argv) cargv.push_back(const_cast<char*>(a.c_str()));
        cargv.push_back(nullptr);
        execvp(cargv[0], cargv.data());
        _exit(127);
    }
}
} // namespace

std::string Server::ipcFileList(const std::string& args) {
    std::string path;
    try {
        auto j = nlohmann::json::parse(args);
        if (!fileSafePath(j.value("path", ""), &path))
            return m_ipcServer->createErrorResponse(-2, "Invalid path");
    } catch (...) {
        return m_ipcServer->createErrorResponse(-1, "Invalid JSON");
    }
    auto files = getFileManager().listDirectory(path);
    if (files.empty() && !getFileManager().fileExists(path))
        return m_ipcServer->createErrorResponse(-3, "Cannot list: " + path);
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& f : files) {
        arr.push_back({{"name", f.name},
                       {"path", f.path},
                       {"mime", f.mimeType},
                       {"size", f.size},
                       {"modified", f.modified},
                       {"is_directory", f.isDirectory},
                       {"hidden", f.isHidden}});
    }
    nlohmann::json r;
    r["files"] = arr;
    r["count"] = files.size();
    return r.dump();
}

std::string Server::ipcFileInfo(const std::string& args) {
    std::string path;
    try {
        auto j = nlohmann::json::parse(args);
        if (!fileSafePath(j.value("path", ""), &path))
            return m_ipcServer->createErrorResponse(-2, "Invalid path");
    } catch (...) {
        return m_ipcServer->createErrorResponse(-1, "Invalid JSON");
    }
    auto& fm = getFileManager();
    if (!fm.fileExists(path))
        return m_ipcServer->createErrorResponse(-3, "No such file: " + path);
    FileInfo f = fm.getFileInfo(path);
    nlohmann::json r = {{"name", f.name},
                        {"path", f.path},
                        {"mime", f.mimeType},
                        {"size", f.size},
                        {"modified", f.modified},
                        {"is_directory", f.isDirectory},
                        {"hidden", f.isHidden}};
    return r.dump();
}

// Shared implementation for the mutating file operations
std::string Server::ipcFileOp(const std::string& op, const std::string& args) {
    // Mutating operations are contained to the user's home and /tmp:
    // the IPC socket is user-owned but file_delete must never become a
    // root-ish primitive on system paths. Containment is exact-or-child
    // (a sibling like /home/user-evil must not match /home/user) and is
    // re-checked on the symlink-resolved path, so a link planted inside
    // HOME cannot point the deletion outside.
    auto underRoot = [](const std::string& p, const std::string& root) {
        return p == root || p.rfind(root + "/", 0) == 0;
    };
    auto contained = [&](const std::string& p) {
        const char* home = getenv("HOME");
        if (home && underRoot(p, home)) return true;
        return underRoot(p, "/tmp");
    };
    // Resolve symlinks and re-check the real path. For not-yet-existing
    // targets (mkdir, copy dst) resolve the parent instead; a failure to
    // resolve means containment cannot be verified, so reject.
    auto resolveContained = [&](const std::string& p) -> bool {
        if (!contained(p)) return false;
        char buf[PATH_MAX];
        if (realpath(p.c_str(), buf)) return contained(buf);
        size_t slash = p.find_last_of('/');
        if (slash == std::string::npos || slash == 0) return false;
        if (!realpath(p.substr(0, slash).c_str(), buf)) return false;
        return contained(buf);
    };
    try {
        auto j = nlohmann::json::parse(args);
        auto get = [&](const char* k) {
            std::string p;
            if (!j.contains(k) || !fileSafePath(j[k].get<std::string>(), &p))
                return std::string();
            return p;
        };
        auto& fm = getFileManager();
        bool ok = false;
        if (op == "copy" || op == "move" || op == "rename") {
            std::string src = get("src");
            std::string dst = get("dst");
            if (src.empty() || dst.empty())
                return m_ipcServer->createErrorResponse(-2, "Missing or invalid src/dst");
            if (!resolveContained(src) || !resolveContained(dst))
                return m_ipcServer->createErrorResponse(-2, "Paths must be under HOME or /tmp");
            if (op == "copy") ok = fm.copyFile(src, dst);
            else if (op == "move") ok = fm.moveFile(src, dst);
            else ok = fm.renameFile(src, dst);
        } else if (op == "delete") {
            std::string p = get("path");
            if (p.empty()) return m_ipcServer->createErrorResponse(-2, "Missing or invalid path");
            if (!resolveContained(p))
                return m_ipcServer->createErrorResponse(-2, "Path must be under HOME or /tmp");
            if (fm.isDirectory(p)) ok = fm.deleteDirectory(p);
            else ok = fm.deleteFile(p);
        } else if (op == "mkdir") {
            std::string p = get("path");
            if (p.empty()) return m_ipcServer->createErrorResponse(-2, "Missing or invalid path");
            if (!resolveContained(p))
                return m_ipcServer->createErrorResponse(-2, "Path must be under HOME or /tmp");
            ok = fm.createDirectory(p);
        } else {
            return m_ipcServer->createErrorResponse(-4, "Unknown op");
        }
        nlohmann::json r;
        r["success"] = ok;
        if (!ok) r["error"] = "operation failed";
        return r.dump();
    } catch (const std::exception& e) {
        return m_ipcServer->createErrorResponse(-1, e.what());
    }
}

std::string Server::ipcOpenPath(const std::string& args) {
    std::string path;
    try {
        auto j = nlohmann::json::parse(args);
        if (!fileSafePath(j.value("path", ""), &path))
            return m_ipcServer->createErrorResponse(-2, "Invalid path");
    } catch (...) {
        return m_ipcServer->createErrorResponse(-1, "Invalid JSON");
    }
    std::string openedWith;
    if (!openPath(path, &openedWith))
        return m_ipcServer->createErrorResponse(-3, "No such path: " + path);
    nlohmann::json r;
    r["success"] = true;
    r["opened_with"] = openedWith;
    return r.dump();
}

bool Server::openPath(const std::string& rawPath, std::string* openedWith) {
    std::string path;
    if (!fileSafePath(rawPath, &path)) return false;
    auto& fm = getFileManager();
    if (!fm.fileExists(path)) return false;

    // Directories open in the project file manager when it is installed
    // next to the compositor, else xdg-open.
    if (fm.isDirectory(path)) {
        std::vector<std::string> candidates;
        std::string self = "/proc/self/exe";
        char buf[4096];
        ssize_t n = readlink(self.c_str(), buf, sizeof(buf) - 1);
        if (n > 0) {
            buf[n] = '\0';
            std::string dir = buf;
            size_t slash = dir.rfind('/');
            if (slash != std::string::npos) {
                dir = dir.substr(0, slash);
                candidates.push_back(dir + "/havel-filemanager");
                candidates.push_back(dir + "/../src/shell/filemanager/havel-filemanager");
            }
        }
        for (const auto& c : candidates) {
            if (access(c.c_str(), X_OK) == 0) {
                spawnArgv({c, path});
                LOG_INFO("[Files] Opened directory %s in %s", path.c_str(), c.c_str());
                if (openedWith) *openedWith = c;
                return true;
            }
        }
        spawnArgv({"xdg-open", path});
        LOG_WARN("[Files] no havel-filemanager found; xdg-open used for %s", path.c_str());
        if (openedWith) *openedWith = "xdg-open";
        return true;
    }

    spawnArgv({"xdg-open", path});
    LOG_INFO("[Files] Opened %s with xdg-open", path.c_str());
    if (openedWith) *openedWith = "xdg-open";
    return true;
}

std::string Server::ipcZoom(const std::string& args) {
    // zoom {"action":"in|out|reset|toggle|push_start|push_end|mode|status|configure",
    //        "output":0, "mode":"fullscreen|lens|strip|custom", ...geometry keys}
    try {
        auto j = nlohmann::json::parse(args);
        if (!j.contains("action"))
            return m_ipcServer->createErrorResponse(-5, "Missing action");
        std::string action = j["action"].get<std::string>();

        // Target output: explicit index, "all", or primary by default.
        std::vector<int> targets;
        if (j.contains("output")) {
            if (j["output"].is_string() && j["output"].get<std::string>() == "all") {
                for (int i = 0; i < kMaxZoomOutputs; i++) targets.push_back(i);
            } else if (j["output"].is_number_integer()) {
                targets.push_back(j["output"].get<int>());
            }
        }
        if (targets.empty()) targets.push_back(0);

        if (action == "configure") {
            zoomConfigure(j.dump());
        } else if (action == "mode") {
            if (j.contains("mode")) {
                std::string m = j["mode"].get<std::string>();
                int mode = m == "lens" ? 1 : m == "strip" ? 2 : m == "custom" ? 3 : 0;
                zoomSetMode(mode);
            } else {
                zoomCycleMode();
            }
        } else if (action == "status") {
            return zoomStatusJson();
        } else {
            for (int idx : targets) {
                if (action == "in") zoomIn(idx);
                else if (action == "out") zoomOut(idx);
                else if (action == "reset") zoomReset(idx);
                else if (action == "toggle") zoomToggle(idx);
                else if (action == "push_start") zoomPushStart(idx);
                else if (action == "push_end") zoomPushEnd(idx);
                else return m_ipcServer->createErrorResponse(-5, "Unknown action: " + action);
            }
        }
        return zoomStatusJson();
    } catch (const std::exception& e) {
        return m_ipcServer->createErrorResponse(-1, e.what());
    }
}

std::string Server::ipcGetPlugins() {
    JsonArray plugins;
    for (const auto& plugin : m_pluginManager.plugins()) {
        if (!plugin) continue;
        JsonObject p;
        p["name"] = plugin->name();
        p["enabled"] = m_pluginManager.isPluginEnabled(plugin->name());
        plugins.push_back(p);
    }
    JsonObject result;
    result["plugins"] = plugins;
    return m_ipcServer->jsonToString(result);
}

std::string Server::ipcNotify(const std::string& args) {
    std::string summary = m_ipcServer->extractJsonString(args, "summary");
    std::string body = m_ipcServer->extractJsonString(args, "body");
    std::string app = m_ipcServer->extractJsonString(args, "app", "havel-wm");
    int timeout = m_ipcServer->extractJsonInt(args, "timeout", 5000);
    if (summary.empty()) {
        return m_ipcServer->createErrorResponse(-5, "Missing summary");
    }
    if (!m_notificationDaemon) {
        m_notificationDaemon = std::make_unique<DbusNotificationDaemon>();
        if (!m_notificationDaemon->initialize()) {
            m_notificationDaemon.reset();
            return m_ipcServer->createErrorResponse(-5, "Notification daemon unavailable");
        }
    }
    uint32_t id = m_notificationDaemon->showDbusNotification(app, summary, body, {}, timeout);
    JsonObject result;
    result["id"] = id;
    return m_ipcServer->jsonToString(result);
}

std::string Server::ipcCloseNotification(const std::string& args) {
    int id = m_ipcServer->extractJsonInt(args, "id", -1);
    if (id < 0) {
        return m_ipcServer->createErrorResponse(-5, "Invalid notification id");
    }
    if (!m_notificationDaemon || !m_notificationDaemon->isRunning()) {
        return m_ipcServer->createErrorResponse(-5, "Notification daemon not running");
    }
    m_notificationDaemon->closeDbusNotification(static_cast<uint32_t>(id));
    return m_ipcServer->createSuccessResponse("Notification closed");
}


std::string Server::ipcGetConfig() {
    JsonObject config;
    config["config_path"] = "~/.config/havel-wm/plugins.json";
    config["plugin_count"] = m_pluginManager.plugins().size();
    config["initialized"] = m_pluginManager.isInitialized();
    return m_ipcServer->jsonToString(config);
}

std::string Server::ipcGetWindow(const std::string& args) {
    int id = m_ipcServer->extractJsonInt(args, "id", -1);
    Window* window = (id >= 0) ? m_coreWindowManager.getWindow(static_cast<uint64_t>(id)) : nullptr;
    if (!window) {
        return m_ipcServer->createErrorResponse(-5, "Window not found");
    }
    JsonObject result;
    result["id"] = window->id();
    result["title"] = window->title();
    result["app_id"] = window->appId();
    Rect geom = window->geometry();
    result["x"] = geom.x;
    result["y"] = geom.y;
    result["width"] = geom.w;
    result["height"] = geom.h;
    result["workspace"] = window->workspace();
    result["floating"] = window->isFloating();
    result["minimized"] = window->isMinimized();
    result["maximized"] = window->isMaximized();
    result["fullscreen"] = window->isFullscreen();
    result["always_on_top"] = window->isAlwaysOnTop();
    result["opacity"] = window->opacity();
    return m_ipcServer->jsonToString(result);
}

std::string Server::ipcGetWindowsByApp(const std::string& args) {
    std::string appId = m_ipcServer->extractJsonString(args, "app_id");
    JsonArray windows;
    for (Window* window : m_coreWindowManager.getAllWindows()) {
        if (window->appId() != appId) continue;
        JsonObject w;
        w["id"] = window->id();
        w["title"] = window->title();
        w["app_id"] = window->appId();
        w["workspace"] = window->workspace();
        w["fullscreen"] = window->isFullscreen();
        windows.push_back(w);
    }
    JsonObject result;
    result["windows"] = windows;
    return m_ipcServer->jsonToString(result);
}

std::string Server::ipcSetWindowOpacity(const std::string& args) {
    int id = m_ipcServer->extractJsonInt(args, "id", -1);
    float opacity = m_ipcServer->extractJsonFloat(args, "opacity", 1.0f);
    Window* window = (id >= 0) ? m_coreWindowManager.getWindow(static_cast<uint64_t>(id)) : nullptr;
    if (!window) {
        return m_ipcServer->createErrorResponse(-5, "Window not found");
    }
    window->setOpacity(opacity);
    // Push to the compositor: the model field alone never reached the
    // screen (setViewOpacity routes into the C scene-buffer opacity).
    if (View* view = window->view()) {
        setViewOpacity(view, opacity);
    }
    return m_ipcServer->createSuccessResponse("Window opacity set");
}

std::string Server::ipcSetWindowFullscreen(const std::string& args) {
    int id = m_ipcServer->extractJsonInt(args, "id", -1);
    bool fullscreen = m_ipcServer->extractJsonBool(args, "fullscreen", true);
    Window* window = (id >= 0) ? m_coreWindowManager.getWindow(static_cast<uint64_t>(id)) : nullptr;
    if (!window) {
        return m_ipcServer->createErrorResponse(-5, "Window not found");
    }
    m_coreWindowManager.fullscreenWindow(window, fullscreen);
    return m_ipcServer->createSuccessResponse(fullscreen ? "Window fullscreen enabled" : "Window fullscreen disabled");
}

std::string Server::ipcSetWindowAlwaysOnTop(const std::string& args) {
    int id = m_ipcServer->extractJsonInt(args, "id", -1);
    bool onTop = m_ipcServer->extractJsonBool(args, "on_top", true);
    Window* window = (id >= 0) ? m_coreWindowManager.getWindow(static_cast<uint64_t>(id)) : nullptr;
    if (!window) {
        return m_ipcServer->createErrorResponse(-5, "Window not found");
    }
    window->setAlwaysOnTop(onTop);
    return m_ipcServer->createSuccessResponse(onTop ? "Window always-on-top enabled" : "Window always-on-top disabled");
}

std::string Server::ipcGetWorkspaces() {
    JsonArray workspaces;
    for (uint32_t i = 0; i < workspaceCount(); ++i) {
        JsonObject ws;
        ws["id"] = i;
        ws["active"] = (i == m_activeWorkspace);
        auto windowsOnWs = m_coreWindowManager.getWindowsOnWorkspace(i);
        JsonArray windowIds;
        for (Window* window : windowsOnWs) {
            windowIds.push_back(window->id());
        }
        ws["window_count"] = windowIds.size();
        ws["windows"] = windowIds;
        workspaces.push_back(ws);
    }
    JsonObject result;
    result["workspaces"] = workspaces;
    result["active"] = m_activeWorkspace;
    return m_ipcServer->jsonToString(result);
}

std::string Server::ipcWorkspaceStep(int direction) {
    uint32_t target;
    if (direction > 0) {
        target = (m_activeWorkspace + 1) % workspaceCount();
    } else {
        target = (m_activeWorkspace == 0) ? (workspaceCount() - 1) : (m_activeWorkspace - 1);
    }
    setActiveWorkspace(target);
    return m_ipcServer->createSuccessResponse("Switched to workspace " + std::to_string(target));
}

std::string Server::ipcMoveToWorkspace(const std::string& args) {
    int windowId = m_ipcServer->extractJsonInt(args, "window_id", -1);
    int workspace = m_ipcServer->extractJsonInt(args, "workspace", -1);
    if (windowId < 0 || workspace < 0 || static_cast<uint32_t>(workspace) >= workspaceCount()) {
        return m_ipcServer->createErrorResponse(-5, "Invalid window_id or workspace");
    }
    Window* window = m_coreWindowManager.getWindow(static_cast<uint64_t>(windowId));
    if (!window) {
        return m_ipcServer->createErrorResponse(-5, "Window not found");
    }
    // Route through the one movement path so the compositor-side view
    // follows the model. Calling the core manager alone only changed
    // Window::workspace() and left the window on screen in its old
    // workspace.
    if (View* view = window->view()) {
        moveViewToWorkspace(view, static_cast<uint32_t>(workspace));
    } else {
        m_coreWindowManager.moveWindowToWorkspace(window, static_cast<uint32_t>(workspace));
    }
    return m_ipcServer->createSuccessResponse("Window moved to workspace " + std::to_string(workspace));
}

void Server::lockScreen() {
    if (m_screenLock) m_screenLock->lock();
}

void Server::unlockScreen() {
    if (m_screenLock) m_screenLock->unlock();
}

void Server::checkIdleLock() {
    if (m_idleLockTimeoutMs == 0 || !m_screenLock) return;
    if (m_screenLock->isLocked()) return;
    auto now = (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    if (now - m_lastInputTimeMs >= m_idleLockTimeoutMs) {
        LOG_INFO("[ScreenLock] idle timeout (%ums) reached, locking", m_idleLockTimeoutMs);
        lockScreen();
    }
}

bool Server::isScreenLocked() const {
    return m_screenLock && m_screenLock->isLocked();
}

bool Server::setWallpaperFromFile(const std::string& path) {
    if (path.empty()) {
        if (m_nativeHandle) {
            havel_wlr_set_wallpaper((struct havel_wlr_server*)m_nativeHandle,
                                     nullptr, 0, 0);
        }
        return true;
    }
    std::vector<uint32_t> pixels;
    int w = 0, h = 0;
    if (!loadPngPixels(path, pixels, &w, &h)) {
        LOG_ERROR("[Wallpaper] cannot decode '%s'", path.c_str());
        return false;
    }
    if (!m_nativeHandle) return false;
    havel_wlr_set_wallpaper((struct havel_wlr_server*)m_nativeHandle,
                            pixels.data(), w, h);
    return true;
}

std::string Server::ipcGetVersion() {
    JsonObject version;
    version["name"] = "Havel WM";
    version["version"] = "0.1.0";
    version["wlroots"] = "0.20";
    return m_ipcServer->jsonToString(version);
}

std::string Server::ipcGetStats() {
    auto uptimeMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - m_startTime).count();
    JsonObject stats;
    stats["uptime_ms"] = (uint64_t)uptimeMs;
    stats["window_count"] = m_coreWindowManager.getAllWindows().size();
    stats["workspace_count"] = workspaceCount();
    return m_ipcServer->jsonToString(stats);
}

} // namespace havel
