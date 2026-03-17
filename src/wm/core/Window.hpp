#pragma once

#include <wm/View.hpp>
#include <wm/Types.hpp>
#include <cstdint>
#include <string>

namespace havel {

/**
 * Window - High-level window representation with full state tracking.
 * 
 * Wraps View and adds:
 * - Window ID for IPC/taskbar
 * - Minimize/maximize/fullscreen state
 * - Geometry history for restore
 * - Workspace assignment
 * - Focus tracking
 * 
 * LIFETIME: Non-owning wrapper around View. View is owned by Server.
 */
class Window {
public:
    explicit Window(View* view);
    ~Window() = default;

    // === Identity ===
    uint64_t id() const { return m_id; }
    View* view() const { return m_view; }
    
    const std::string& appId() const { return m_appId; }
    const std::string& title() const { return m_title; }
    
    void setTitle(const std::string& title);
    void setAppId(const std::string& appId);

    // === Geometry ===
    Rect geometry() const;
    void setGeometry(int x, int y, int w, int h);
    void move(int x, int y);
    void resize(int w, int h);
    
    // Floating geometry storage
    Rect savedGeometry() const { return m_savedGeometry; }
    void saveGeometry();
    void restoreGeometry();
    bool hasSavedGeometry() const { return m_hasSavedGeometry; }

    // === State Flags ===
    bool isFloating() const { return m_floating; }
    void setFloating(bool floating);
    
    bool isMinimized() const { return m_minimized; }
    void setMinimized(bool minimized);
    
    bool isMaximized() const { return m_maximized; }
    void setMaximized(bool maximized);
    
    bool isFullscreen() const { return m_fullscreen; }
    void setFullscreen(bool fullscreen);
    
    bool isAlwaysOnTop() const { return m_alwaysOnTop; }
    void setAlwaysOnTop(bool onTop);
    
    bool isSticky() const { return m_sticky; }
    void setSticky(bool sticky);
    
    bool isPinned() const { return m_pinned; }
    void setPinned(bool pinned);

    // Combined state check
    bool isTiled() const { return !m_floating && !m_fullscreen; }
    bool isNormal() const { return !m_minimized && !m_maximized && !m_fullscreen; }

    // === Workspace ===
    uint32_t workspace() const { return m_workspace; }
    void setWorkspace(uint32_t ws);
    
    bool isOnWorkspace(uint32_t ws) const {
        return m_workspace == ws || m_sticky;
    }

    // === Focus ===
    bool isFocused() const { return m_focused; }
    void setFocused(bool focused);

    // === Output ===
    uint32_t outputId() const { return m_outputId; }
    void setOutputId(uint32_t id) { m_outputId = id; }

    // === Layer ===
    enum class Layer {
        Normal,
        Top,
        Bottom,
        Overlay
    };
    Layer layer() const { return m_layer; }
    void setLayer(Layer layer) { m_layer = layer; }

    // === Decorations ===
    bool hasDecorations() const { return m_decorations; }
    void setDecorations(bool deco) { m_decorations = deco; }

    // === Opacity ===
    float opacity() const { return m_opacity; }
    void setOpacity(float opacity);

    // === Close ===
    void close();

    // === Equality ===
    bool operator==(const Window& other) const { return m_id == other.m_id; }
    bool operator==(const View* view) const { return m_view == view; }

private:
    uint64_t m_id;
    View* m_view;
    
    std::string m_appId;
    std::string m_title;
    
    uint32_t m_workspace = 0;
    uint32_t m_outputId = 0;
    
    Rect m_savedGeometry;
    bool m_hasSavedGeometry = false;
    
    bool m_floating = false;
    bool m_minimized = false;
    bool m_maximized = false;
    bool m_fullscreen = false;
    bool m_alwaysOnTop = false;
    bool m_sticky = false;
    bool m_pinned = false;
    bool m_focused = false;
    bool m_decorations = true;
    
    float m_opacity = 1.0f;
    Layer m_layer = Layer::Normal;
};

// Hash for unordered_map
struct WindowHash {
    size_t operator()(const Window* w) const {
        return std::hash<uint64_t>{}(w->id());
    }
};

} // namespace havel
