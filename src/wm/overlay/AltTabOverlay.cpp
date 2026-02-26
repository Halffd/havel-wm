#include "AltTabOverlay.hpp"
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
    // Would draw semi-transparent dark overlay
    // For now, this is a stub - actual rendering needs GLES2
    (void)screenWidth;
    (void)screenHeight;
}

void AltTabOverlay::drawThumbnail(void* renderer, const WindowThumbnail& thumb, 
                                   int x, int y, bool selected) {
    // Would draw:
    // 1. Thumbnail texture (scaled window content)
    // 2. Border (highlighted if selected)
    // 3. App icon
    // 4. Window title
    
    (void)renderer;
    (void)thumb;
    (void)x;
    (void)y;
    (void)selected;
    
    // Stub for now - actual rendering requires wlroots texture access
}

} // namespace havel
