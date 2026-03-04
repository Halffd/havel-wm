#include "AltTabOverlay.hpp"
#include <wm/render/OverlayRenderer.hpp>
#include <cstdio>
#include <algorithm>

namespace havel {

AltTabOverlay::AltTabOverlay() = default;

AltTabOverlay::~AltTabOverlay() {
    hide();
}

void AltTabOverlay::show(const std::vector<WindowThumbnail>& windows) {
    if (windows.empty()) {
        printf("[AltTab] No windows to show\n");
        return;
    }
    
    m_state.windows = windows;
    m_state.visible = true;
    m_state.selectedIndex = 0;
    
    // Find currently focused window and select it
    for (size_t i = 0; i < m_state.windows.size(); i++) {
        if (m_state.windows[i].isFocused) {
            m_state.selectedIndex = static_cast<int>(i);
            break;
        }
    }
    
    printf("[AltTab] Showing overlay with %zu windows\n", windows.size());
}

void AltTabOverlay::hide() {
    if (m_state.visible) {
        m_state.visible = false;
        m_state.windows.clear();
        m_state.selectedIndex = 0;
        printf("[AltTab] Hidden\n");
    }
}

void AltTabOverlay::toggle() {
    if (m_state.visible) {
        hide();
    }
    // Show would be called separately with window list
}

void AltTabOverlay::next() {
    if (!m_state.visible || m_state.windows.empty()) return;
    
    m_state.selectedIndex++;
    if (m_state.selectedIndex >= static_cast<int>(m_state.windows.size())) {
        m_state.selectedIndex = 0;
    }
    
    printf("[AltTab] Selection: %d/%zu\n", m_state.selectedIndex + 1, m_state.windows.size());
}

void AltTabOverlay::previous() {
    if (!m_state.visible || m_state.windows.empty()) return;
    
    m_state.selectedIndex--;
    if (m_state.selectedIndex < 0) {
        m_state.selectedIndex = static_cast<int>(m_state.windows.size()) - 1;
    }
    
    printf("[AltTab] Selection: %d/%zu\n", m_state.selectedIndex + 1, m_state.windows.size());
}

void AltTabOverlay::select() {
    if (!m_state.visible || m_state.windows.empty()) return;
    
    uint64_t selectedId = m_state.windows[m_state.selectedIndex].windowId;
    printf("[AltTab] Selected window %lu\n", selectedId);
    
    hide();
    
    if (m_selectCallback) {
        m_selectCallback(selectedId);
    }
}

void AltTabOverlay::cancel() {
    printf("[AltTab] Cancelled\n");
    hide();
}

void AltTabOverlay::render(void* renderer, int screenWidth, int screenHeight) {
    if (!m_state.visible || !renderer) return;
    
    // Layout thumbnails
    layoutThumbnails(screenWidth, screenHeight);
    
    // Draw semi-transparent background
    drawBackground(screenWidth, screenHeight);
    
    // Draw thumbnails
    int totalWidth = m_state.windows.size() * (m_state.thumbnailWidth + m_state.spacing) - m_state.spacing;
    int startX = (screenWidth - totalWidth) / 2;
    int y = (screenHeight - m_state.thumbnailHeight) / 2;
    
    for (size_t i = 0; i < m_state.windows.size(); i++) {
        int x = startX + i * (m_state.thumbnailWidth + m_state.spacing);
        bool selected = (static_cast<int>(i) == m_state.selectedIndex);
        drawThumbnail(renderer, m_state.windows[i], x, y, selected);
    }
}

void AltTabOverlay::layoutThumbnails(int screenWidth, int screenHeight) {
    // Adjust thumbnail size based on screen size and window count
    int maxTotalWidth = screenWidth - 2 * m_state.padding;
    int availableWidth = maxTotalWidth;
    
    // Calculate optimal thumbnail width
    int totalSpacing = m_state.windows.size() > 1 ? 
        (m_state.windows.size() - 1) * m_state.spacing : 0;
    int thumbnailWidth = (availableWidth - totalSpacing) / m_state.windows.size();
    
    // Clamp thumbnail size
    thumbnailWidth = std::clamp(thumbnailWidth, 100, 240);
    m_state.thumbnailWidth = thumbnailWidth;
    m_state.thumbnailHeight = thumbnailWidth * 3 / 4;  // 4:3 aspect ratio
}

void AltTabOverlay::drawBackground(int screenWidth, int screenHeight) {
    // Draw semi-transparent dark overlay
    // Background: 70% black with slight blue tint
    // Note: This would be drawn by the render pipeline
    // For now, the background is handled by the overlay layer
    (void)screenWidth;
    (void)screenHeight;
}

void AltTabOverlay::drawThumbnail(void* rendererPtr, const WindowThumbnail& thumb,
                                   int x, int y, bool selected) {
    OverlayRenderer* renderer = static_cast<OverlayRenderer*>(rendererPtr);
    if (!renderer) return;

    int w = m_state.thumbnailWidth;
    int h = m_state.thumbnailHeight;

    // Draw thumbnail background (dark rectangle)
    renderer->drawRect(x, y, w, h, Color(0.1f, 0.1f, 0.15f, 0.9f));

    // Draw window texture if available
    // Note: texture field is void* - would need proper texture ID extraction
    // For now, show placeholder
    renderer->drawText(thumb.appId.c_str(), x + w/2 - 40, y + h/2 - 10, 
                      14.0f, Color(0.5f, 0.5f, 0.5f, 1.0f));

    // Draw border (highlighted if selected)
    Color borderColor = selected ? Color(0.3f, 0.6f, 1.0f, 1.0f) : Color(0.3f, 0.3f, 0.4f, 0.8f);
    float borderWidth = selected ? 3.0f : 2.0f;
    renderer->drawRect(x - borderWidth, y - borderWidth, w + borderWidth*2, borderWidth, borderColor);  // Top
    renderer->drawRect(x - borderWidth, y + h, w + borderWidth*2, borderWidth, borderColor);  // Bottom
    renderer->drawRect(x - borderWidth, y, borderWidth, h, borderColor);  // Left
    renderer->drawRect(x + w, y, borderWidth, h, borderColor);  // Right

    // Draw window title below thumbnail
    if (!thumb.title.empty()) {
        std::string title = thumb.title;
        if (title.length() > 20) {
            title = title.substr(0, 17) + "...";
        }
        renderer->drawText(title.c_str(), x + 4, y + h + 16, 
                          11.0f, selected ? Color(1.0f, 1.0f, 1.0f, 1.0f) : Color(0.8f, 0.8f, 0.8f, 1.0f));
    }
}

} // namespace havel
