#pragma once

#include <wm/Types.hpp>
#include <wm/Workspace.hpp>
#include <wm/Focus.hpp>
#include <wm/Animator.hpp>
#include <wm/bridge.h>
#include <wm/DisplaySettings.hpp>
#include <wm/render/OverlayRenderer.hpp>
#include <wm/plugins/PluginManager.hpp>
#include <input/KeybindingManager.hpp>
#include <input/TextInputManager.hpp>
#include <shell/WindowManager.hpp>
#include <shell/IPCServer.hpp>
#include "core/CoreWindowManager.hpp"
#include "scene/SceneGraph.hpp"
#include <memory>
#include <array>
#include <unordered_map>

// Havel language host (global-namespace class from HavelHost.hpp).
// Declared at GLOBAL scope: an elaborated 'class HavelHost*' inside
// namespace havel would forward-declare a NEW havel::HavelHost instead
// of referring to this one.
class HavelHost;

namespace havel {

class DbusNotificationDaemon;

// Forward declarations for C bridge
struct wlr_xdg_surface;
struct wlr_xwayland_surface;

/**
 * Server is the main compositor state machine.
 * Owned by C++ layer, called from C bridge.
 */
class Server {
public:
    Server();
    ~Server();

    // Workspace management
    Workspace* activeWorkspace() const;
    void setActiveWorkspace(uint32_t id);
    void workspaceStep(bool backwards);
    void workspaceStepTo(uint32_t target);
    void workspaceToggleTiling();

    // View lifecycle (called from C bridge)
    // Returns raw pointer - C++ owns View, C stores opaque handle
    View* createXdgView(void* c_view, uint32_t workspace_id, const char* appId, const char* title);
    View* createXwaylandView(void* xwaylandSurface);
    void onViewMapped(View* view);
    void onViewUnmapped(View* view);
    // Title change from the C bridge: syncs the core Window (which the
    // shell projection reads) and re-broadcasts to IPC listeners.
    void onViewTitleChanged(View* view, const std::string& title);
    void onViewDestroyed(View* view);

    // Input handling - returns true if consumed by compositor
    bool handleKey(uint32_t keycode, bool pressed, uint32_t modifiers, uint32_t keysym, char key_char, const char* utf8);
    void handlePointerButton(uint32_t button, bool pressed, double x, double y);
    void handlePointerMotion(double x, double y);
    
    // Cursor position (for HotCorners, etc.)
    double cursorX() const { return m_cursorX; }
    double cursorY() const { return m_cursorY; }

    // Focus
    void focusView(View* view);
    void focusNextMru(bool backwards = false);

    // Window enumeration (for Alt-Tab, Overview, etc.)
    std::vector<View*> getAllViews() const;
    std::vector<View*> getViewsInWorkspace(uint32_t workspaceId) const;
    View* getFocusedView() const;
    View* getViewById(uint64_t id) const;

    // Window metadata
    std::string getViewAppId(View* view) const;
    std::string getViewTitle(View* view) const;

    // Layout
    void arrangeWorkspace(uint32_t id);

    // Output geometry (set from C side)
    void setOutputGeometry(uint32_t workspaceId, const Rect& geom);
    Rect outputGeometry(uint32_t workspaceId) const;
    int getOutputWidth() const;
    int getOutputHeight() const;
    void scheduleRedraw();

    // Native server handle for C bridge
    void setNativeHandle(void* handle) {
        m_nativeHandle = handle;
        m_displaySettings.setApplyHandle(reinterpret_cast<havel_wlr_server_t*>(handle));
    }
    void* nativeHandle() const { return m_nativeHandle; }

    // Overlay layer for plugin rendering
    void setOverlayLayer(void* layer) {
        m_overlayLayer = layer;
        // Create overlay renderer for plugins, backed by the scene graph
        // (the layer tree is the compositing surface in wlroots 0.20).
        if (layer && !m_overlayRenderer) {
            m_overlayRenderer = new OverlayRenderer();
            m_overlayRenderer->setSceneTree(static_cast<wlr_scene_tree_t*>(layer));
            bool initialized = m_overlayRenderer->initialize();
            printf("[Server] OverlayRenderer created and initialized: %s\n", initialized ? "SUCCESS" : "FAILED");
        }
    }
    void* overlayLayer() const { return m_overlayLayer; }
    OverlayRenderer* getOverlayRenderer() {
        if (!m_overlayRenderer) {
            printf("[Server] getOverlayRenderer() returning NULL - not initialized\n");
        }
        return m_overlayRenderer;
    }

