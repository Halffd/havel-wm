// Desktop Environment Manager Implementation

#include "DesktopManager.hpp"
#include "NASAWallpaper.hpp"
#include <wm/render/OverlayRenderer.hpp>
#include <wm/render/AppIconLoader.hpp>
#include <Logger.h>
#include <algorithm>
#include <fstream>
#include <sstream>
#include <ctime>
#include <random>
#include <cstring>
#include <sys/stat.h>
#include <GLES2/gl2.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

namespace havel {

// Global desktop manager instance
static DesktopManager* g_desktopManager = nullptr;

DesktopManager& getDesktopManager() {
    if (!g_desktopManager) {
        g_desktopManager = new DesktopManager();
    }
    return *g_desktopManager;
}

// ============================================================================
// DesktopManager Implementation
// ============================================================================

DesktopManager::DesktopManager() {
    // Set default config
    m_config.iconSize = 64;
    m_config.gridSize = 80;
    m_config.iconSpacing = 10;
    m_config.margin = 20;
    m_config.showLabels = true;
    m_config.snapToGrid = true;
    m_config.showTaskbar = true;
    m_config.taskbarPosition = 1;  // Bottom
    m_config.taskbarSize = 48;
    m_config.rightClickMenu = true;
    m_config.enableFadeToDesktop = true;
}

DesktopManager::~DesktopManager() {
    shutdown();
}

bool DesktopManager::initialize() {
    if (m_initialized) {
        return true;
    }
    
    m_screenWidth = 1920;
    m_screenHeight = 1080;
    
    // Initialize NASA wallpaper manager
    if (m_nasaWallpaperEnabled) {
        m_nasaWallpaperManager = std::make_unique<NASAWallpaperManager>();
        m_nasaWallpaperManager->initialize();

        // NASA wallpaper download disabled - libcurl crashes in compositor context
        // To use: call fetchNASAWallpapers() from a separate process
    }

    // Initialize icon loader singleton
    new AppIconLoader();

    // Load default icons (applications)
    addIcon("firefox");
    addIcon("foot");
    addIcon("thunar");

    // Auto-arrange initial icons
    autoArrangeIcons();
    
    LOG_INFO("[DesktopManager] Initialized (%dx%d)", m_screenWidth, m_screenHeight);
    m_initialized = true;
    return true;
}

void DesktopManager::shutdown() {
    // Unload all icon textures
    for (auto& icon : m_icons) {
        if (icon->iconTextureId != 0) {
            unloadIconTexture(icon->iconTextureId);
        }
    }
    
    // Unload wallpaper texture
    if (m_wallpaperTexture != 0) {
        unloadIconTexture(m_wallpaperTexture);
    }
    
    m_icons.clear();
    m_selectedIcons.clear();
    m_taskbarItems.clear();
    m_contextMenuItems.clear();
    
    m_initialized = false;
    LOG_INFO("[DesktopManager] Shutdown complete");
}

void DesktopManager::show() {
    m_visible = true;
    LOG_DEBUG("[DesktopManager] Shown");
}

void DesktopManager::hide() {
    m_visible = false;
    LOG_DEBUG("[DesktopManager] Hidden");
}

void DesktopManager::toggle() {
    m_visible = !m_visible;
    LOG_DEBUG("[DesktopManager] Toggled: %s", m_visible ? "visible" : "hidden");
}

void DesktopManager::fadeToDesktop(float duration) {
    if (!m_config.enableFadeToDesktop) {
        show();
        return;
    }
    
    m_fading = true;
    m_fadeAlpha = 0.0f;
    
    // Would animate fade over duration
    // For now, instant fade
    m_fadeAlpha = 1.0f;
    m_fading = false;
    
    minimizeAllWindows();
    
    LOG_INFO("[DesktopManager] Fade to desktop");
}

DesktopIcon* DesktopManager::addIcon(const std::string& appId, int x, int y) {
    auto icon = std::make_unique<DesktopIcon>();
    icon->id = appId + "_" + std::to_string(m_icons.size());
    icon->appId = appId;
    icon->name = appId;  // Would lookup from .desktop file
    icon->exec = appId;
    icon->icon = appId;
    icon->autoPosition = (x < 0 || y < 0);
    
    if (icon->autoPosition) {
        // Will be positioned by autoArrangeIcons
        icon->x = 0;
        icon->y = 0;
    } else {
        icon->x = x;
        icon->y = y;
    }
    
    // Load icon texture
    icon->iconTextureId = loadIconTexture(icon->icon);
    icon->iconSize = m_config.iconSize;
    
    DesktopIcon* ptr = icon.get();
    m_icons.push_back(std::move(icon));
    
    LOG_INFO("[DesktopManager] Added icon: %s", appId.c_str());
    
    if (m_onIconAdded) {
        m_onIconAdded(ptr);
    }
    
    if (ptr->autoPosition) {
        autoArrangeIcons();
    }
    
    return ptr;
}

DesktopIcon* DesktopManager::addFileIcon(const std::string& filePath, int x, int y) {
    auto icon = std::make_unique<DesktopIcon>();
    icon->id = "file_" + std::to_string(m_icons.size());
    icon->filePath = filePath;
    
    // Extract filename
    size_t lastSlash = filePath.rfind('/');
    icon->name = (lastSlash != std::string::npos) ? filePath.substr(lastSlash + 1) : filePath;
    
    icon->mimeType = "application/octet-stream";  // Would detect properly
    icon->autoPosition = (x < 0 || y < 0);
    
    if (icon->autoPosition) {
        icon->x = 0;
        icon->y = 0;
    } else {
        icon->x = x;
        icon->y = y;
    }
    
    icon->icon = "text-x-generic";  // Default file icon
    icon->iconTextureId = loadIconTexture(icon->icon);
    icon->iconSize = m_config.iconSize;
    
    DesktopIcon* ptr = icon.get();
    m_icons.push_back(std::move(icon));
    
    LOG_INFO("[DesktopManager] Added file icon: %s", filePath.c_str());
    
    if (m_onIconAdded) {
        m_onIconAdded(ptr);
    }
    
    if (ptr->autoPosition) {
        autoArrangeIcons();
    }
    
    return ptr;
}

DesktopIcon* DesktopManager::addFolderIcon(const std::string& folderPath, int x, int y) {
    auto icon = std::make_unique<DesktopIcon>();
    icon->id = "folder_" + std::to_string(m_icons.size());
    icon->filePath = folderPath;
    
    // Extract folder name
    size_t lastSlash = folderPath.rfind('/');
    icon->name = (lastSlash != std::string::npos) ? folderPath.substr(lastSlash + 1) : folderPath;
    
    icon->mimeType = "inode/directory";
    icon->autoPosition = (x < 0 || y < 0);
    
    if (icon->autoPosition) {
        icon->x = 0;
        icon->y = 0;
    } else {
        icon->x = x;
        icon->y = y;
    }
    
    icon->icon = "folder";
    icon->iconTextureId = loadIconTexture(icon->icon);
    icon->iconSize = m_config.iconSize;
    
    DesktopIcon* ptr = icon.get();
    m_icons.push_back(std::move(icon));
    
    LOG_INFO("[DesktopManager] Added folder icon: %s", folderPath.c_str());
    
    if (m_onIconAdded) {
        m_onIconAdded(ptr);
    }
    
    if (ptr->autoPosition) {
        autoArrangeIcons();
    }
    
    return ptr;
}

void DesktopManager::removeIcon(const std::string& iconId) {
    auto it = std::find_if(m_icons.begin(), m_icons.end(),
        [&iconId](const std::unique_ptr<DesktopIcon>& i) {
            return i->id == iconId;
        });
    
    if (it != m_icons.end()) {
        DesktopIcon* icon = it->get();
        if (icon->iconTextureId != 0) {
            unloadIconTexture(icon->iconTextureId);
        }
        
        LOG_INFO("[DesktopManager] Removed icon: %s", iconId.c_str());
        
        if (m_onIconRemoved) {
            m_onIconRemoved(icon);
        }
        
        m_icons.erase(it);
    }
}

void DesktopManager::removeIcon(DesktopIcon* icon) {
    if (!icon) return;
    removeIcon(icon->id);
}

DesktopIcon* DesktopManager::getIcon(const std::string& iconId) {
    for (auto& icon : m_icons) {
        if (icon->id == iconId) {
            return icon.get();
        }
    }
    return nullptr;
}

DesktopIcon* DesktopManager::getIconAt(int x, int y) {
    for (auto& icon : m_icons) {
        int iconW = icon->iconSize + 10;  // Include padding
        int iconH = icon->iconSize + 40;  // Include label
        
        if (x >= icon->x && x <= icon->x + iconW &&
            y >= icon->y && y <= icon->y + iconH) {
            return icon.get();
        }
    }
    return nullptr;
}

void DesktopManager::selectIcon(DesktopIcon* icon) {
    if (!icon) return;
    
    deselectAll();
    icon->selected = true;
    m_selectedIcons.push_back(icon);
    
    LOG_DEBUG("[DesktopManager] Selected icon: %s", icon->id.c_str());
}

void DesktopManager::deselectAll() {
    for (auto* icon : m_selectedIcons) {
        icon->selected = false;
    }
    m_selectedIcons.clear();
}

void DesktopManager::selectAll() {
    deselectAll();
    for (auto& icon : m_icons) {
        icon->selected = true;
        m_selectedIcons.push_back(icon.get());
    }
}

void DesktopManager::setIconPosition(DesktopIcon* icon, int x, int y) {
    if (!icon) return;
    
    icon->x = x;
    icon->y = y;
    icon->autoPosition = false;
}

void DesktopManager::autoArrangeIcons() {
    if (m_icons.empty()) return;
    
    int x = m_config.margin;
    int y = m_config.margin;
    int rowHeight = m_config.iconSize + m_config.iconSpacing + 20;  // Icon + spacing + label
    
    int maxIconsPerColumn = (m_screenHeight - 2 * m_config.margin) / rowHeight;
    if (maxIconsPerColumn < 1) maxIconsPerColumn = 1;
    
    int column = 0;
    int iconIndex = 0;
    
    for (auto& icon : m_icons) {
        if (icon->autoPosition) {
            icon->x = x;
            icon->y = y;
            
            iconIndex++;
            if (iconIndex >= maxIconsPerColumn) {
                // Move to next column
                column++;
                x = m_config.margin + column * (m_config.iconSize + m_config.iconSpacing);
                iconIndex = 0;
                y = m_config.margin;
            } else {
                y += rowHeight;
            }
        }
    }
    
    LOG_DEBUG("[DesktopManager] Auto-arranged %zu icons", m_icons.size());
}

void DesktopManager::snapToGrid(DesktopIcon* icon) {
    if (!icon || !m_config.snapToGrid) return;
    
    int gridSize = m_config.gridSize;
    icon->x = (icon->x / gridSize) * gridSize;
    icon->y = (icon->y / gridSize) * gridSize;
}

void DesktopManager::setLayoutMode(DesktopLayoutMode mode) {
    m_config.layoutMode = mode;
    
    if (mode == DesktopLayoutMode::AutoGrid) {
        autoArrangeIcons();
    }
    
    LOG_INFO("[DesktopManager] Layout mode: %d", static_cast<int>(mode));
}

void DesktopManager::startDrag(DesktopIcon* icon, int startX, int startY) {
    if (!icon) return;
    
    m_draggingIcon = icon;
    m_dragStartX = startX;
    m_dragStartY = startY;
    icon->dragging = true;
    
    LOG_DEBUG("[DesktopManager] Started drag: %s", icon->id.c_str());
}

void DesktopManager::updateDrag(int x, int y) {
    if (!m_draggingIcon) return;
    
    int dx = x - m_dragStartX;
    int dy = y - m_dragStartY;
    
    m_draggingIcon->x += dx;
    m_draggingIcon->y += dy;
    
    m_dragStartX = x;
    m_dragStartY = y;
}

void DesktopManager::endDrag() {
    if (!m_draggingIcon) return;
    
    m_draggingIcon->dragging = false;
    
    if (m_config.snapToGrid) {
        snapToGrid(m_draggingIcon);
    }
    
    m_draggingIcon = nullptr;
    
    LOG_DEBUG("[DesktopManager] Ended drag");
}

void DesktopManager::setWallpaper(const std::string& path) {
    m_config.wallpaper.path = path;
    m_config.wallpaper.mode = WallpaperMode::Single;
    
    // Load wallpaper texture
    if (m_wallpaperTexture != 0) {
        unloadIconTexture(m_wallpaperTexture);
    }
    m_wallpaperTexture = loadIconTexture(path);
    
    LOG_INFO("[DesktopManager] Wallpaper set: %s", path.c_str());
    
    if (m_onWallpaperChanged) {
        m_onWallpaperChanged(path);
    }
}

void DesktopManager::setWallpaperMode(WallpaperMode mode) {
    m_config.wallpaper.mode = mode;
    LOG_INFO("[DesktopManager] Wallpaper mode: %d", static_cast<int>(mode));
}

void DesktopManager::setSlideshowInterval(int seconds) {
    m_config.wallpaper.slideshowInterval = seconds;
    LOG_INFO("[DesktopManager] Slideshow interval: %ds", seconds);
}

void DesktopManager::addSlideshowImage(const std::string& path) {
    m_config.wallpaper.slideshowPaths.push_back(path);
    LOG_DEBUG("[DesktopManager] Added slideshow image: %s", path.c_str());
}

void DesktopManager::setPerMonitorWallpaper(const std::string& monitorId, const std::string& path) {
    m_config.wallpaper.perMonitor = true;
    m_config.wallpaper.monitorConfigs[monitorId] = m_config.wallpaper;
    m_config.wallpaper.monitorConfigs[monitorId].path = path;
    
    LOG_INFO("[DesktopManager] Per-monitor wallpaper: %s -> %s", monitorId.c_str(), path.c_str());
}

void DesktopManager::setConfig(const DesktopConfig& config) {
    m_config = config;
    
    // Apply icon size to all icons
    for (auto& icon : m_icons) {
        icon->iconSize = m_config.iconSize;
    }
    
    if (m_config.layoutMode == DesktopLayoutMode::AutoGrid) {
        autoArrangeIcons();
    }
    
    LOG_INFO("[DesktopManager] Configuration updated");
}

void DesktopManager::loadConfig(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        LOG_WARN("[DesktopManager] Config file not found: %s", path.c_str());
        return;
    }
    
