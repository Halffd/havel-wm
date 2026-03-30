#pragma once

#include <wm/Types.hpp>
#include <cstdint>
#include <string>

namespace havel {

/**
 * View represents a single window/surface in the compositor.
 * Pure domain model — no wlroots dependencies.
 */
class View {
public:
    View();

    // Workspace assignment
    uint32_t workspaceId() const { return m_workspaceId; }
    void setWorkspaceId(uint32_t id);

    // Geometry
    Rect geom() const { return m_geom; }
    void setGeom(int x, int y, int w, int h);
    void move(int x, int y);
    void resize(int w, int h);
    
    // Opacity (for animations)
    float opacity() const { return m_opacity; }
    void setOpacity(float opacity);
    
    // Thumbnail texture (for Alt-Tab, Overview)
    uint32_t textureId() const { return m_textureId; }
    void setTextureId(uint32_t id);
    int textureWidth() const { return m_textureWidth; }
    int textureHeight() const { return m_textureHeight; }
    void setTextureSize(int w, int h);

    // Floating state
    bool isFloating() const { return m_floating; }
    void setFloating(bool floating);

    Rect floatGeom() const { return m_floatGeom; }
    void setFloatGeom(const Rect& rect);

    bool hasFloatGeom() const { return m_haveFloatGeom; }

    // State
    bool isMapped() const { return m_mapped; }
    void setMapped(bool mapped);

    // Opaque handle for C bridge to store wlroots pointer
    void* nativeHandle() const { return m_nativeHandle; }
    void setNativeHandle(void* handle) { m_nativeHandle = handle; }

    // Window manager ID (for taskbar integration)
    uint64_t windowId() const { return m_windowId; }
    void setWindowId(uint64_t id) { m_windowId = id; }
    
    // Window metadata (from XDG surface)
    const std::string& appId() const { return m_appId; }
    void setAppId(const std::string& id) { m_appId = id; }
    
    const std::string& title() const { return m_title; }
    void setTitle(const std::string& t) { m_title = t; }

private:
    uint32_t m_workspaceId = 0;
    Rect m_geom;
    Rect m_floatGeom;
    bool m_floating = false;
    bool m_haveFloatGeom = false;
    bool m_mapped = false;
    float m_opacity = 1.0f;  // Opacity for animations (0.0 - 1.0)
    
    // Thumbnail texture (for Alt-Tab, Overview)
    uint32_t m_textureId = 0;
    int m_textureWidth = 0;
    int m_textureHeight = 0;
    
    void* m_nativeHandle = nullptr;
    uint64_t m_windowId = 0;
    std::string m_appId;
    std::string m_title;
};

} // namespace havel
