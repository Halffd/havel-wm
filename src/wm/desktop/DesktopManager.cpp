// Desktop Environment Manager Implementation

#include "DesktopManager.hpp"
#include "NASAWallpaper.hpp"
#include <Logger.h>
#include <algorithm>
#include <fstream>
#include <sstream>
#include <ctime>
#include <random>
#include <cstring>

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

        // DISABLED: Fetch NASA wallpapers (crashes with libcurl)
        // fetchNASAWallpapers(10);

        // Set up NASA wallpaper slideshow
        setNASAWallpaperSlideshow(300);  // 5 minutes
    }
    
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

void DesktopManager::renderIcons(void* renderer) {
    if (!renderer) return;
    
    // Would use actual OverlayRenderer
    // For now, just log
    for (const auto& icon : m_icons) {
        // Would render icon texture at (icon->x, icon->y)
        // Would render label below icon
        (void)icon;
    }
}

void DesktopManager::renderWallpaper(void* renderer, int screenWidth, int screenHeight) {
    if (!renderer) return;
    
    // Would render wallpaper texture
    // Handle different modes (single, slideshow, video, solid color)
    (void)screenWidth;
    (void)screenHeight;
}

void DesktopManager::renderContextMenu(void* renderer) {
    if (!renderer || m_contextMenuItems.empty()) return;
    
    // Would render context menu at (m_contextMenuX, m_contextMenuY)
    // For now, just log
    LOG_DEBUG("[DesktopManager] Rendering context menu");
}

void DesktopManager::renderTaskbar(void* renderer, int screenWidth, int screenHeight) {
    if (!renderer) return;
    
    // Would render taskbar based on position and size
    // Would render taskbar items
    (void)screenWidth;
    (void)screenHeight;
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
    // Would load icon from system theme or file
    // Returns OpenGL texture ID
    // For now, return 0 (placeholder)
    (void)iconPath;
    return 0;
}

void DesktopManager::unloadIconTexture(uint32_t textureId) {
    // Would delete OpenGL texture
    (void)textureId;
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
