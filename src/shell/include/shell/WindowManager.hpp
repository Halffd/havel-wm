#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <functional>

namespace havel {

/**
 * Window state flags
 */
enum class WindowFlags : uint32_t {
    None = 0,
    Focused = 1 << 0,
    Urgent = 1 << 1,
    Minimized = 1 << 2,
    Floating = 1 << 3,
    Fullscreen = 1 << 4,
    Maximized = 1 << 5,
};

inline WindowFlags operator|(WindowFlags a, WindowFlags b) {
    return static_cast<WindowFlags>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}

inline bool hasFlag(WindowFlags flags, WindowFlags flag) {
    return (static_cast<uint32_t>(flags) & static_cast<uint32_t>(flag)) != 0;
}

/**
 * Window information exposed to taskbar/panel
 */
struct WindowInfo {
    uint64_t id = 0;              // Unique window ID
    std::string appId;            // XDG app_id or WM_CLASS
    std::string title;            // Window title
    uint32_t workspace = 0;       // Workspace ID
    WindowFlags flags = WindowFlags::None;
    float opacity = 1.0f;         // per-window opacity (0.05..1.0)
    
    // For IPC serialization
    uint32_t toFlags() const { return static_cast<uint32_t>(flags); }
    static WindowFlags fromFlags(uint32_t f) { return static_cast<WindowFlags>(f); }
};

/**
 * Window event types
 */
enum class WindowEventType {
    WindowCreated,
    WindowDestroyed,
    WindowFocused,
    WindowTitleChanged,
    WindowAppIdChanged,
    WindowWorkspaceChanged,
    WindowFlagsChanged,
    WindowMoved,
    WindowResized,
};

/**
 * Window event for signaling
 */
struct WindowEvent {
    WindowEventType type;
    uint64_t windowId;
    WindowInfo info;  // Full info for created, partial for changes
};

/**
 * Window manager - projection of CoreWindowManager state for the shell/IPC.
 *
 * Phase 4 unification: this class previously kept its own window array
 * with its own ID counter, duplicating CoreWindowManager state and
 * desyncing from it (two different IDs for the same window; IPC
 * mutations emitted events into an unwired callback and never reached
 * real windows). It is now a read-side translation layer:
 *
 * - WindowInfo records are built on demand from the real core::Window
 *   wrappers, so IDs are CoreWindowManager IDs and state can never drift.
 * - Mutations are routed to the compositor through the ServerActions
 *   interface (wired by Server). If no actions are wired (panel/IPC
 *   constructed standalone), mutations are refused with a log line
 *   instead of silently pretending to succeed.
 *
 * The public method names are unchanged; IPCServer and Server call
 * sites did not need edits.
 */
class WindowManager {
public:
    using WindowCallback = std::function<void(const WindowEvent&)>;

    // Mutation routing into the compositor. Implemented by Server;
    // each takes a CoreWindowManager window ID.
    struct ServerActions {
        std::function<void(uint64_t)> focus;
        std::function<void(uint64_t)> minimize;
        std::function<void(uint64_t)> maximize;
        std::function<void(uint64_t)> restore;
        std::function<void(uint64_t)> close;
        std::function<void(uint64_t, int, int)> move;
        std::function<void(uint64_t, int, int)> resize;
        std::function<void(uint64_t, bool)> setFloating;
        std::function<void(uint32_t)> switchWorkspace;
    };

    WindowManager();
    ~WindowManager();

    // Wire the projection to the real window store + mutation path.
    // Called by Server once during composition; without it, queries
    // return empty and mutations are refused.
    void attach(class CoreWindowManager* core, ServerActions actions);
    void detach();

    // Window lifecycle bookkeeping. The C bridge/Server still calls
    // these at create/destroy so window_created/destroyed events fire,
    // but the store itself is core-owned: registerWindow now maps the
    // shell-side identity to the core Window created for the same view
    // (id is allocated by CoreWindowManager).
    uint64_t registerWindow(void* nativeHandle, const std::string& appId = "", const std::string& title = "");
    void unregisterWindow(uint64_t id);

    // Window property updates (routed to the real core Window if the
    // id is live there; events fire for IPC listeners)
    void setWindowTitle(uint64_t id, const std::string& title);
    void setWindowAppId(uint64_t id, const std::string& appId);
    void setWindowWorkspace(uint64_t id, uint32_t workspace);
    void setWindowFlags(uint64_t id, WindowFlags flags);
    void setWindowFlag(uint64_t id, WindowFlags flag, bool set);

    // Focus management (routes through ServerActions.focus)
    void focusWindow(uint64_t id);
    uint64_t focusedWindow() const;

    // Window queries (built from core::Window state on demand)
    const WindowInfo* getWindow(uint64_t id) const;
    std::vector<WindowInfo> getAllWindows() const;
    std::vector<WindowInfo> getWorkspaceWindows(uint32_t workspace) const;
    std::vector<WindowInfo> getVisibleWindows(uint32_t workspace) const;

    // Event subscription (IPC layer listens for window events)
    void setWindowCallback(WindowCallback cb) { m_windowCallback = cb; }

    // Mutations (routed to the compositor; refused if detached)
    void minimizeWindow(uint64_t id);
    void maximizeWindow(uint64_t id);
    void restoreWindow(uint64_t id);
    bool isWindowMinimized(uint64_t id) const;

    void closeWindow(uint64_t id);
    void moveWindow(uint64_t id, int x, int y);
    void resizeWindow(uint64_t id, int w, int h);
    void setFloating(uint64_t id, bool floating);
    void switchToWorkspace(uint32_t ws);
    uint32_t getCurrentWorkspace() const;

private:
    void emitEvent(const WindowEvent& event);
    WindowInfo buildInfo(const class Window& window) const;

    class CoreWindowManager* m_core = nullptr;
    ServerActions m_actions;

    // Views registered before core attach happens (createXdgView runs
    // before onViewMapped) are remembered by native handle so the
    // shell-side registerWindow id can be resolved once core owns them.
    struct PendingView {
        void* nativeHandle;
        std::string appId;
        std::string title;
    };
    std::vector<PendingView> m_pending;
    uint64_t m_fallbackId = 0;     // last id handed out pre-attach
    mutable WindowInfo m_lastInfo; // cache for getWindow() on-demand builds
    WindowCallback m_windowCallback;
};

} // namespace havel