    // Would parse JSON/config file
    // For now, just log
    LOG_INFO("[DesktopManager] Loading config from: %s", path.c_str());
    
    file.close();
}

void DesktopManager::saveConfig(const std::string& path) {
    std::ofstream file(path);
    if (!file.is_open()) {
        LOG_ERROR("[DesktopManager] Cannot save config to: %s", path.c_str());
        return;
    }
    
    // Would write JSON/config file
    // Save icon positions
    for (const auto& icon : m_icons) {
        // Would serialize icon data
        (void)icon;
    }
    
    file.close();
    LOG_INFO("[DesktopManager] Config saved to: %s", path.c_str());
}

void DesktopManager::showContextMenu(int x, int y) {
    if (!m_config.rightClickMenu) return;
    
    m_contextMenuX = x;
    m_contextMenuY = y;
    m_contextMenuVisible = true;
    
    // Create default context menu
    m_contextMenuItems.clear();
    m_contextMenuItems.push_back(createDefaultContextMenu());
    
    LOG_DEBUG("[DesktopManager] Context menu shown at (%d, %d)", x, y);
}

void DesktopManager::hideContextMenu() {
    m_contextMenuVisible = false;
    m_contextMenuItems.clear();
}

void DesktopManager::addContextMenuItem(const ContextMenuItem& item) {
    m_contextMenuItems.push_back(item);
}

