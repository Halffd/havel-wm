#include "CoreWindowManager.hpp"
#include <wm/Server.hpp>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cstdio>

using json = nlohmann::json;

namespace havel {

CoreWindowManager::CoreWindowManager() = default;

CoreWindowManager::~CoreWindowManager() {
    m_windows.clear();
    m_viewToWindow.clear();
}

// ============================================================================
// Window Lifecycle
// ============================================================================

Window* CoreWindowManager::addWindow(View* view) {
    if (!view) return nullptr;
    
    // Check if already tracked
    auto it = m_viewToWindow.find(view);
    if (it != m_viewToWindow.end()) {
        return it->second;
    }
    
    // Create new window
    auto window = std::make_unique<Window>(view);
    Window* ptr = window.get();
    
    // Store in maps
    m_windows[ptr->id()] = std::move(window);
    m_viewToWindow[view] = ptr;
    
    // Apply window rules
    applyWindowRules(ptr);
    
    printf("[WindowManager] Added window %lu: %s - %s (workspace=%u, floating=%s)\n",
           ptr->id(), ptr->appId().c_str(), ptr->title().c_str(),
           ptr->workspace(), ptr->isFloating() ? "yes" : "no");
    
    return ptr;
}

void CoreWindowManager::removeWindow(View* view) {
    if (!view) return;
    
    auto it = m_viewToWindow.find(view);
    if (it == m_viewToWindow.end()) {
        return;
    }
    
    Window* window = it->second;
    uint64_t id = window->id();
    
    printf("[WindowManager] Removing window %lu: %s\n", id, window->title().c_str());
    
    // Clear focus if needed
    if (m_focusedWindow == window) {
        m_focusedWindow = nullptr;
    }
    
    // Remove from maps
    m_viewToWindow.erase(it);
    m_windows.erase(id);
}

void CoreWindowManager::removeWindow(uint64_t id) {
    auto it = m_windows.find(id);
    if (it == m_windows.end()) return;
    
    View* view = it->second->view();
    m_viewToWindow.erase(view);
    m_windows.erase(it);
}

Window* CoreWindowManager::getWindow(uint64_t id) const {
    auto it = m_windows.find(id);
    return (it != m_windows.end()) ? it->second.get() : nullptr;
}

Window* CoreWindowManager::getWindow(View* view) const {
    auto it = m_viewToWindow.find(view);
    return (it != m_viewToWindow.end()) ? it->second : nullptr;
}

// ============================================================================
// Window Enumeration
// ============================================================================

std::vector<Window*> CoreWindowManager::getAllWindows() const {
    std::vector<Window*> result;
    result.reserve(m_windows.size());
    
    for (const auto& [id, window] : m_windows) {
        result.push_back(window.get());
    }
    
    return result;
}

std::vector<Window*> CoreWindowManager::getWindowsOnWorkspace(uint32_t ws) const {
    std::vector<Window*> result;
    
    for (const auto& [id, window] : m_windows) {
        if (window->isOnWorkspace(ws) && !window->isMinimized()) {
            result.push_back(window.get());
        }
    }
    
    return result;
}

std::vector<Window*> CoreWindowManager::getVisibleWindows(uint32_t ws) const {
    std::vector<Window*> result;
    
    for (const auto& [id, window] : m_windows) {
        if (window->isOnWorkspace(ws) && 
            !window->isMinimized() && 
            !window->isFullscreen()) {
            result.push_back(window.get());
        }
    }
    
    return result;
}

std::vector<Window*> CoreWindowManager::getTiledWindows(uint32_t ws) const {
    std::vector<Window*> result;
    
    for (const auto& [id, window] : m_windows) {
        if (window->isOnWorkspace(ws) && window->isTiled()) {
            result.push_back(window.get());
        }
    }
    
    return result;
}

std::vector<Window*> CoreWindowManager::getFloatingWindows(uint32_t ws) const {
    std::vector<Window*> result;
    
    for (const auto& [id, window] : m_windows) {
        if (window->isOnWorkspace(ws) && window->isFloating()) {
            result.push_back(window.get());
        }
    }
    
    return result;
}

// ============================================================================
// Focus Management
// ============================================================================

void CoreWindowManager::focusWindow(Window* window) {
    if (!window || m_focusedWindow == window) return;
    
    // Unfocus old window
    if (m_focusedWindow) {
        m_focusedWindow->setFocused(false);
    }
    
    // Focus new window
    m_focusedWindow = window;
    window->setFocused(true);
    
    // Unminimize if needed
    if (window->isMinimized()) {
        window->setMinimized(false);
    }
    
    // Raise to top (via Server)
    if (m_server) {
        m_server->focusView(window->view());
    }
    
    printf("[WindowManager] Focused window %lu: %s\n", 
           window->id(), window->title().c_str());
    
    if (m_onFocus) {
        m_onFocus(window);
    }
}

void CoreWindowManager::focusWindow(uint64_t id) {
    Window* window = getWindow(id);
    if (window) {
        focusWindow(window);
    }
}

void CoreWindowManager::focusNextWindow(bool backwards) {
    auto windows = getAllWindows();
    if (windows.empty()) return;
    
    // Find current focused window index
    int currentIndex = -1;
    for (size_t i = 0; i < windows.size(); i++) {
        if (windows[i] == m_focusedWindow) {
            currentIndex = static_cast<int>(i);
            break;
        }
    }
    
    // Calculate next index
    int nextIndex;
    if (backwards) {
        nextIndex = (currentIndex <= 0) ? static_cast<int>(windows.size() - 1) : currentIndex - 1;
    } else {
        nextIndex = (currentIndex < 0 || currentIndex >= static_cast<int>(windows.size()) - 1) ? 0 : currentIndex + 1;
    }
    
    focusWindow(windows[nextIndex]);
}

void CoreWindowManager::focusPreviousWindow() {
    focusNextWindow(true);
}

// ============================================================================
// Window State
// ============================================================================

void CoreWindowManager::minimizeWindow(Window* window) {
    if (!window) return;
    
    window->setMinimized(true);
    printf("[WindowManager] Minimized window %lu\n", window->id());
    
    if (m_onMinimize) {
        m_onMinimize(window);
    }
    
    // Focus next available window
    if (m_focusedWindow == window) {
        focusNextWindow();
    }
}

void CoreWindowManager::maximizeWindow(Window* window) {
    if (!window) return;
    
    window->setMaximized(!window->isMaximized());
    
    // Apply maximized geometry
    if (window->isMaximized() && m_server) {
        // Would get output geometry and maximize
        // For now, just toggle the flag
    }
}

void CoreWindowManager::toggleMaximize(Window* window) {
    maximizeWindow(window);
}

void CoreWindowManager::fullscreenWindow(Window* window, bool fullscreen) {
    if (!window) return;
    
    window->setFullscreen(fullscreen);
    
    if (m_server) {
        // Server handles actual fullscreen via wlroots
    }
}

void CoreWindowManager::toggleFullscreen(Window* window) {
    if (!window) return;
    fullscreenWindow(window, !window->isFullscreen());
}

void CoreWindowManager::closeWindow(Window* window) {
    if (!window) return;
    
    printf("[WindowManager] Closing window %lu: %s\n", 
           window->id(), window->title().c_str());
    
    if (m_onClose) {
        m_onClose(window);
    }
    
    // Remove from tracking
    removeWindow(window->view());
}

void CoreWindowManager::closeWindow(uint64_t id) {
    Window* window = getWindow(id);
    if (window) {
        closeWindow(window);
    }
}

// ============================================================================
// Geometry
// ============================================================================

void CoreWindowManager::moveWindow(Window* window, int x, int y) {
    if (!window) return;
    
    window->move(x, y);
    
    if (m_onMove) {
        m_onMove(window);
    }
}

void CoreWindowManager::resizeWindow(Window* window, int w, int h) {
    if (!window) return;
    window->resize(w, h);
}

void CoreWindowManager::setWindowGeometry(Window* window, int x, int y, int w, int h) {
    if (!window) return;
    window->setGeometry(x, y, w, h);
}

// ============================================================================
// Floating Mode
// ============================================================================

void CoreWindowManager::setFloating(Window* window, bool floating) {
    if (!window) return;
    window->setFloating(floating);
}

void CoreWindowManager::toggleFloating(Window* window) {
    if (!window) return;
    setFloating(window, !window->isFloating());
}

// ============================================================================
// Workspace
// ============================================================================

void CoreWindowManager::moveWindowToWorkspace(Window* window, uint32_t ws) {
    if (!window) return;
    window->setWorkspace(ws);
}

void CoreWindowManager::moveWindowToWorkspace(uint64_t id, uint32_t ws) {
    Window* window = getWindow(id);
    if (window) {
        moveWindowToWorkspace(window, ws);
    }
}

// ============================================================================
// Always on Top / Sticky
// ============================================================================

void CoreWindowManager::setAlwaysOnTop(Window* window, bool onTop) {
    if (!window) return;
    window->setAlwaysOnTop(onTop);
}

void CoreWindowManager::toggleAlwaysOnTop(Window* window) {
    if (!window) return;
    setAlwaysOnTop(window, !window->isAlwaysOnTop());
}

void CoreWindowManager::setSticky(Window* window, bool sticky) {
    if (!window) return;
    window->setSticky(sticky);
}

void CoreWindowManager::toggleSticky(Window* window) {
    if (!window) return;
    setSticky(window, !window->isSticky());
}

// ============================================================================
// Window Rules
// ============================================================================

void CoreWindowManager::addWindowRule(WindowRule rule) {
    m_rules.push_back(std::move(rule));
    printf("[WindowManager] Added window rule for: %s\n", rule.appId.c_str());
}

void CoreWindowManager::removeWindowRule(const std::string& appId) {
    m_rules.erase(
        std::remove_if(m_rules.begin(), m_rules.end(),
            [&appId](const WindowRule& r) { return r.appId == appId; }),
        m_rules.end());
}

void CoreWindowManager::applyWindowRules(Window* window) {
    if (!window) return;
    
    const std::string& appId = window->appId();
    const std::string& title = window->title();
    
    for (const auto& rule : m_rules) {
        bool match = false;
        
        if (!rule.appId.empty() && appId.find(rule.appId) != std::string::npos) {
            match = true;
        }
        if (!rule.title.empty() && title.find(rule.title) != std::string::npos) {
            match = true;
        }
        
        if (match) {
            applyWindowRule(rule, window);
        }
    }
}

void CoreWindowManager::applyWindowRule(const WindowRule& rule, Window* window) {
    printf("[WindowManager] Applying rule to %lu: %s\n", 
           window->id(), window->title().c_str());
    
    if (rule.floating) {
        window->setFloating(true);
    }
    
    if (rule.w > 0 && rule.h > 0) {
        int x = (rule.x >= 0) ? rule.x : window->geometry().x;
        int y = (rule.y >= 0) ? rule.y : window->geometry().y;
        window->setGeometry(x, y, rule.w, rule.h);
    }
    
    if (rule.workspace != UINT32_MAX) {
        window->setWorkspace(rule.workspace);
    }
    
    if (rule.fullscreen) {
        window->setFullscreen(true);
    }
    
    if (rule.maximized) {
        window->setMaximized(true);
    }
    
    if (rule.sticky) {
        window->setSticky(true);
    }
    
    window->setDecorations(rule.decorations);
    window->setOpacity(rule.opacity);
}

// ============================================================================
// IPC Support
// ============================================================================

CoreWindowManager::IPCWindowInfo CoreWindowManager::getWindowInfo(uint64_t id) const {
    IPCWindowInfo info = {};
    
    Window* window = getWindow(id);
    if (!window) return info;
    
    info.id = id;
    info.appId = window->appId();
    info.title = window->title();
    
    Rect geom = window->geometry();
    info.x = geom.x;
    info.y = geom.y;
    info.w = geom.w;
    info.h = geom.h;
    
    info.workspace = window->workspace();
    info.floating = window->isFloating();
    info.minimized = window->isMinimized();
    info.maximized = window->isMaximized();
    info.fullscreen = window->isFullscreen();
    info.focused = (window == m_focusedWindow);
    
    return info;
}

std::string CoreWindowManager::getIPCWindowList() const {
    json arr = json::array();
    
    for (const auto& [id, window] : m_windows) {
        auto info = getWindowInfo(id);
        json win = {
            {"id", info.id},
            {"appId", info.appId},
            {"title", info.title},
            {"x", info.x},
            {"y", info.y},
            {"w", info.w},
            {"h", info.h},
            {"workspace", info.workspace},
            {"floating", info.floating},
            {"minimized", info.minimized},
            {"maximized", info.maximized},
            {"fullscreen", info.fullscreen},
            {"focused", info.focused}
        };
        arr.push_back(win);
    }
    
    return arr.dump();
}

// ============================================================================
// Stats
// ============================================================================

size_t CoreWindowManager::windowCountOnWorkspace(uint32_t ws) const {
    size_t count = 0;
    for (const auto& [id, window] : m_windows) {
        if (window->isOnWorkspace(ws)) {
            count++;
        }
    }
    return count;
}

} // namespace havel
