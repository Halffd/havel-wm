// WindowManager - projection of CoreWindowManager state for shell/IPC.
// Phase 4 unification: see header for the design notes. Queries translate
// real core::Window state into WindowInfo records; mutations route to the
// compositor through ServerActions instead of updating a private copy.

#include <shell/WindowManager.hpp>
#include <core/CoreWindowManager.hpp>
#include <core/Window.hpp>
#include <wm/View.hpp>
#include <Logger.h>

namespace havel {

WindowManager::WindowManager() = default;

WindowManager::~WindowManager() = default;

void WindowManager::attach(CoreWindowManager* core, ServerActions actions) {
    m_core = core;
    m_actions = std::move(actions);
}

void WindowManager::detach() {
    m_core = nullptr;
    m_actions = ServerActions{};
}

// Translate a real core Window into the shell-facing record.
WindowInfo WindowManager::buildInfo(const Window& window) const {
    WindowInfo info;
    info.id = window.id();
    info.appId = window.appId();
    info.title = window.title();
    info.workspace = window.workspace();

    WindowFlags flags = WindowFlags::None;
    if (window.isFocused())  flags = flags | WindowFlags::Focused;
    if (window.isMinimized()) flags = flags | WindowFlags::Minimized;
    if (window.isFloating())  flags = flags | WindowFlags::Floating;
    if (window.isFullscreen()) flags = flags | WindowFlags::Fullscreen;
    if (window.isMaximized()) flags = flags | WindowFlags::Maximized;
    info.flags = flags;
    info.opacity = window.opacity();
    return info;
}

uint64_t WindowManager::registerWindow(void* nativeHandle, const std::string& appId, const std::string& title) {
    // nativeHandle is the View* the C++ core created for this surface.
    // Pre-create the core Window so the id handed to IPC/panel is the
    // stable CoreWindowManager id from the very first broadcast.
    // CoreWindowManager::addWindow is idempotent (returns the existing
    // wrapper), so Server's later onViewMapped -> addWindow reuses this
    // Window instead of making a second one.
    if (m_core && nativeHandle) {
        Window* window = m_core->addWindow(static_cast<View*>(nativeHandle));
        if (window) {
            if (!appId.empty() || !title.empty()) {
                if (!appId.empty() && window->appId().empty()) window->setAppId(appId);
                if (!title.empty() && window->title().empty()) window->setTitle(title);
            }

            WindowEvent event;
            event.type = WindowEventType::WindowCreated;
            event.windowId = window->id();
            event.info = buildInfo(*window);
            emitEvent(event);
            return window->id();
        }
    }

    // Not attached yet (or no view): remember so a later attach can
    // resolve, and hand out a negative-space marker id that cannot
    // collide with real core ids.
    PendingView pending;
    pending.nativeHandle = nativeHandle;
    pending.appId = appId;
    pending.title = title;
    m_pending.push_back(std::move(pending));
    return --m_fallbackId;
}

void WindowManager::unregisterWindow(uint64_t id) {
    if (!m_core) return;
    Window* window = m_core->getWindow(id);
    if (!window) return;

    WindowEvent event;
    event.type = WindowEventType::WindowDestroyed;
    event.windowId = id;
    event.info = buildInfo(*window);
    emitEvent(event);

    // Core removes by View; Server::onViewDestroyed performs the core
    // removal, this projection only reports.
}

void WindowManager::setWindowTitle(uint64_t id, const std::string& title) {
    if (!m_core) return;
    if (Window* window = m_core->getWindow(id)) {
        if (window->title() != title) {
            window->setTitle(title);
            WindowEvent event;
            event.type = WindowEventType::WindowTitleChanged;
            event.windowId = id;
            event.info = buildInfo(*window);
            emitEvent(event);
        }
    }
}

void WindowManager::setWindowAppId(uint64_t id, const std::string& appId) {
    if (!m_core) return;
    if (Window* window = m_core->getWindow(id)) {
        if (window->appId() != appId) {
            window->setAppId(appId);
            WindowEvent event;
            event.type = WindowEventType::WindowAppIdChanged;
            event.windowId = id;
            event.info = buildInfo(*window);
            emitEvent(event);
        }
    }
}

void WindowManager::setWindowWorkspace(uint64_t id, uint32_t workspace) {
    if (!m_core) return;
    if (Window* window = m_core->getWindow(id)) {
        if (window->workspace() != workspace) {
            window->setWorkspace(workspace);
            WindowEvent event;
            event.type = WindowEventType::WindowWorkspaceChanged;
            event.windowId = id;
            event.info = buildInfo(*window);
            emitEvent(event);
        }
    }
}

void WindowManager::setWindowFlags(uint64_t id, WindowFlags flags) {
    if (!m_core) return;
    if (Window* window = m_core->getWindow(id)) {
        // Flags are derived from core state; push the bits we own.
        if (hasFlag(flags, WindowFlags::Minimized)) window->setMinimized(true);
        if (hasFlag(flags, WindowFlags::Floating))  window->setFloating(true);
        if (hasFlag(flags, WindowFlags::Fullscreen)) window->setFullscreen(true);
        if (hasFlag(flags, WindowFlags::Maximized)) window->setMaximized(true);
        WindowEvent event;
        event.type = WindowEventType::WindowFlagsChanged;
        event.windowId = id;
        event.info = buildInfo(*window);
        emitEvent(event);
    }
}

void WindowManager::setWindowFlag(uint64_t id, WindowFlags flag, bool set) {
    if (!m_core) return;
    if (Window* window = m_core->getWindow(id)) {
        switch (flag) {
            case WindowFlags::Minimized:  window->setMinimized(set); break;
            case WindowFlags::Floating:   window->setFloating(set); break;
            case WindowFlags::Fullscreen: window->setFullscreen(set); break;
            case WindowFlags::Maximized:  window->setMaximized(set); break;
            case WindowFlags::Focused:   window->setFocused(set); break;
            default: break;  // Urgent has no core representation yet
        }
        WindowEvent event;
        event.type = WindowEventType::WindowFlagsChanged;
        event.windowId = id;
        event.info = buildInfo(*window);
        emitEvent(event);
    }
}

void WindowManager::focusWindow(uint64_t id) {
    if (m_actions.focus) {
        m_actions.focus(id);
        return;
    }
    LOG_WARN("[WindowManager] focusWindow(%lu) refused: not attached to compositor", id);
}

uint64_t WindowManager::focusedWindow() const {
    if (!m_core) return 0;
    const Window* focused = m_core->getFocusedWindow();
    return focused ? focused->id() : 0;
}

const WindowInfo* WindowManager::getWindow(uint64_t id) const {
    // Note: returns a pointer into m_lastInfo, a mutable cache so the
    // const method can hand back a WindowInfo built on demand.
    if (!m_core) return nullptr;
    const Window* window = m_core->getWindow(id);
    if (!window) return nullptr;
    m_lastInfo = buildInfo(*window);
    return &m_lastInfo;
}

std::vector<WindowInfo> WindowManager::getAllWindows() const {
    std::vector<WindowInfo> result;
    if (!m_core) return result;
    result.reserve(m_core->getAllWindows().size());
    for (const Window* window : m_core->getAllWindows()) {
        if (window) result.push_back(buildInfo(*window));
    }
    return result;
}

std::vector<WindowInfo> WindowManager::getWorkspaceWindows(uint32_t workspace) const {
    std::vector<WindowInfo> result;
    if (!m_core) return result;
    for (const Window* window : m_core->getWindowsOnWorkspace(workspace)) {
        if (window) result.push_back(buildInfo(*window));
    }
    return result;
}

std::vector<WindowInfo> WindowManager::getVisibleWindows(uint32_t workspace) const {
    std::vector<WindowInfo> result;
    if (!m_core) return result;
    for (const Window* window : m_core->getWindowsOnWorkspace(workspace)) {
        if (window && !window->isMinimized()) result.push_back(buildInfo(*window));
    }
    return result;
}

void WindowManager::minimizeWindow(uint64_t id) {
    if (m_actions.minimize) {
        m_actions.minimize(id);
        return;
    }
    LOG_WARN("[WindowManager] minimizeWindow(%lu) refused: not attached to compositor", id);
}

void WindowManager::maximizeWindow(uint64_t id) {
    if (m_actions.maximize) {
        m_actions.maximize(id);
        return;
    }
    LOG_WARN("[WindowManager] maximizeWindow(%lu) refused: not attached to compositor", id);
}

void WindowManager::restoreWindow(uint64_t id) {
    if (m_actions.restore) {
        m_actions.restore(id);
        return;
    }
    LOG_WARN("[WindowManager] restoreWindow(%lu) refused: not attached to compositor", id);
}

bool WindowManager::isWindowMinimized(uint64_t id) const {
    if (!m_core) return false;
    const Window* window = m_core->getWindow(id);
    return window && window->isMinimized();
}

void WindowManager::closeWindow(uint64_t id) {
    if (m_actions.close) {
        m_actions.close(id);
        return;
    }
    LOG_WARN("[WindowManager] closeWindow(%lu) refused: not attached to compositor", id);
}

void WindowManager::moveWindow(uint64_t id, int x, int y) {
    if (m_actions.move) {
        m_actions.move(id, x, y);
        return;
    }
    LOG_WARN("[WindowManager] moveWindow(%lu) refused: not attached to compositor", id);
}

void WindowManager::resizeWindow(uint64_t id, int w, int h) {
    if (m_actions.resize) {
        m_actions.resize(id, w, h);
        return;
    }
    LOG_WARN("[WindowManager] resizeWindow(%lu) refused: not attached to compositor", id);
}

void WindowManager::setFloating(uint64_t id, bool floating) {
    if (m_actions.setFloating) {
        m_actions.setFloating(id, floating);
        return;
    }
    LOG_WARN("[WindowManager] setFloating(%lu) refused: not attached to compositor", id);
}

void WindowManager::switchToWorkspace(uint32_t ws) {
    if (m_actions.switchWorkspace) {
        m_actions.switchWorkspace(ws);
        return;
    }
    LOG_WARN("[WindowManager] switchToWorkspace(%u) refused: not attached to compositor", ws);
}

uint32_t WindowManager::getCurrentWorkspace() const {
    // Workspace switching state lives in Server (m_activeWorkspace); the
    // actions.switchWorkspace closure owns it. The projection itself
    // keeps no workspace state, so 0 is reported when detached.
    if (!m_core) return 0;
    const Window* focused = m_core->getFocusedWindow();
    return focused ? focused->workspace() : 0;
}

void WindowManager::emitEvent(const WindowEvent& event) {
    if (m_windowCallback) {
        m_windowCallback(event);
    }
}

} // namespace havel
