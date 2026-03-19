#include <shell/WindowManager.hpp>
#include <Logger.h>
#include <algorithm>
#include <cstring>

namespace havel {

WindowManager::WindowManager() = default;

WindowManager::~WindowManager() = default;

uint64_t WindowManager::registerWindow(void* nativeHandle, const std::string& appId, const std::string& title) {
    uint64_t id = m_nextWindowId++;
    
    WindowData data;
    data.id = id;
    data.nativeHandle = nativeHandle;
    data.info.id = id;
    data.info.appId = appId;
    data.info.title = title;
    data.info.workspace = 0;  // Will be set separately
    data.info.flags = WindowFlags::None;
    data.minimized = false;
    
    m_windows.push_back(data);
    
    // Emit created event
    WindowEvent event;
    event.type = WindowEventType::WindowCreated;
    event.windowId = id;
    event.info = data.info;
    emitEvent(event);
    
    return id;
}

void WindowManager::unregisterWindow(uint64_t id) {
    auto it = std::find_if(m_windows.begin(), m_windows.end(),
        [id](const WindowData& w) { return w.id == id; });
    
    if (it != m_windows.end()) {
        // Emit destroyed event
        WindowEvent event;
        event.type = WindowEventType::WindowDestroyed;
        event.windowId = id;
        event.info = it->info;
        emitEvent(event);
        
        if (m_focusedWindowId == id) {
            m_focusedWindowId = 0;
        }
        
        m_windows.erase(it);
    }
}

void WindowManager::setWindowTitle(uint64_t id, const std::string& title) {
    auto* window = const_cast<WindowInfo*>(getWindow(id));
    if (window && window->title != title) {
        window->title = title;
        
        WindowEvent event;
        event.type = WindowEventType::WindowTitleChanged;
        event.windowId = id;
        event.info = *window;
        emitEvent(event);
    }
}

void WindowManager::setWindowAppId(uint64_t id, const std::string& appId) {
    auto* window = const_cast<WindowInfo*>(getWindow(id));
    if (window && window->appId != appId) {
        window->appId = appId;
        
        WindowEvent event;
        event.type = WindowEventType::WindowAppIdChanged;
        event.windowId = id;
        event.info = *window;
        emitEvent(event);
    }
}

void WindowManager::setWindowWorkspace(uint64_t id, uint32_t workspace) {
    auto* window = const_cast<WindowInfo*>(getWindow(id));
    if (window && window->workspace != workspace) {
        window->workspace = workspace;
        
        WindowEvent event;
        event.type = WindowEventType::WindowWorkspaceChanged;
        event.windowId = id;
        event.info = *window;
        emitEvent(event);
    }
}

void WindowManager::setWindowFlags(uint64_t id, WindowFlags flags) {
    auto* window = const_cast<WindowInfo*>(getWindow(id));
    if (window && window->flags != flags) {
        window->flags = flags;
        
        WindowEvent event;
        event.type = WindowEventType::WindowFlagsChanged;
        event.windowId = id;
        event.info = *window;
        emitEvent(event);
    }
}

void WindowManager::setWindowFlag(uint64_t id, WindowFlags flag, bool set) {
    auto* window = const_cast<WindowInfo*>(getWindow(id));
    if (window) {
        WindowFlags newFlags = set ? (window->flags | flag) : static_cast<WindowFlags>(static_cast<uint32_t>(window->flags) & ~static_cast<uint32_t>(flag));
        setWindowFlags(id, newFlags);
    }
}

void WindowManager::focusWindow(uint64_t id) {
    // Clear focus from previous window
    if (m_focusedWindowId != 0) {
        setWindowFlag(m_focusedWindowId, WindowFlags::Focused, false);
    }
    
    m_focusedWindowId = id;
    
    // Set focus flag on new window
    if (id != 0) {
        setWindowFlag(id, WindowFlags::Focused, true);
    }
    
    // Emit focus event
    WindowEvent event;
    event.type = WindowEventType::WindowFocused;
    event.windowId = id;
    if (const auto* info = getWindow(id)) {
        event.info = *info;
    }
    emitEvent(event);
}

const WindowInfo* WindowManager::getWindow(uint64_t id) const {
    auto it = std::find_if(m_windows.begin(), m_windows.end(),
        [id](const WindowData& w) { return w.id == id; });
    
    if (it != m_windows.end()) {
        return &it->info;
    }
    return nullptr;
}

std::vector<WindowInfo> WindowManager::getAllWindows() const {
    std::vector<WindowInfo> result;
    result.reserve(m_windows.size());
    for (const auto& w : m_windows) {
        result.push_back(w.info);
    }
    return result;
}

std::vector<WindowInfo> WindowManager::getWorkspaceWindows(uint32_t workspace) const {
    std::vector<WindowInfo> result;
    for (const auto& w : m_windows) {
        if (w.info.workspace == workspace) {
            result.push_back(w.info);
        }
    }
    return result;
}

std::vector<WindowInfo> WindowManager::getVisibleWindows(uint32_t workspace) const {
    std::vector<WindowInfo> result;
    for (const auto& w : m_windows) {
        if (w.info.workspace == workspace && !w.minimized) {
            result.push_back(w.info);
        }
    }
    return result;
}

void WindowManager::minimizeWindow(uint64_t id) {
    auto it = std::find_if(m_windows.begin(), m_windows.end(),
        [id](const WindowData& w) { return w.id == id; });
    
    if (it != m_windows.end()) {
        it->minimized = true;
        setWindowFlag(id, WindowFlags::Minimized, true);
    }
}

void WindowManager::restoreWindow(uint64_t id) {
    auto it = std::find_if(m_windows.begin(), m_windows.end(),
        [id](const WindowData& w) { return w.id == id; });
    
    if (it != m_windows.end()) {
        it->minimized = false;
        setWindowFlag(id, WindowFlags::Minimized, false);
    }
}

bool WindowManager::isWindowMinimized(uint64_t id) const {
    auto it = std::find_if(m_windows.begin(), m_windows.end(),
        [id](const WindowData& w) { return w.id == id; });

    if (it != m_windows.end()) {
        return it->minimized;
    }
    return false;
}

// IPC implementations - communicate with compositor via IPC
void WindowManager::closeWindow(uint64_t id) {
    auto it = std::find_if(m_windows.begin(), m_windows.end(),
        [id](const WindowData& w) { return w.id == id; });

    if (it != m_windows.end()) {
        // Emit close event for compositor to handle
        WindowEvent event;
        event.type = WindowEventType::WindowDestroyed;
        event.windowId = id;
        event.info = it->info;
        emitEvent(event);
        
        // Remove from our tracking
        m_windows.erase(it);
        LOG_INFO("[WindowManager] Closed window %lu", id);
    }
}

void WindowManager::moveWindow(uint64_t id, int x, int y) {
    auto it = std::find_if(m_windows.begin(), m_windows.end(),
        [id](const WindowData& w) { return w.id == id; });

    if (it != m_windows.end()) {
        // Update our tracking
        // Actual movement handled by compositor
        LOG_INFO("[WindowManager] Move window %lu to (%d, %d)", id, x, y);
        
        WindowEvent event;
        event.type = WindowEventType::WindowMoved;
        event.windowId = id;
        event.info = it->info;
        emitEvent(event);
    }
}

void WindowManager::resizeWindow(uint64_t id, int w, int h) {
    auto it = std::find_if(m_windows.begin(), m_windows.end(),
        [id](const WindowData& w) { return w.id == id; });

    if (it != m_windows.end()) {
        // Update our tracking
        // Actual resize handled by compositor
        LOG_INFO("[WindowManager] Resize window %lu to %dx%d", id, w, h);
        
        WindowEvent event;
        event.type = WindowEventType::WindowResized;
        event.windowId = id;
        event.info = it->info;
        emitEvent(event);
    }
}

void WindowManager::setFloating(uint64_t id, bool floating) {
    auto it = std::find_if(m_windows.begin(), m_windows.end(),
        [id](const WindowData& w) { return w.id == id; });

    if (it != m_windows.end()) {
        setWindowFlag(id, floating ? WindowFlags::Floating : WindowFlags::None, floating);
        
        WindowEvent event;
        event.type = WindowEventType::WindowFlagsChanged;
        event.windowId = id;
        event.info = it->info;
        emitEvent(event);
        
        LOG_INFO("[WindowManager] Set window %lu floating=%s", id, floating ? "true" : "false");
    }
}

void WindowManager::switchToWorkspace(uint32_t ws) {
    if (ws >= 10) {
        LOG_ERROR("[WindowManager] Invalid workspace: %u", ws);
        return;
    }
    
    m_currentWorkspace = ws;
    
    // Emit workspace change event
    WindowEvent event;
    event.type = WindowEventType::WindowWorkspaceChanged;
    event.windowId = 0;  // Workspace change, not window-specific
    event.info.workspace = ws;
    emitEvent(event);
    
    LOG_INFO("[WindowManager] Switched to workspace %u", ws);
}

void WindowManager::emitEvent(const WindowEvent& event) {
    if (m_windowCallback) {
        m_windowCallback(event);
    }
}

} // namespace havel