void DesktopManager::clearContextMenu() {
    m_contextMenuItems.clear();
}

void DesktopManager::addTaskbarItem(const std::string& appId, const std::string& title, uint32_t iconTexture) {
    TaskbarItem item;
    item.appId = appId;
    item.title = title;
    item.iconTexture = iconTexture;
    item.active = false;
    
    m_taskbarItems.push_back(item);
    
    LOG_DEBUG("[DesktopManager] Taskbar item added: %s", appId.c_str());
}

void DesktopManager::removeTaskbarItem(const std::string& appId) {
    m_taskbarItems.erase(
        std::remove_if(m_taskbarItems.begin(), m_taskbarItems.end(),
            [&appId](const TaskbarItem& item) {
                return item.appId == appId;
            }),
        m_taskbarItems.end());
    
    LOG_DEBUG("[DesktopManager] Taskbar item removed: %s", appId.c_str());
}

void DesktopManager::updateTaskbarItem(const std::string& appId, const std::string& title) {
    for (auto& item : m_taskbarItems) {
        if (item.appId == appId) {
            item.title = title;
            break;
        }
    }
}

void DesktopManager::setActiveTaskbarItem(const std::string& appId) {
    for (auto& item : m_taskbarItems) {
        item.active = (item.appId == appId);
    }
}