    // Text Input Manager (IME)
    void setTextInputManager(void* manager) { m_textInputManager = static_cast<TextInputManager*>(manager); }
    TextInputManager* textInputManager() const { return m_textInputManager; }

    // Animation control
    void setAnimationsEnabled(bool enabled);
    bool animationsEnabled() const { return m_animator.isEnabled(); }
    void updateAnimations();

    // Effect control (global)
    void setGrayscaleEnabled(bool enabled);
    void setNegativeEnabled(bool enabled);
    bool isGrayscaleEnabled() const;
    bool isNegativeEnabled() const;
    void toggleGrayscale();
    void toggleNegative();

    // (Alt-tab, workspace overview and app launcher overlays are owned by
    // their plugins: AltTabPlugin, OverviewPlugin, AppLauncherPlugin.)

    // Plugin management
    PluginManager& pluginManager() { return m_pluginManager; }
    const PluginManager& pluginManager() const { return m_pluginManager; }
    void registerPlugin(std::unique_ptr<Plugin> plugin);

    // Scene graph access
    Scene* sceneGraph() { return m_sceneGraph; }
    const Scene* sceneGraph() const { return m_sceneGraph; }

    // CompositorAPI implementation (for plugins)
    void setViewPosition(View* view, int x, int y, bool animate = true);
    void setViewOpacity(View* view, float alpha);
    void setViewGeometry(View* view, int x, int y, int w, int h);
    
    // Background color (for wallpaper plugin)
    void setBackgroundColor(float r, float g, float b);
    void getBackgroundColor(float* r, float* g, float* b) const;
    
    // Gamma/temperature control
    void setGamma(float gamma);
    void setTemperature(int kelvin);
    void setBrightness(float brightness);

    // Window group management
    void* windowGroupManager() const { return m_windowGroupManager; }
    void setWindowGroupManager(void* manager) { m_windowGroupManager = manager; }

    // Window management (real window tracking and placement)
    havel::CoreWindowManager& coreWindowManager() { return m_coreWindowManager; }
    const havel::CoreWindowManager& coreWindowManager() const { return m_coreWindowManager; }
    void setCoreWindowManagerServer(Server* server) { m_coreWindowManager.setServer(server); }

    // Desktop management
    void* desktopManager() const { return m_desktopManager; }
    void setDesktopManager(void* manager) { m_desktopManager = manager; }

    // IPC server management
    bool startIPCServer(const std::string& socketPath);
    void stopIPCServer();
    bool isIPCServerRunning() const;
    void processIPCEvents();

    // IPC socket fds (listen fd first, then clients) for event-loop
    // registration in the wlr bridge - fd sources are the reliable IPC
    // wakeup (see wlr_bridge.c ipc fd sources note).
    int collectIpcFds(int* out, int max);

    // IPC command handlers (extracted from startIPCServer lambdas, phase 2).
    // Each returns the JSON-RPC result string for its command.
    std::string ipcSetGamma(const std::string& args);
    std::string ipcSetTemperature(const std::string& args);
    std::string ipcSetBrightness(const std::string& args);
    std::string ipcGetDisplaySettings();
    std::string ipcGetPlugins();
    std::string ipcNotify(const std::string& args);
    std::string ipcCloseNotification(const std::string& args);
    std::string ipcReloadConfig();
    std::string ipcGetConfig();
    std::string ipcGetWindow(const std::string& args);
    std::string ipcGetWindowsByApp(const std::string& args);
    std::string ipcSetWindowOpacity(const std::string& args);
    std::string ipcSetWindowFullscreen(const std::string& args);
    std::string ipcSetWindowAlwaysOnTop(const std::string& args);
    std::string ipcGetWorkspaces();
    std::string ipcWorkspaceStep(int direction);
    std::string ipcMoveToWorkspace(const std::string& args);
    std::string ipcGetVersion();
    std::string ipcGetStats();

    // Shell window manager accessor (id-based operations; the projection
    // the IPC commands use)
    havel::WindowManager& windowManager() { return m_windowManager; }

