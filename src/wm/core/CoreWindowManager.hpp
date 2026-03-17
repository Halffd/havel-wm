#pragma once

#include "Window.hpp"
#include <wm/View.hpp>
#include <wm/Types.hpp>
#include <unordered_map>
#include <vector>
#include <functional>
#include <memory>

namespace havel {

class Server;

/**
 * WindowManager - Actual window management.
 * 
 * Responsibilities:
 * - Track all windows by ID and View pointer
 * - Assign windows to workspaces
 * - Handle minimize/maximize/fullscreen
 * - Window focus and activation
 * - Window rules (app-specific behavior)
 * - IPC window enumeration
 */
class CoreWindowManager {
public:
    CoreWindowManager();
    ~CoreWindowManager();

    // === Lifecycle ===
    void setServer(Server* server) { m_server = server; }
    
    // Add/remove windows
    Window* addWindow(View* view);
    void removeWindow(View* view);
    void removeWindow(uint64_t id);
    
    // Get windows
    Window* getWindow(uint64_t id) const;
    Window* getWindow(View* view) const;
    Window* getFocusedWindow() const { return m_focusedWindow; }
    
    // === Enumeration ===
    std::vector<Window*> getAllWindows() const;
    std::vector<Window*> getWindowsOnWorkspace(uint32_t ws) const;
    std::vector<Window*> getVisibleWindows(uint32_t ws) const;
    std::vector<Window*> getTiledWindows(uint32_t ws) const;
    std::vector<Window*> getFloatingWindows(uint32_t ws) const;
    
    // === Focus ===
    void focusWindow(Window* window);
    void focusWindow(uint64_t id);
    void focusNextWindow(bool backwards = false);
    void focusPreviousWindow();
    
    // === State Changes ===
    void minimizeWindow(Window* window);
    void maximizeWindow(Window* window);
    void toggleMaximize(Window* window);
    void fullscreenWindow(Window* window, bool fullscreen);
    void toggleFullscreen(Window* window);
    void closeWindow(Window* window);
    void closeWindow(uint64_t id);
    
    // === Geometry ===
    void moveWindow(Window* window, int x, int y);
    void resizeWindow(Window* window, int w, int h);
    void setWindowGeometry(Window* window, int x, int y, int w, int h);
    
    // === Floating ===
    void setFloating(Window* window, bool floating);
    void toggleFloating(Window* window);
    
    // === Workspace ===
    void moveWindowToWorkspace(Window* window, uint32_t ws);
    void moveWindowToWorkspace(uint64_t id, uint32_t ws);
    
    // === Always on top / Sticky ===
    void setAlwaysOnTop(Window* window, bool onTop);
    void toggleAlwaysOnTop(Window* window);
    void setSticky(Window* window, bool sticky);
    void toggleSticky(Window* window);
    
    // === Window Rules ===
    struct WindowRule {
        std::string appId;
        std::string title;  // Can use regex in future
        bool floating = false;
        int x = -1, y = -1;  // -1 = center or default
        int w = 0, h = 0;    // 0 = default
        uint32_t workspace = UINT32_MAX;  // UINT32_MAX = current
        bool fullscreen = false;
        bool maximized = false;
        bool sticky = false;
        bool decorations = true;
        float opacity = 1.0f;
    };
    
    void addWindowRule(WindowRule rule);
    void removeWindowRule(const std::string& appId);
    void applyWindowRules(Window* window);

    // === IPC Support ===
    struct IPCWindowInfo {
        uint64_t id;
        std::string appId;
        std::string title;
        int x, y, w, h;
        uint32_t workspace;
        bool floating;
        bool minimized;
        bool maximized;
        bool fullscreen;
        bool focused;
    };
    
    std::string getIPCWindowList() const;
    IPCWindowInfo getWindowInfo(uint64_t id) const;

    // === Callbacks ===
    using WindowCallback = std::function<void(Window*)>;
    void setOnFocus(WindowCallback cb) { m_onFocus = cb; }
    void setOnClose(WindowCallback cb) { m_onClose = cb; }
    void setOnMinimize(WindowCallback cb) { m_onMinimize = cb; }
    void setOnMove(WindowCallback cb) { m_onMove = cb; }

    // === Stats ===
    size_t windowCount() const { return m_windows.size(); }
    size_t windowCountOnWorkspace(uint32_t ws) const;

private:
    Server* m_server = nullptr;
    
    // Window storage - owned by us
    std::unordered_map<uint64_t, std::unique_ptr<Window>> m_windows;
    
    // Quick lookup: View* -> Window*
    std::unordered_map<View*, Window*> m_viewToWindow;
    
    // Focused window
    Window* m_focusedWindow = nullptr;
    
    // Window rules
    std::vector<WindowRule> m_rules;
    
    // Callbacks
    WindowCallback m_onFocus;
    WindowCallback m_onClose;
    WindowCallback m_onMinimize;
    WindowCallback m_onMove;
    
    // Internal helpers
    void applyWindowRule(const WindowRule& rule, Window* window);
    void updateWindowList();
};

} // namespace havel