void DesktopManager::minimizeAllWindows() {
    m_showingDesktop = true;
    LOG_INFO("[DesktopManager] Show desktop (minimize all windows)");
    // Would signal window manager to minimize all
}

void DesktopManager::restoreWindows() {
    m_showingDesktop = false;
    LOG_INFO("[DesktopManager] Restore windows");
    // Would signal window manager to restore windows
}

void DesktopManager::bindLogout(uint32_t keycode, uint32_t modifiers) {
    LogoutBinding binding;
    binding.keycode = keycode;
    binding.modifiers = modifiers;
    m_logoutBindings.push_back(binding);
    
    LOG_INFO("[DesktopManager] Logout binding: key=%u, mods=%u", keycode, modifiers);
}

void DesktopManager::triggerLogout() {
    LOG_INFO("[DesktopManager] Logout triggered");
    
    if (m_onLogout) {
        m_onLogout("logout");
    }
}

void DesktopManager::render(void* renderer, int screenWidth, int screenHeight) {
    if (!m_visible || !m_initialized) return;
    
    m_screenWidth = screenWidth;
    m_screenHeight = screenHeight;
    
    // Render wallpaper
    renderWallpaper(renderer, screenWidth, screenHeight);
    
    // Render icons
    renderIcons(renderer);
    
    // Render context menu if visible
    if (m_contextMenuVisible) {
        renderContextMenu(renderer);
    }
    
    // Render taskbar
    if (m_config.showTaskbar) {
        renderTaskbar(renderer, screenWidth, screenHeight);
    }
}

