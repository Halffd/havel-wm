#include "Window.hpp"
#include <wm/Server.hpp>
#include <cstdio>

namespace havel {

// Global window ID counter
static uint64_t s_nextWindowId = 1;

Window::Window(View* view)
    : m_id(s_nextWindowId++)
    , m_view(view)
{
    if (view) {
        m_appId = view->appId();
        m_title = view->title();
        m_workspace = view->workspaceId();
        m_floating = view->isFloating();
    }
}

void Window::setTitle(const std::string& title) {
    m_title = title;
    if (m_view) {
        m_view->setTitle(title);
    }
}

void Window::setAppId(const std::string& appId) {
    m_appId = appId;
    if (m_view) {
        m_view->setAppId(appId);
    }
}

Rect Window::geometry() const {
    return m_view ? m_view->geom() : Rect{0, 0, 0, 0};
}

void Window::setGeometry(int x, int y, int w, int h) {
    if (!m_view) return;
    m_view->setGeom(x, y, w, h);
}

void Window::move(int x, int y) {
    if (!m_view) return;
    Rect g = m_view->geom();
    m_view->setGeom(x, y, g.w, g.h);
}

void Window::resize(int w, int h) {
    if (!m_view) return;
    Rect g = m_view->geom();
    m_view->setGeom(g.x, g.y, w, h);
}

void Window::saveGeometry() {
    if (!m_view) return;
    m_savedGeometry = m_view->geom();
    m_hasSavedGeometry = true;
}

void Window::restoreGeometry() {
    if (!m_view || !m_hasSavedGeometry) return;
    m_view->setGeom(m_savedGeometry.x, m_savedGeometry.y, 
                    m_savedGeometry.w, m_savedGeometry.h);
}

void Window::setFloating(bool floating) {
    if (!m_view) return;
    
    if (floating && !m_floating) {
        // Switching to floating - save current geometry
        saveGeometry();
    } else if (!floating && m_floating && m_hasSavedGeometry) {
        // Switching back to tiled - could restore, but let tiler handle it
    }
    
    m_floating = floating;
    m_view->setFloating(floating);
}

void Window::setMinimized(bool minimized) {
    if (m_minimized == minimized) return;
    m_minimized = minimized;
    printf("[Window] %s minimized: %s\n", 
           m_title.c_str(), minimized ? "yes" : "no");
    // Actual hide/show handled by Server
}

void Window::setMaximized(bool maximized) {
    if (m_maximized == maximized) return;
    
    if (maximized && !m_maximized) {
        saveGeometry();
    } else if (!maximized && m_maximized) {
        restoreGeometry();
    }
    
    m_maximized = maximized;
    printf("[Window] %s maximized: %s\n", 
           m_title.c_str(), maximized ? "yes" : "no");
}

void Window::setFullscreen(bool fullscreen) {
    if (m_fullscreen == fullscreen) return;
    
    if (fullscreen && !m_fullscreen) {
        saveGeometry();
    } else if (!fullscreen && m_fullscreen) {
        restoreGeometry();
    }
    
    m_fullscreen = fullscreen;
    printf("[Window] %s fullscreen: %s\n", 
           m_title.c_str(), fullscreen ? "yes" : "no");
}

void Window::setAlwaysOnTop(bool onTop) {
    m_alwaysOnTop = onTop;
    printf("[Window] %s always-on-top: %s\n", 
           m_title.c_str(), onTop ? "yes" : "no");
}

void Window::setSticky(bool sticky) {
    m_sticky = sticky;
    printf("[Window] %s sticky: %s\n", 
           m_title.c_str(), sticky ? "yes" : "no");
}

void Window::setPinned(bool pinned) {
    m_pinned = pinned;
}

void Window::setWorkspace(uint32_t ws) {
    m_workspace = ws;
    if (m_view) {
        m_view->setWorkspaceId(ws);
    }
    printf("[Window] %s moved to workspace %u\n", 
           m_title.c_str(), ws);
}

void Window::setFocused(bool focused) {
    m_focused = focused;
}

void Window::setOpacity(float opacity) {
    m_opacity = std::clamp(opacity, 0.0f, 1.0f);
    // Actual opacity applied by renderer (if supported)
}

void Window::close() {
    // Request close via Server
    // Actual implementation in WindowManager
}

} // namespace havel
