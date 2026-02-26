#pragma once

#include <wm/Types.hpp>
#include <vector>
#include <string>
#include <functional>

namespace havel {

/**
 * Window thumbnail for Alt-Tab overlay
 */
struct WindowThumbnail {
    uint64_t windowId = 0;
    std::string appId;
    std::string title;
    int x = 0, y = 0, w = 0, h = 0;
    bool isFocused = false;
    void* texture = nullptr;  // wlroots texture for thumbnail
};

/**
 * Alt-Tab overlay state
 */
struct AltTabState {
    bool visible = false;
    int selectedIndex = 0;
    std::vector<WindowThumbnail> windows;
    
    // Layout
    int thumbnailWidth = 160;
    int thumbnailHeight = 120;
    int spacing = 20;
    int padding = 40;
};

/**
 * Alt-Tab overlay renderer
 * 
 * Renders a horizontal strip of window thumbnails at screen center.
 * Handles keyboard navigation and selection.
 */
class AltTabOverlay {
public:
    AltTabOverlay();
    ~AltTabOverlay();
    
    // Show/hide overlay
    void show(const std::vector<WindowThumbnail>& windows);
    void hide();
    void toggle();
    bool isVisible() const { return m_state.visible; }
    
    // Keyboard navigation
    void next();      // Move selection forward
    void previous();  // Move selection backward
    void select();    // Confirm selection
    void cancel();    // Cancel and hide
    
    // Selection callback
    using SelectCallback = std::function<void(uint64_t windowId)>;
    void setSelectCallback(SelectCallback cb) { m_selectCallback = cb; }
    
    // Get current state
    const AltTabState& state() const { return m_state; }
    AltTabState& state() { return m_state; }
    
    // Render overlay (called by render pipeline)
    void render(void* renderer, int screenWidth, int screenHeight);
    
private:
    void layoutThumbnails(int screenWidth, int screenHeight);
    void drawThumbnail(void* renderer, const WindowThumbnail& thumb, int x, int y, bool selected);
    void drawBackground(int screenWidth, int screenHeight);
    
    AltTabState m_state;
    SelectCallback m_selectCallback;
    
    // Cached rendering resources
    void* m_shaderProgram = nullptr;
    bool m_initialized = false;
};

} // namespace havel