void DesktopManager::renderIcons(void* rendererPtr) {
    if (!rendererPtr) return;
    
    OverlayRenderer* renderer = static_cast<OverlayRenderer*>(rendererPtr);
    
    const int iconSize = 64;
    const int padding = 10;
    
    for (const auto& icon : m_icons) {
        int x = icon->x;
        int y = icon->y;
        
        // Draw icon background (highlight if selected)
        if (icon->selected) {
            Color highlight(0.3f, 0.4f, 0.5f, 0.5f);
            renderer->drawRect(x - padding, y - padding, 
                              iconSize + padding * 2, iconSize + padding * 2, highlight);
        }
        
        // Draw icon texture or placeholder
        if (icon->iconTextureId != 0) {
            renderer->drawTexture(icon->iconTextureId, x, y, iconSize, iconSize, 1.0f);
        } else {
            // Draw colored placeholder based on file type
            Color iconColor;
            if (icon->mimeType.find("image") != std::string::npos) {
                iconColor = Color(0.2f, 0.4f, 0.6f, 1.0f);  // Blue for images
            } else if (icon->mimeType.find("video") != std::string::npos) {
                iconColor = Color(0.6f, 0.2f, 0.4f, 1.0f);  // Purple for videos
            } else if (icon->mimeType.find("audio") != std::string::npos) {
                iconColor = Color(0.4f, 0.6f, 0.2f, 1.0f);  // Green for audio
            } else {
                iconColor = Color(0.5f, 0.5f, 0.5f, 1.0f);  // Gray for others
            }
            renderer->drawRect(x, y, iconSize, iconSize, iconColor);
        }
        
        // Draw label below icon
        int labelY = y + iconSize + 5;
        Color labelColor(1.0f, 1.0f, 1.0f, 1.0f);
        
        // Truncate name if too long
        std::string displayName = icon->name;
        if (displayName.length() > 15) {
            displayName = displayName.substr(0, 12) + "...";
        }
        
        renderer->drawText(displayName.c_str(), x, labelY, 14.0f, labelColor);
    }
}