    // Generic command spawn (fork+exec, no shell) - same path
    // spawnTerminal() uses
    void spawnCommand(const char* cmd);

#ifdef HAVE_HAVEL_LANG
    // Havel language embedding (scripts as compositor commands)
    ::HavelHost* havelHost() { return m_havelHost; }
    bool initHavelHost();  // create + load config.hv
    bool registerHavelKeybinding(uint32_t modifiers, uint32_t keysym,
                                 const char* name, std::function<void()> callback);
#endif

private:
    std::array<std::unique_ptr<Workspace>, WORKSPACE_COUNT> m_workspaces;
    uint32_t m_activeWorkspace = 0;
    FocusManager m_focusManager;
    havel::WindowManager m_windowManager;  // Shell window manager (IPC)
    havel::CoreWindowManager m_coreWindowManager;  // Real window management
    Scene* m_sceneGraph = nullptr;  // True scene graph

    // Keybindings
    KeybindingManager m_keybindingManager;

    // Text Input (IME)
    TextInputManager* m_textInputManager = nullptr;

#ifdef HAVE_HAVEL_LANG
    ::HavelHost* m_havelHost = nullptr;
#endif

    // Overlay UIs (alt-tab, overview, launcher) are plugin-owned:
    // AltTabPlugin, OverviewPlugin, AppLauncherPlugin.
    PluginManager m_pluginManager;
    
    // IPC server for external tool communication
    std::unique_ptr<IPCServer> m_ipcServer;

    // D-Bus notification daemon (lazily created by the IPC notify command)
    std::unique_ptr<DbusNotificationDaemon> m_notificationDaemon;

    // Overlay scene layer (for Alt-Tab, Overview, etc.)
    void* m_overlayLayer = nullptr;  // wlr_scene_tree*
    OverlayRenderer* m_overlayRenderer = nullptr;  // For plugin overlay rendering

    void* m_nativeHandle = nullptr;
    std::unordered_map<uint32_t, Rect> m_outputGeoms;
    Animator m_animator;
    
    // Background color (for wallpaper plugin)
    float m_bgColorR = 0.1f;
    float m_bgColorG = 0.1f;
    float m_bgColorB = 0.15f;

    // Window group manager
    void* m_windowGroupManager = nullptr;

    // Desktop manager
    void* m_desktopManager = nullptr;

    // Display settings (gamma/temperature/brightness/effects), extracted
    // from Server in phase 3. Public setters below forward to it.
    DisplaySettings m_displaySettings;

    // Start time for uptime calculation
    std::chrono::steady_clock::time_point m_startTime = std::chrono::steady_clock::now();

    // Grab state for mouse operations
    enum class GrabMode : uint8_t {
        None = 0,
        Move,
        Resize,
        Draw
    };
    
    struct GrabState {
        View* view = nullptr;
        GrabMode mode = GrabMode::None;
        uint32_t button = 0;
        double startX = 0;
        double startY = 0;
        int startViewX = 0;
        int startViewY = 0;
        int startViewW = 0;
        int startViewH = 0;
    } m_grab;
    
    // Cursor position tracking (for HotCorners, etc.)
    double m_cursorX = 0;
    double m_cursorY = 0;

    // Animation state for views
    struct ViewAnimationState {
        int currentX = 0, currentY = 0;
        int currentW = 0, currentH = 0;
        float currentAlpha = 1.0f;
        float currentScale = 1.0f;
    };
    std::unordered_map<View*, ViewAnimationState> m_viewAnimState;

    // Helper methods
    void spawnTerminal();
    void spawnRofi();
    void spawnBrowser();
    void spawnFileManager();
    void closeFocusedWindow();
    void toggleMaximize();
    void toggleFloating();
    void minimizeWindow();
    void toggleFullscreen();
    void toggleAlwaysOnTop();
    void focusFirstLastView(bool first);
    void moveViewToWorkspace(uint32_t ws);
    void moveViewToWorkspaceRelative(bool next);
    void quit();

    // Keybinding registration
    void registerKeybindings();

    // View manipulation through C callbacks (with optional animation)
    // Note: setViewPosition and setViewOpacity are public for CompositorAPI
    void setViewSize(View* view, int w, int h, bool animate = true);
    void focusViewNative(View* view);
    void raiseView(View* view);
    Rect getViewGeometry(View* view);

    // Animation helpers
    void animateViewFade(View* view, float from, float to);
    void animateViewMove(View* view, int fromX, int fromY, int toX, int toY);
    void animateViewResize(View* view, int fromW, int fromH, int toW, int toH);
    void animateViewScale(View* view, float from, float to);
};

} // namespace havel
