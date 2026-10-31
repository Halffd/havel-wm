#pragma once

#include <wm/Types.hpp>
#include <ui/lock/ScreenLock.hpp>
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
#include <map>
#include <vector>
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
    // XWayland surfaces route through createXdgView with the X class as
    // appId (wlr_bridge.c server_new_xwayland_surface) - no dedicated
    // factory needed.
    void onViewMapped(View* view);
    void onViewUnmapped(View* view);
    // Title change from the C bridge: syncs the core Window (which the
    // shell projection reads) and re-broadcasts to IPC listeners.
    void onViewTitleChanged(View* view, const std::string& title);
    void onViewAppIdChanged(View* view, const std::string& appId);
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

    // Which output the current overlay pass is drawing for (set from the
    // C side before plugins render). Plugins that target one monitor
    // (Alt-Tab's monitor setting) resolve against this.
    struct OverlayTarget {
        int x = 0, y = 0, w = 0, h = 0;  // output box in scene coordinates
        int index = 0;                   // position in the output list
        bool primary = true;
        bool underCursor = true;
    };
    void setOverlayTarget(int x, int y, int w, int h, int index,
                          bool primary, bool underCursor) {
        m_overlayTarget = OverlayTarget{x, y, w, h, index, primary, underCursor};
    }
    const OverlayTarget& overlayTarget() const { return m_overlayTarget; }

    // Native server handle for C bridge
    // Screen lock (compositor-internal, intercepts all input when locked)
    class ScreenLock* screenLock() const { return m_screenLock; }
    void lockScreen();
    void unlockScreen();
    bool isScreenLocked() const;

    // Idle auto-lock: the screen locks after timeoutMs of no input.
    // Set to 0 to disable.
    void setIdleLockTimeout(uint32_t timeoutMs) { m_idleLockTimeoutMs = timeoutMs; }
    uint32_t idleLockTimeoutMs() const { return m_idleLockTimeoutMs; }
    void checkIdleLock();  // called from the frame loop

    // Cursor mode request. Recorded even when the C handle is not up yet
    // (config.hv runs early), then applied as soon as it exists and to
    // every output as it is enabled.
    void requestSoftwareCursor(bool forced) {
        m_softwareCursorForced = forced;
        if (m_nativeHandle) {
            havel_wlr_set_software_cursor((struct havel_wlr_server*)m_nativeHandle, forced);
        }
    }
    bool softwareCursorForced() const { return m_softwareCursorForced; }

    // Wallpaper from a PNG file, rendered behind all windows. Empty path
    // clears back to the solid background colour. Returns false when the
    // file cannot be decoded.
    bool setWallpaperFromFile(const std::string& path);

    void setNativeHandle(void* handle) {
        m_nativeHandle = handle;
        if (handle && m_softwareCursorForced) {
            havel_wlr_set_software_cursor((struct havel_wlr_server*)handle, true);
        }
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

    // Move a specific view to a workspace (AltTab's move button)
    void moveViewToWorkspace(View* view, uint32_t ws);
    
    // Background color (for wallpaper plugin)
    void setBackgroundColor(float r, float g, float b);
    void getBackgroundColor(float* r, float* g, float* b) const;
    
    // Gamma/temperature control
    void setGamma(float gamma);
    void setTemperature(int kelvin);
    void setBrightness(float brightness);

    // ------------------------------------------------------------------------
    // Zoom (fullscreen scale + lens/strip/custom magnifier overlays).
    //
    // This state is the single source of truth for zoom TARGETS: IPC, Havel
    // and the ZoomPlugin keybindings all mutate it here. The plugin animates
    // the applied values toward the targets every frame (setZoomForOutput)
    // and draws the magnifier overlays; the C frame loop owns the actual
    // scale + cursor-following rendering.
    enum class ZoomMode { Fullscreen = 0, Lens = 1, Strip = 2, Custom = 3 };

    struct ZoomConfig {
        ZoomMode mode = ZoomMode::Fullscreen;
        float step = 1.25f;           // multiplicative in/out step
        float maxLevel = 8.0f;        // fullscreen level clamp
        float maxMag = 20.0f;         // magnifier power clamp
        float toggleLevel = 2.0f;     // level used by toggle/push-to-zoom
    };

    struct ZoomTargets {
        float level = 1.0f;   // fullscreen target (1.0 = off)
        float mag = 1.0f;     // magnifier power target (1.0 = off)
        bool active = false;  // toggle/push state for this output
    };

    ZoomConfig& zoomConfig() { return m_zoomConfig; }
    void zoomConfigure(const std::string& json);   // merge config keys
    void zoomIn(int outputIndex);
    void zoomOut(int outputIndex);
    void zoomReset(int outputIndex);
    void zoomToggle(int outputIndex);
    void zoomPushStart(int outputIndex);           // hold-to-zoom begin
    void zoomPushEnd(int outputIndex);
    void zoomSetMode(int mode);                    // resets all outputs
    void zoomCycleMode();
    float zoomTargetLevel(int outputIndex) const;
    float zoomTargetMag(int outputIndex) const;
    bool zoomActive(int outputIndex) const;
    int zoomMode() const { return (int)m_zoomConfig.mode; }
    std::string zoomStatusJson() const;

    // Magnifier capture handshake (C frame loop <-> plugin):
    // the plugin requests a source region during the overlay pass; the C
    // side takes the request after overlays are drawn, builds the scene
    // state, copies the region out of the rendered buffer and feeds it back.
    void zoomRequestCapture(int outputIndex, float cx, float cy, int w, int h);
    bool zoomTakeCaptureRequest(int outputIndex, float* cx, float* cy, int* w, int* h);
    void zoomFeedCapture(int outputIndex, const uint32_t* pixels, int w, int h,
                         float srcX, float srcY);
    bool zoomGetCapture(int outputIndex, const uint32_t** pixels, int* w, int* h,
                        float* srcX, float* srcY, uint64_t* seq) const;

    // Settings store: persisted key/value pairs (~/.config/havel-wm/
    // settings.json) with live application of known keys. Keys:
    //   gamma, temperature, brightness, animations, background_color
    //   plugin.<plugin>.<key> -> plugin setting, applied immediately
    //   anything else -> stored for scripts/REPL
    // Returns false (without storing) when a typed key's value is invalid.
    bool settingsSet(const std::string& key, const std::string& value);
    std::string settingsGet(const std::string& key) const;
    const std::map<std::string, std::string>& settingsAll() const { return m_settings; }
    void settingsLoad();  // read + apply persisted settings (boot)
    std::string ipcSettings(const std::string& args);

    // Declarative configuration (config.hv statements, applied live)
    void setGaps(int px);                        // tiling gap in px
    void setBorderWidth(int px);                 // decoration border in px
    void addFloatingRule(const std::string& appId);
    void setDefaultLayout(const std::string& name);
    void setWorkspaceCount(uint32_t n);          // 1..WORKSPACE_COUNT
    uint32_t workspaceCount() const { return m_workspaceCount; }
    void addMonitorRule(const std::string& name, int x, int y);
    bool monitorRuleFor(const std::string& name, int* x, int* y) const;
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
    // Re-run config.hv after dropping script-registered keybindings.
    // Empty return = success, else the interpreter error. A failed reload
    // restores the previous bindings.
    std::string reloadHavelConfig();

    std::string ipcSetGamma(const std::string& args);
    std::string ipcSetTemperature(const std::string& args);
    std::string ipcSetBrightness(const std::string& args);
    std::string ipcGetDisplaySettings();
    std::string ipcZoom(const std::string& args);
    std::string ipcFileList(const std::string& args);
    std::string ipcFileInfo(const std::string& args);
    std::string ipcFileOp(const std::string& op, const std::string& args);
    std::string ipcOpenPath(const std::string& args);
    // Script-facing: open a directory in the file manager or a file with
    // its default application. False when the path is invalid/missing.
    bool openPath(const std::string& path, std::string* openedWith = nullptr);
    std::string ipcGetPlugins();
    std::string ipcNotify(const std::string& args);
    std::string ipcCloseNotification(const std::string& args);
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
    bool m_softwareCursorForced = false;
    class ScreenLock* m_screenLock = nullptr;

    // Window placement mode (config: "cascade", "center", "pointer")
    enum class PlacementMode { Cascade, Center, Pointer };
    PlacementMode m_placementMode = PlacementMode::Cascade;
    int m_cascadeX = 40;  // running cascade offset (scene coords)
    int m_cascadeY = 40;
    void placeWindow(View* view);
    uint32_t m_idleLockTimeoutMs = 0;  // 0 = disabled
    uint64_t m_lastInputTimeMs = 0;
    OverlayTarget m_overlayTarget;

    // Zoom state (see zoomConfig()/zoom* methods)
    ZoomConfig m_zoomConfig;
    std::map<int, ZoomTargets> m_zoomTargets;
    std::map<std::string, std::string> m_settings;  // persisted settings store

    // Declarative configuration state
    uint32_t m_workspaceCount = WORKSPACE_COUNT;
    std::map<std::string, std::pair<int, int>> m_monitorRules;  // output name -> x,y
    std::string m_defaultLayout = "master";
    struct ZoomCaptureRequest {
        bool pending = false;
        float cx = 0, cy = 0;
        int w = 0, h = 0;
    };
    struct ZoomCapture {
        std::vector<uint32_t> pixels;
        int w = 0, h = 0;
        float srcX = 0, srcY = 0;
        uint64_t seq = 0;      // bumped on every feed; 0 = none yet
    };
    static constexpr int kMaxZoomOutputs = 8;
    std::array<ZoomCaptureRequest, kMaxZoomOutputs> m_zoomCaptureReq;
    std::array<ZoomCapture, kMaxZoomOutputs> m_zoomCapture;
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
    // Interactive drags started by the decoration plugin (title-bar
    // left-drag = move, right-drag = resize). Routes into the C grab.
public:
    void beginViewDrag(View* view, int mode);
private:
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