void DesktopManager::renderWallpaper(void* rendererPtr, int screenWidth, int screenHeight) {
    if (!rendererPtr) return;
    
    OverlayRenderer* renderer = static_cast<OverlayRenderer*>(rendererPtr);
    
    // Handle different wallpaper modes
    if (m_config.wallpaper.mode == WallpaperMode::SolidColor) {
        // Draw solid color background
        Color bgColor(m_config.wallpaper.colorR, m_config.wallpaper.colorG, m_config.wallpaper.colorB, 1.0f);
        renderer->drawRect(0, 0, screenWidth, screenHeight, bgColor);
    } else if (m_config.wallpaper.mode == WallpaperMode::Single && !m_config.wallpaper.path.empty()) {
        // Draw wallpaper image (would use loaded texture)
        // For now, draw placeholder with path text
        Color placeholder(0.1f, 0.15f, 0.2f, 1.0f);
        renderer->drawRect(0, 0, screenWidth, screenHeight, placeholder);
        renderer->drawText(m_config.wallpaper.path.c_str(), 20, screenHeight - 30, 16.0f, 
                          Color(0.5f, 0.5f, 0.5f, 1.0f));
    } else if (m_config.wallpaper.mode == WallpaperMode::Slideshow) {
        // Draw current slideshow image
        Color slideshow(0.1f, 0.1f, 0.15f, 1.0f);
        renderer->drawRect(0, 0, screenWidth, screenHeight, slideshow);
    }
    // Video mode would use video player integration
}

void DesktopManager::renderContextMenu(void* rendererPtr) {
    if (!rendererPtr || m_contextMenuItems.empty()) return;
    
    OverlayRenderer* renderer = static_cast<OverlayRenderer*>(rendererPtr);
    
    const int itemHeight = 30;
    const int menuWidth = 200;
    const int menuHeight = m_contextMenuItems.size() * itemHeight;
    const int padding = 5;
    
    // Draw menu background
    Color menuBg(0.15f, 0.15f, 0.2f, 0.95f);
    renderer->drawRect(m_contextMenuX, m_contextMenuY, menuWidth, menuHeight, menuBg);
    
    // Draw menu items
    for (size_t i = 0; i < m_contextMenuItems.size(); i++) {
        const auto& item = m_contextMenuItems[i];
        int itemY = m_contextMenuY + i * itemHeight;
        
        // Highlight first item (simplified - no selection tracking)
        if (i == 0) {
            Color highlight(0.3f, 0.4f, 0.5f, 0.9f);
            renderer->drawRect(m_contextMenuX + padding, itemY + padding, 
                              menuWidth - padding * 2, itemHeight - padding * 2, highlight);
        }
        
        // Draw item text
        Color textColor(1.0f, 1.0f, 1.0f, 1.0f);
        renderer->drawText(item.label.c_str(), m_contextMenuX + padding * 2, 
                          itemY + 18, 16.0f, textColor);
    }
}

