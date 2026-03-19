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
 * Window manager - tracks all windows and exposes metadata
 * 
 * This is the data model for taskbar/panel integration.
 * Provides signals for window lifecycle events.
 */
class WindowManager {
public:
    using WindowCallback = std::function<void(const WindowEvent&)>;
    
    WindowManager();
    ~WindowManager();
    
    // Window registration (called from C bridge)
    uint64_t registerWindow(void* nativeHandle, const std::string& appId = "", const std::string& title = "");
    void unregisterWindow(uint64_t id);
    
    // Window property updates
    void setWindowTitle(uint64_t id, const std::string& title);
    void setWindowAppId(uint64_t id, const std::string& appId);
    void setWindowWorkspace(uint64_t id, uint32_t workspace);
    void setWindowFlags(uint64_t id, WindowFlags flags);
    void setWindowFlag(uint64_t id, WindowFlags flag, bool set);
    
    // Focus management
    void focusWindow(uint64_t id);
    uint64_t focusedWindow() const { return m_focusedWindowId; }
    
    // Window queries
    const WindowInfo* getWindow(uint64_t id) const;
    std::vector<WindowInfo> getAllWindows() const;
    std::vector<WindowInfo> getWorkspaceWindows(uint32_t workspace) const;
    std::vector<WindowInfo> getVisibleWindows(uint32_t workspace) const;
    
    // Event subscription
    void setWindowCallback(WindowCallback cb) { m_windowCallback = cb; }
    
    // Minimize support
    void minimizeWindow(uint64_t id);
    void restoreWindow(uint64_t id);
    bool isWindowMinimized(uint64_t id) const;

    // Additional IPC methods (stubs for now - would need C bridge integration)
    void closeWindow(uint64_t id);
    void moveWindow(uint64_t id, int x, int y);
    void resizeWindow(uint64_t id, int w, int h);
    void setFloating(uint64_t id, bool floating);
    void switchToWorkspace(uint32_t ws);
    uint32_t getCurrentWorkspace() const { return m_currentWorkspace; }

private:
    void emitEvent(const WindowEvent& event);
    
    struct WindowData {
        uint64_t id;
        void* nativeHandle;
        WindowInfo info;
        bool minimized = false;
    };
    
    std::vector<WindowData> m_windows;
    uint64_t m_nextWindowId = 1;
    uint64_t m_focusedWindowId = 0;
    uint32_t m_currentWorkspace = 0;
    WindowCallback m_windowCallback;
};

} // namespace havel
