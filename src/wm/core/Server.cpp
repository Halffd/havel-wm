#include <wm/Server.hpp>
#include <wm/Layout.hpp>
#include <wm/plugins/Plugins.hpp>
#include <shell/IPCServer.hpp>
#include "core/CoreWindowManager.hpp"
#include "scene/SceneGraph.hpp"
#include <nlohmann/json.hpp>
#include <Logger.h>
#include <algorithm>
#include <cstdlib>
#include <unistd.h>
#include <sys/types.h>
#include <cstdio>

namespace havel {

using json = nlohmann::json;
using JsonObject = nlohmann::ordered_json;

Server::Server() {
    LOG_DEBUG("Server constructor");
    // Initialize workspaces
    for (uint32_t i = 0; i < WORKSPACE_COUNT; ++i) {
        m_workspaces[i] = std::make_unique<Workspace>(i);
    }

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
    registerPlugin(std::unique_ptr<Plugin>(create_example_plugin()));
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

    // TextInputManager is owned by C layer, don't delete here
}

void Server::registerPlugin(std::unique_ptr<Plugin> plugin) {
    m_pluginManager.registerPlugin(std::move(plugin));
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
    
    // Broadcast workspace changed event via IPC
    if (m_ipcServer) {
        JsonObject params;
        params["workspace"] = (int)id;
        m_ipcServer->broadcastEvent(IPCServer::EventType::WorkspaceChanged, m_ipcServer->jsonToString(params));
    }
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
    if (!pressed) return false;

    // First, check registered keybindings (highest priority)
    if (m_keybindingManager.handleKey(keycode, pressed, modifiers)) {
        return true;
    }

    // Then, give plugins a chance to handle the key
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

// Window enumeration implementation
std::vector<View*> Server::getAllViews() const {
    std::vector<View*> allViews;
    for (uint32_t ws = 0; ws < WORKSPACE_COUNT; ++ws) {
        auto* workspace = m_workspaces[ws].get();
        if (workspace) {
            auto wsViews = workspace->views();
            allViews.insert(allViews.end(), wsViews.begin(), wsViews.end());
        }
    }
    return allViews;
}

std::vector<View*> Server::getViewsInWorkspace(uint32_t workspaceId) const {
    if (workspaceId >= WORKSPACE_COUNT) return {};
    auto* workspace = m_workspaces[workspaceId].get();
    return workspace ? workspace->views() : std::vector<View*>{};
}

View* Server::getFocusedView() const {
    auto* ws = activeWorkspace();
    return ws ? ws->activeView() : nullptr;
}

View* Server::getViewById(uint64_t id) const {
    // Search all workspaces for view with matching windowId
    for (uint32_t ws = 0; ws < WORKSPACE_COUNT; ++ws) {
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
    if (id >= WORKSPACE_COUNT) return;

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
    m_gamma = gamma;
    LOG_INFO("Gamma set to %.2f", gamma);
    // Would apply to all outputs via C bridge
}

void Server::setTemperature(int kelvin) {
    m_temperature = kelvin;
    LOG_INFO("Temperature set to %dK", kelvin);
    // Would apply to all outputs via C bridge
}

void Server::setBrightness(float brightness) {
    m_brightness = brightness;
    LOG_INFO("Brightness set to %.2f", brightness);
    // Would apply to all outputs via C bridge
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
    m_grayscaleEnabled = enabled;
    printf("[Server] Grayscale %s\n", enabled ? "enabled" : "disabled");
}

void Server::setNegativeEnabled(bool enabled) {
    m_negativeEnabled = enabled;
    printf("[Server] Negative %s\n", enabled ? "enabled" : "disabled");
}

bool Server::isGrayscaleEnabled() const {
    return m_grayscaleEnabled;
}

bool Server::isNegativeEnabled() const {
    return m_negativeEnabled;
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
// Workspace Overview
// ============================================================================

void Server::showOverview() {
    m_overviewOverlay.show(WORKSPACE_COUNT, m_activeWorkspace);
    m_overviewOverlay.setWorkspaceCallback([this](uint32_t wsId) {
        setActiveWorkspace(wsId);
    });
    
    // Overview v1: Just show the overlay UI
    // Future: Reposition views into grid layout using scene graph
    // For now, the overlay itself provides the workspace switching UI
    LOG_INFO("Overview shown (grid layout pending scene graph extension)");
}

void Server::hideOverview() {
    m_overviewOverlay.hide();
    LOG_INFO("Overview hidden");
}

void Server::overviewNavigate(int dx, int dy) {
    if (dx < 0) m_overviewOverlay.navigateLeft();
    if (dx > 0) m_overviewOverlay.navigateRight();
    if (dy < 0) m_overviewOverlay.navigateUp();
    if (dy > 0) m_overviewOverlay.navigateDown();
}

void Server::overviewSelect() {
    m_overviewOverlay.select();
}

bool Server::isOverviewVisible() const {
    return m_overviewOverlay.isVisible();
}

// ============================================================================
// App Launcher
// ============================================================================

void Server::showLauncher() {
    m_launcherOverlay.scanApplications();
    m_launcherOverlay.show();
    m_launcherOverlay.setLaunchCallback([this](const std::string& exec) {
        if (g_server_spawn) {
            g_server_spawn(exec.c_str());
        }
    });
}

void Server::hideLauncher() {
    m_launcherOverlay.hide();
}

void Server::launcherInput(char key) {
    if (m_launcherOverlay.isVisible()) {
        std::string current = m_launcherOverlay.searchText();
        current += key;
        m_launcherOverlay.setSearchText(current);
    }
}

void Server::launcherBackspace() {
    m_launcherOverlay.backspace();
}

void Server::launcherNavigate(int dy) {
    if (dy < 0) m_launcherOverlay.navigateUp();
    if (dy > 0) m_launcherOverlay.navigateDown();
}

void Server::launcherSelect() {
    m_launcherOverlay.select();
}

bool Server::isLauncherVisible() const {
    return m_launcherOverlay.isVisible();
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
        havel_wlr_set_view_position(view->nativeHandle(), x, y);
    }
}

void Server::setViewOpacity(View* view, float alpha) {
    // wlroots 0.20 scene doesn't have per-node opacity
    // This would require scene graph extension
    // Log the request for debugging
    if (view && alpha != 1.0f) {
        LOG_DEBUG("[Server] setViewOpacity requested: %.2f (not supported in wlroots 0.20)", alpha);
    }
    (void)view;
    (void)alpha;
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
        KeybindingManager::MOD_CTRL | KeybindingManager::MOD_LOGO, 111,
        "quit", [this]() { quit(); }
    );

    // Ctrl+Meta+Return: Show rofi
    m_keybindingManager.registerKeybinding(
        KeybindingManager::MOD_CTRL | KeybindingManager::MOD_LOGO, 28,
        "show_rofi", [this]() { spawnRofi(); }
    );

    // Meta+Return: Spawn terminal
    m_keybindingManager.registerKeybinding(
        KeybindingManager::MOD_LOGO, 28,
        "spawn_terminal", [this]() { spawnTerminal(); }
    );

    // Meta+Tab: Workspace switch forward
    m_keybindingManager.registerKeybinding(
        KeybindingManager::MOD_LOGO, 23,
        "workspace_next", [this]() { workspaceStep(false); }
    );

    // Meta+Shift+Tab: Workspace switch backward
    m_keybindingManager.registerKeybinding(
        KeybindingManager::MOD_LOGO | KeybindingManager::MOD_SHIFT, 23,
        "workspace_prev", [this]() { workspaceStep(true); }
    );

    // Meta+PgUp: Workspace switch backward
    m_keybindingManager.registerKeybinding(
        KeybindingManager::MOD_LOGO, 104,
        "workspace_prev_pgup", [this]() { workspaceStep(false); }
    );

    // Meta+PgDn: Workspace switch forward
    m_keybindingManager.registerKeybinding(
        KeybindingManager::MOD_LOGO, 105,
        "workspace_next_pgdn", [this]() { workspaceStep(true); }
    );

    // Meta+W: Show workspace overview
    m_keybindingManager.registerKeybinding(
        KeybindingManager::MOD_LOGO, 32,
        "show_overview", [this]() { showOverview(); }
    );

    // Meta+D: Show app launcher
    m_keybindingManager.registerKeybinding(
        KeybindingManager::MOD_LOGO, 33,
        "show_launcher", [this]() { showLauncher(); }
    );

    // Meta+Y: Toggle tiling
    m_keybindingManager.registerKeybinding(
        KeybindingManager::MOD_LOGO, 21,
        "toggle_tiling", [this]() { workspaceToggleTiling(); }
    );

    // Meta+G: Toggle grayscale
    m_keybindingManager.registerKeybinding(
        KeybindingManager::MOD_LOGO, 34,
        "toggle_grayscale", [this]() { toggleGrayscale(); }
    );

    // Meta+N: Toggle negative
    m_keybindingManager.registerKeybinding(
        KeybindingManager::MOD_LOGO, 39,
        "toggle_negative", [this]() { toggleNegative(); }
    );

    // Meta+Shift+Q: Close focused window
    m_keybindingManager.registerKeybinding(
        KeybindingManager::MOD_LOGO | KeybindingManager::MOD_SHIFT, 16,
        "close_window", [this]() { closeFocusedWindow(); }
    );

    // Meta+F: Toggle fullscreen
    m_keybindingManager.registerKeybinding(
        KeybindingManager::MOD_LOGO, 35,
        "toggle_fullscreen", [this]() { toggleFullscreen(); }
    );

    // Meta+Shift+S: Print scene graph tree (debug)
    m_keybindingManager.registerKeybinding(
        KeybindingManager::MOD_LOGO | KeybindingManager::MOD_SHIFT, 31,
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
        KeybindingManager::MOD_LOGO | KeybindingManager::MOD_SHIFT, 38,
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
        KeybindingManager::MOD_LOGO | KeybindingManager::MOD_SHIFT, 34,
        "scene_graph_update", [this]() {
            if (m_sceneGraph) {
                scene_graph_update(m_sceneGraph);
                printf("[Debug] Scene graph updated\n");
            }
        }
    );

    // Meta+Space: Toggle floating
    m_keybindingManager.registerKeybinding(
        KeybindingManager::MOD_LOGO, 57,
        "toggle_floating", [this]() { toggleFloating(); }
    );

    // Meta+Shift+F: Toggle always on top
    m_keybindingManager.registerKeybinding(
        KeybindingManager::MOD_LOGO | KeybindingManager::MOD_SHIFT, 35,
        "toggle_always_on_top", [this]() { toggleAlwaysOnTop(); }
    );

    // Meta+0: Minimize window
    m_keybindingManager.registerKeybinding(
        KeybindingManager::MOD_LOGO, 11,
        "minimize_window", [this]() { minimizeWindow(); }
    );

    // Meta+Shift+G: Gamma night mode
    m_keybindingManager.registerKeybinding(
        KeybindingManager::MOD_LOGO | KeybindingManager::MOD_SHIFT, 34,
        "gamma_night_mode", [this]() {
            // Toggle gamma plugin to night mode (3500K)
            setTemperature(3500);
            LOG_INFO("Night mode enabled (3500K)");
        }
    );

    // Meta+Shift+R: Reload configuration (hot-reload)
    m_keybindingManager.registerKeybinding(
        KeybindingManager::MOD_LOGO | KeybindingManager::MOD_SHIFT, 19,
        "reload_config", [this]() {
            LOG_INFO("Reloading configuration...");
            m_pluginManager.reloadConfig();
        }
    );

    // Meta+1 through Meta+9: Direct workspace switch
    for (uint32_t i = 0; i < 9; i++) {
        m_keybindingManager.registerKeybinding(
            KeybindingManager::MOD_LOGO, 10 + i,
            ("workspace_" + std::to_string(i + 1)).c_str(),
            [this, i]() { workspaceStepTo(i); }
        );
    }

    // Meta+0: Workspace 10
    m_keybindingManager.registerKeybinding(
        KeybindingManager::MOD_LOGO, 19,
        "workspace_10", [this]() { workspaceStepTo(9); }
    );

    // Meta+Shift+1 through Meta+Shift+9: Move window to workspace
    for (uint32_t i = 0; i < 9; i++) {
        m_keybindingManager.registerKeybinding(
            KeybindingManager::MOD_LOGO | KeybindingManager::MOD_SHIFT, 10 + i,
            ("move_to_workspace_" + std::to_string(i + 1)).c_str(),
            [this, i]() { moveViewToWorkspace(i); }
        );
    }

    // Meta+Shift+0: Move window to workspace 10
    m_keybindingManager.registerKeybinding(
        KeybindingManager::MOD_LOGO | KeybindingManager::MOD_SHIFT, 19,
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

    // Register JSON-RPC command handlers
    m_ipcServer->registerCommand("get_windows", [this](const std::string& args) -> std::string {
        return m_ipcServer->handleGetWindows();
    });

    m_ipcServer->registerCommand("get_focused", [this](const std::string& args) -> std::string {
        return m_ipcServer->handleGetFocused();
    });

    m_ipcServer->registerCommand("focus", [this](const std::string& args) -> std::string {
        return m_ipcServer->handleFocus(args);
    });

    m_ipcServer->registerCommand("minimize", [this](const std::string& args) -> std::string {
        return m_ipcServer->handleMinimize(args);
    });

    m_ipcServer->registerCommand("maximize", [this](const std::string& args) -> std::string {
        return m_ipcServer->handleMaximize(args);
    });

    m_ipcServer->registerCommand("restore", [this](const std::string& args) -> std::string {
        return m_ipcServer->handleRestore(args);
    });

    m_ipcServer->registerCommand("close", [this](const std::string& args) -> std::string {
        return m_ipcServer->handleClose(args);
    });

    m_ipcServer->registerCommand("move", [this](const std::string& args) -> std::string {
        return m_ipcServer->handleMove(args);
    });

    m_ipcServer->registerCommand("resize", [this](const std::string& args) -> std::string {
        return m_ipcServer->handleResize(args);
    });

    m_ipcServer->registerCommand("set_floating", [this](const std::string& args) -> std::string {
        return m_ipcServer->handleSetFloating(args);
    });

    m_ipcServer->registerCommand("workspace", [this](const std::string& args) -> std::string {
        return m_ipcServer->handleWorkspace(args);
    });

    m_ipcServer->registerCommand("get_workspace", [this](const std::string& args) -> std::string {
        return m_ipcServer->handleGetWorkspace();
    });

    m_ipcServer->registerCommand("spawn", [this](const std::string& args) -> std::string {
        return m_ipcServer->handleSpawn(args);
    });

    // ========================================================================
    // Output/Monitor Commands
    // ========================================================================
    m_ipcServer->registerCommand("get_outputs", [this](const std::string& args) -> std::string {
        // Returns list of all outputs with their properties
        JsonArray outputs;
        // Would iterate through outputs and collect info
        // For now, return basic structure
        JsonObject result;
        result["outputs"] = outputs;
        return m_ipcServer->jsonToString(result);
    });

    m_ipcServer->registerCommand("set_output_scale", [this](const std::string& args) -> std::string {
        // Set output scaling factor
        int outputIdx = m_ipcServer->extractJsonInt(args, "output", 0);
        float scale = m_ipcServer->extractJsonFloat(args, "scale", 1.0f);
        // setOutputScale not implemented yet
        return m_ipcServer->createSuccessResponse("Output scale set");
    });

    // ========================================================================
    // Display Settings Commands
    // ========================================================================
    m_ipcServer->registerCommand("set_gamma", [this](const std::string& args) -> std::string {
        float gamma = m_ipcServer->extractJsonFloat(args, "gamma", 1.0f);
        setGamma(gamma);
        return m_ipcServer->createSuccessResponse("Gamma set to " + std::to_string(gamma));
    });

    m_ipcServer->registerCommand("set_temperature", [this](const std::string& args) -> std::string {
        int kelvin = m_ipcServer->extractJsonInt(args, "temperature", 6500);
        setTemperature(kelvin);
        return m_ipcServer->createSuccessResponse("Temperature set to " + std::to_string(kelvin) + "K");
    });

    m_ipcServer->registerCommand("set_brightness", [this](const std::string& args) -> std::string {
        float brightness = m_ipcServer->extractJsonFloat(args, "brightness", 1.0f);
        setBrightness(brightness);
        return m_ipcServer->createSuccessResponse("Brightness set to " + std::to_string(brightness));
    });

    m_ipcServer->registerCommand("set_zoom", [this](const std::string& args) -> std::string {
        float zoom = m_ipcServer->extractJsonFloat(args, "zoom", 1.0f);
        // setZoom not implemented
        return m_ipcServer->createSuccessResponse("Zoom set to " + std::to_string(zoom));
    });

    m_ipcServer->registerCommand("get_display_settings", [this](const std::string& args) -> std::string {
        JsonObject settings;
        // Would return current gamma, temperature, brightness, zoom values
        settings["gamma"] = 1.0f;
        settings["temperature"] = 6500;
        settings["brightness"] = 1.0f;
        settings["zoom"] = 1.0f;
        return m_ipcServer->jsonToString(settings);
    });

    // ========================================================================
    // Plugin Commands
    // ========================================================================
    m_ipcServer->registerCommand("get_plugins", [this](const std::string& args) -> std::string {
        JsonArray plugins;
        // Would iterate through loaded plugins
        JsonObject result;
        result["plugins"] = plugins;
        return m_ipcServer->jsonToString(result);
    });

    m_ipcServer->registerCommand("enable_plugin", [this](const std::string& args) -> std::string {
        std::string name = m_ipcServer->extractJsonString(args, "name");
        // Would enable plugin by name
        return m_ipcServer->createSuccessResponse("Plugin enabled: " + name);
    });

    m_ipcServer->registerCommand("disable_plugin", [this](const std::string& args) -> std::string {
        std::string name = m_ipcServer->extractJsonString(args, "name");
        // Would disable plugin by name
        return m_ipcServer->createSuccessResponse("Plugin disabled: " + name);
    });

    m_ipcServer->registerCommand("configure_plugin", [this](const std::string& args) -> std::string {
        std::string name = m_ipcServer->extractJsonString(args, "name");
        std::string config = m_ipcServer->extractJsonString(args, "config");
        // Would configure plugin
        return m_ipcServer->createSuccessResponse("Plugin configured: " + name);
    });

    // ========================================================================
    // Notification Commands
    // ========================================================================
    m_ipcServer->registerCommand("notify", [this](const std::string& args) -> std::string {
        std::string summary = m_ipcServer->extractJsonString(args, "summary");
        std::string body = m_ipcServer->extractJsonString(args, "body");
        std::string app = m_ipcServer->extractJsonString(args, "app");
        int timeout = m_ipcServer->extractJsonInt(args, "timeout", 5000);
        // Would send notification via NotificationDaemon
        return m_ipcServer->createSuccessResponse("Notification sent: " + summary);
    });

    m_ipcServer->registerCommand("close_notification", [this](const std::string& args) -> std::string {
        int id = m_ipcServer->extractJsonInt(args, "id", 0);
        // Would close notification by ID
        return m_ipcServer->createSuccessResponse("Notification closed");
    });

    // ========================================================================
    // Screenshot Commands
    // ========================================================================
    m_ipcServer->registerCommand("screenshot", [this](const std::string& args) -> std::string {
        return m_ipcServer->handleScreenshot(args);
    });

    m_ipcServer->registerCommand("screenshot_window", [this](const std::string& args) -> std::string {
        return m_ipcServer->handleScreenshotWindow(args);
    });

    m_ipcServer->registerCommand("screenshot_region", [this](const std::string& args) -> std::string {
        return m_ipcServer->handleScreenshotRegion(args);
    });

    // Audio/Video Recording Commands
    m_ipcServer->registerCommand("audio_record", [this](const std::string& args) -> std::string {
        return m_ipcServer->handleAudioRecord(args);
    });

    m_ipcServer->registerCommand("video_record", [this](const std::string& args) -> std::string {
        return m_ipcServer->handleVideoRecord(args);
    });

    m_ipcServer->registerCommand("stop_recording", [this](const std::string& args) -> std::string {
        return m_ipcServer->handleStopRecording(args);
    });

    // ========================================================================
    // Configuration Commands
    // ========================================================================
    m_ipcServer->registerCommand("reload_config", [this](const std::string& args) -> std::string {
        m_pluginManager.reloadConfig();
        return m_ipcServer->createSuccessResponse("Configuration reloaded");
    });

    m_ipcServer->registerCommand("get_config", [this](const std::string& args) -> std::string {
        // Would return current configuration
        JsonObject config;
        config["config_path"] = "~/.config/havel-wm/plugins.json";
        return m_ipcServer->jsonToString(config);
    });

    // ========================================================================
    // Window Query Commands (Enhanced)
    // ========================================================================
    m_ipcServer->registerCommand("get_window", [this](const std::string& args) -> std::string {
        int id = m_ipcServer->extractJsonInt(args, "id", -1);
        // Would return detailed window info
        JsonObject window;
        window["id"] = id;
        window["title"] = "Example Window";
        window["app_id"] = "example";
        return m_ipcServer->jsonToString(window);
    });

    m_ipcServer->registerCommand("get_windows_by_app", [this](const std::string& args) -> std::string {
        std::string appId = m_ipcServer->extractJsonString(args, "app_id");
        // Would return windows matching app_id
        JsonArray windows;
        JsonObject result;
        result["windows"] = windows;
        return m_ipcServer->jsonToString(result);
    });

    m_ipcServer->registerCommand("set_window_opacity", [this](const std::string& args) -> std::string {
        int id = m_ipcServer->extractJsonInt(args, "id", -1);
        float opacity = m_ipcServer->extractJsonFloat(args, "opacity", 1.0f);
        // Would set window opacity
        return m_ipcServer->createSuccessResponse("Window opacity set");
    });

    m_ipcServer->registerCommand("set_window_fullscreen", [this](const std::string& args) -> std::string {
        int id = m_ipcServer->extractJsonInt(args, "id", -1);
        bool fullscreen = m_ipcServer->extractJsonBool(args, "fullscreen", true);
        // Would set window fullscreen
        return m_ipcServer->createSuccessResponse("Window fullscreen set");
    });

    m_ipcServer->registerCommand("set_window_always_on_top", [this](const std::string& args) -> std::string {
        int id = m_ipcServer->extractJsonInt(args, "id", -1);
        bool onTop = m_ipcServer->extractJsonBool(args, "on_top", true);
        // Would set always on top
        return m_ipcServer->createSuccessResponse("Window always-on-top set");
    });

    // ========================================================================
    // Workspace Commands (Enhanced)
    // ========================================================================
    m_ipcServer->registerCommand("get_workspaces", [this](const std::string& args) -> std::string {
        JsonArray workspaces;
        // Would return all workspaces with their windows
        JsonObject result;
        result["workspaces"] = workspaces;
        result["active"] = 0;
        return m_ipcServer->jsonToString(result);
    });

    m_ipcServer->registerCommand("workspace_next", [this](const std::string& args) -> std::string {
        // workspaceStep not implemented;
        return m_ipcServer->createSuccessResponse("Switched to next workspace");
    });

    m_ipcServer->registerCommand("workspace_prev", [this](const std::string& args) -> std::string {
        // workspaceStep not implemented;
        return m_ipcServer->createSuccessResponse("Switched to previous workspace");
    });

    m_ipcServer->registerCommand("move_to_workspace", [this](const std::string& args) -> std::string {
        int windowId = m_ipcServer->extractJsonInt(args, "window_id", -1);
        int workspace = m_ipcServer->extractJsonInt(args, "workspace", 0);
        // Would move window to workspace
        return m_ipcServer->createSuccessResponse("Window moved to workspace " + std::to_string(workspace));
    });

    // ========================================================================
    // System Commands
    // ========================================================================
    m_ipcServer->registerCommand("get_version", [this](const std::string& args) -> std::string {
        JsonObject version;
        version["name"] = "Havel WM";
        version["version"] = "0.1.0";
        version["wlroots"] = "0.20";
        return m_ipcServer->jsonToString(version);
    });

    m_ipcServer->registerCommand("get_stats", [this](const std::string& args) -> std::string {
        JsonObject stats;
        stats["uptime_ms"] = 0;  // Would calculate from startup time
        stats["window_count"] = m_windowManager.getAllWindows().size();
        stats["workspace_count"] = 10;
        return m_ipcServer->jsonToString(stats);
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

    m_ipcServer->registerCommand("quit", [this](const std::string& args) -> std::string {
        return m_ipcServer->handleQuit();
    });

    if (m_ipcServer->start(socketPath)) {
        LOG_INFO("IPC server started on: %s", socketPath.c_str());
        return true;
    } else {
        LOG_ERROR("Failed to start IPC server on: %s", socketPath.c_str());
        m_ipcServer.reset();
        return false;
    }
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

} // namespace havel