void DesktopManager::renderTaskbar(void* rendererPtr, int screenWidth, int screenHeight) {
    if (!rendererPtr) return;
    
    OverlayRenderer* renderer = static_cast<OverlayRenderer*>(rendererPtr);
    
    const int taskbarHeight = 40;
    int taskbarY = screenHeight - taskbarHeight;  // Default to bottom
    
    // Draw taskbar background
    Color taskbarBg(0.1f, 0.1f, 0.15f, 0.9f);
    renderer->drawRect(0, taskbarY, screenWidth, taskbarHeight, taskbarBg);
    
    // Draw placeholder text (no open windows tracking yet)
    renderer->drawText("Taskbar - open windows will appear here", 20, taskbarY + 25, 
                      14.0f, Color(0.6f, 0.6f, 0.6f, 1.0f));
}

void DesktopManager::processMouseMove(int x, int y) {
    if (m_draggingIcon) {
        updateDrag(x, y);
    }
}

void DesktopManager::processMouseButton(int button, bool pressed, int x, int y) {
    if (button == 0x111 && pressed) {  // Right click
        if (m_config.rightClickMenu) {
            showContextMenu(x, y);
        }
    } else if (button == 0x110 && pressed) {  // Left click
        DesktopIcon* icon = getIconAt(x, y);
        if (icon) {
            selectIcon(icon);
            startDrag(icon, x, y);
        } else {
            deselectAll();
            hideContextMenu();
        }
    } else if (button == 0x110 && !pressed) {  // Left release
        endDrag();
    }
}

void DesktopManager::processMouseWheel(int delta, int x, int y) {
    // Could use for icon size adjustment with Ctrl
    (void)delta;
    (void)x;
    (void)y;
}

void DesktopManager::processKeyDown(uint32_t keycode, uint32_t modifiers) {
    // Check logout bindings
    for (const auto& binding : m_logoutBindings) {
        if (keycode == binding.keycode && modifiers == binding.modifiers) {
            triggerLogout();
            return;
        }
    }
    
    // Desktop shortcuts
    if (keycode == 39 && (modifiers & (1 << 6))) {  // Meta+D
        // Show desktop
        if (m_showingDesktop) {
            restoreWindows();
        } else {
            fadeToDesktop();
        }
    }
}

void DesktopManager::updateSlideshow() {
    uint64_t now = 1;  // Would use actual timestamp
    
    if (now - m_lastSlideshowChange >= static_cast<uint64_t>(m_config.wallpaper.slideshowInterval * 1000)) {
        if (!m_config.wallpaper.slideshowPaths.empty()) {
            m_currentSlideshowIndex = (m_currentSlideshowIndex + 1) % m_config.wallpaper.slideshowPaths.size();
            setWallpaper(m_config.wallpaper.slideshowPaths[m_currentSlideshowIndex]);
            m_lastSlideshowChange = now;
        }
    }
}

void DesktopManager::updateVideoWallpaper() {
    // Would update video frame
    // Requires video player integration
}

ContextMenuItem DesktopManager::createDefaultContextMenu() {
    ContextMenuItem menu;
    menu.label = "Desktop";
    
    // Add default items
    ContextMenuItem newItem;
    newItem.label = "New Folder";
    newItem.icon = "folder-new";
    newItem.callback = [this]() {
        LOG_INFO("[Desktop] Create new folder");
    };
    menu.subItems.push_back(newItem);
    
    ContextMenuItem separator;
    separator.isSeparator = true;
    menu.subItems.push_back(separator);
    
    ContextMenuItem arrangeItem;
    arrangeItem.label = "Arrange Icons";
    arrangeItem.icon = "view-sort";
    arrangeItem.callback = [this]() {
        autoArrangeIcons();
    };
    menu.subItems.push_back(arrangeItem);
    
    ContextMenuItem refreshItem;
    refreshItem.label = "Refresh";
    refreshItem.icon = "view-refresh";
    refreshItem.callback = [this]() {
        LOG_INFO("[Desktop] Refresh");
    };
    menu.subItems.push_back(refreshItem);
    
    ContextMenuItem separator2;
    separator2.isSeparator = true;
    menu.subItems.push_back(separator2);
    
    ContextMenuItem settingsItem;
    settingsItem.label = "Desktop Settings";
    settingsItem.icon = "preferences-desktop";
    settingsItem.callback = [this]() {
        LOG_INFO("[Desktop] Open settings");
    };
    menu.subItems.push_back(settingsItem);
    
    return menu;
}

void DesktopManager::executeContextMenuAction(const std::string& action) {
    if (action == "new_folder") {
        LOG_INFO("[Desktop] Create new folder");
    } else if (action == "arrange") {
        autoArrangeIcons();
    } else if (action == "refresh") {
        LOG_INFO("[Desktop] Refresh");
    } else if (action == "settings") {
        LOG_INFO("[Desktop] Open settings");
    }
}

uint32_t DesktopManager::loadIconTexture(const std::string& iconPath) {
    // Load icon from system theme or file path
    // Returns OpenGL texture ID
    
    if (iconPath.empty()) {
        return 0;
    }
    
    // Check if it's a file path
    struct stat st;
    if (stat(iconPath.c_str(), &st) == 0 && S_ISREG(st.st_mode)) {
        // Load from file using AppIconLoader
        return AppIconLoader::getInstance()->loadIcon(iconPath);
    }
    
    // Load from icon theme
    return AppIconLoader::getInstance()->loadIcon(iconPath);
}

void DesktopManager::unloadIconTexture(uint32_t textureId) {
    // Delete OpenGL texture
    if (textureId != 0) {
        glDeleteTextures(1, &textureId);
    }
}

// ============================================================================
// NASA Wallpaper Integration
// ============================================================================

void DesktopManager::enableNASAWallpaper(bool enabled) {
    m_nasaWallpaperEnabled = enabled;
    
    if (enabled && !m_nasaWallpaperManager) {
        m_nasaWallpaperManager = std::make_unique<NASAWallpaperManager>();
        m_nasaWallpaperManager->initialize();
        fetchNASAWallpapers(10);
    }
    
    LOG_INFO("[Desktop] NASA wallpaper %s", enabled ? "enabled" : "disabled");
}

void DesktopManager::fetchNASAWallpapers(int count) {
    if (!m_nasaWallpaperManager) {
        LOG_ERROR("[Desktop] NASA wallpaper manager not initialized");
        return;
    }
    
    LOG_INFO("[Desktop] Fetching %d NASA wallpapers", count);
    
    // Fetch APOD images
    m_nasaWallpaperManager->fetchAPOD(count);
    
    // Download and set as wallpaper
    const NASAImage* image = m_nasaWallpaperManager->getCurrentImage();
    if (image && image->isValid()) {
        m_nasaWallpaperManager->downloadCurrentImage();
        m_nasaWallpaperManager->setAsWallpaper();
    }
}

void DesktopManager::setNASAWallpaperSlideshow(int intervalSeconds) {
    if (!m_nasaWallpaperManager) {
        LOG_ERROR("[Desktop] NASA wallpaper manager not initialized");
        return;
    }
    
    m_nasaWallpaperManager->startSlideshow(intervalSeconds);
    LOG_INFO("[Desktop] NASA wallpaper slideshow started (%ds)", intervalSeconds);
}

} // namespace havel
