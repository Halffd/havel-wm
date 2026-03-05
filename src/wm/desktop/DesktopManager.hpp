// Desktop Environment Manager - Icons, Wallpaper, Taskbar, Menus

#pragma once

#include <wm/Types.hpp>
#include <vector>
#include <string>
#include <memory>
#include <functional>
#include <unordered_map>

// Forward declaration
namespace havel {
class NASAWallpaperManager;
}

namespace havel {

/**
 * Desktop icon representation
 */
struct DesktopIcon {
    std::string id;
    std::string name;
    std::string appId;
    std::string exec;
    std::string icon;
    std::string comment;
    
    // Position
    int x = 0;
    int y = 0;
    bool autoPosition = true;
    
    // State
    bool selected = false;
    bool dragging = false;
    
    // Metadata
    uint64_t lastModified = 0;
    std::string filePath;  // For files/folders
    std::string mimeType;
    
    // Rendering
    uint32_t iconTextureId = 0;
    int iconSize = 64;
};

/**
 * Desktop layout modes
 */
enum class DesktopLayoutMode {
    AutoGrid,       // Automatic grid layout
    Manual,         // Free positioning
    Cascade,        // Cascading windows
    Columns,        // Column-based
};

/**
 * Wallpaper modes
 */
enum class WallpaperMode {
    Single,         // Single image
    Slideshow,      // Rotate images
    Video,          // Video wallpaper
    GIF,            // Animated GIF
    SolidColor,     // Solid color
};

/**
 * Wallpaper configuration
 */
struct WallpaperConfig {
    WallpaperMode mode = WallpaperMode::Single;
    std::string path;
    std::vector<std::string> slideshowPaths;
    int slideshowInterval = 300;  // seconds
    std::string videoPath;
    std::string gifPath;
    
    // Appearance
    float scale = 1.0f;
    int offsetX = 0;
    int offsetY = 0;
    
    // Solid color
    float colorR = 0.1f;
    float colorG = 0.1f;
    float colorB = 0.15f;
    
    // Per-monitor
    bool perMonitor = false;
    std::unordered_map<std::string, WallpaperConfig> monitorConfigs;
};

/**
 * Desktop configuration
 */
struct DesktopConfig {
    // Layout
    DesktopLayoutMode layoutMode = DesktopLayoutMode::AutoGrid;
    int gridSize = 80;
    int iconSpacing = 10;
    int margin = 20;
    
    // Icons
    int iconSize = 64;
    int fontSize = 12;
    bool showLabels = true;
    bool snapToGrid = true;
    
    // Behavior
    bool showHiddenFiles = false;
    bool singleClick = false;
    bool enableTooltips = true;
    
    // Wallpaper
    WallpaperConfig wallpaper;
    
    // Taskbar
    bool showTaskbar = true;
    int taskbarPosition = 1;  // 0=left, 1=bottom, 2=right, 3=top
    int taskbarSize = 48;
    bool autoHide = false;
    
    // Desktop actions
    bool rightClickMenu = true;
    bool showDesktopShortcut = true;
    bool enableFadeToDesktop = true;
};

/**
 * Context menu item
 */
struct ContextMenuItem {
    std::string label;
    std::string icon;
    std::function<void()> callback;
    std::string shortcut;
    bool enabled = true;
    bool isSeparator = false;
    
    // Submenu
    std::vector<ContextMenuItem> subItems;
};

/**
 * Desktop Manager - Complete desktop environment
 */
class DesktopManager {
public:
    DesktopManager();
    ~DesktopManager();

    // Initialize
    bool initialize();
    void shutdown();
    
    // Desktop state
    bool isVisible() const { return m_visible; }
    void show();
    void hide();
    void toggle();
    
    // Fade to desktop
    void fadeToDesktop(float duration = 0.3f);
    bool isFading() const { return m_fading; }
    float getFadeAlpha() const { return m_fadeAlpha; }
    
    // Icon management
    DesktopIcon* addIcon(const std::string& appId, int x = -1, int y = -1);
    DesktopIcon* addFileIcon(const std::string& filePath, int x = -1, int y = -1);
    DesktopIcon* addFolderIcon(const std::string& folderPath, int x = -1, int y = -1);
    void removeIcon(const std::string& iconId);
    void removeIcon(DesktopIcon* icon);
    DesktopIcon* getIcon(const std::string& iconId);
    DesktopIcon* getIconAt(int x, int y);
    const std::vector<std::unique_ptr<DesktopIcon>>& getAllIcons() const { return m_icons; }
    
    // Icon selection
    void selectIcon(DesktopIcon* icon);
    void deselectAll();
    void selectAll();
    const std::vector<DesktopIcon*>& getSelectedIcons() const { return m_selectedIcons; }
    
    // Icon positioning
    void setIconPosition(DesktopIcon* icon, int x, int y);
    void autoArrangeIcons();
    void snapToGrid(DesktopIcon* icon);
    void setLayoutMode(DesktopLayoutMode mode);
    DesktopLayoutMode getLayoutMode() const { return m_config.layoutMode; }
    
    // Drag and drop
    void startDrag(DesktopIcon* icon, int startX, int startY);
    void updateDrag(int x, int y);
    void endDrag();
    bool isDragging() const { return m_draggingIcon != nullptr; }
    
    // Wallpaper
    void setWallpaper(const std::string& path);
    void setWallpaperMode(WallpaperMode mode);
    void setSlideshowInterval(int seconds);
    void addSlideshowImage(const std::string& path);
    void setPerMonitorWallpaper(const std::string& monitorId, const std::string& path);
    const WallpaperConfig& getWallpaperConfig() const { return m_config.wallpaper; }
    
    // NASA wallpaper
    void enableNASAWallpaper(bool enabled = true);
    bool isNASAWallpaperEnabled() const { return m_nasaWallpaperEnabled; }
    void fetchNASAWallpapers(int count = 10);
    void setNASAWallpaperSlideshow(int intervalSeconds = 300);
    
    // Configuration
    void setConfig(const DesktopConfig& config);
    const DesktopConfig& getConfig() const { return m_config; }
    void loadConfig(const std::string& path);
    void saveConfig(const std::string& path);
    
    // Context menu
    void showContextMenu(int x, int y);
    void hideContextMenu();
    bool isContextMenuVisible() const { return m_contextMenuVisible; }
    void addContextMenuItem(const ContextMenuItem& item);
    void clearContextMenu();
    
    // Taskbar integration
    void addTaskbarItem(const std::string& appId, const std::string& title, uint32_t iconTexture);
    void removeTaskbarItem(const std::string& appId);
    void updateTaskbarItem(const std::string& appId, const std::string& title);
    void setActiveTaskbarItem(const std::string& appId);
    
    // Show desktop
    void minimizeAllWindows();
    void restoreWindows();
    bool isShowingDesktop() const { return m_showingDesktop; }
    
    // Logout bindings
    void bindLogout(uint32_t keycode, uint32_t modifiers);
    void triggerLogout();
    
    // Rendering
    void render(void* renderer, int screenWidth, int screenHeight);
    void renderIcons(void* renderer);
    void renderWallpaper(void* renderer, int screenWidth, int screenHeight);
    void renderContextMenu(void* renderer);
    void renderTaskbar(void* renderer, int screenWidth, int screenHeight);
    
    // Event handling
    void processMouseMove(int x, int y);
    void processMouseButton(int button, bool pressed, int x, int y);
    void processMouseWheel(int delta, int x, int y);
    void processKeyDown(uint32_t keycode, uint32_t modifiers);
    
    // Callbacks
    using IconCallback = std::function<void(DesktopIcon*)>;
    using StringCallback = std::function<void(const std::string&)>;
    
    void setOnIconActivated(IconCallback cb) { m_onIconActivated = cb; }
    void setOnIconAdded(IconCallback cb) { m_onIconAdded = cb; }
    void setOnIconRemoved(IconCallback cb) { m_onIconRemoved = cb; }
    void setOnWallpaperChanged(StringCallback cb) { m_onWallpaperChanged = cb; }
    void setOnLogout(StringCallback cb) { m_onLogout = cb; }

private:
    // Internal methods
    void updateSlideshow();
    void updateVideoWallpaper();
    void renderDesktopBackground(void* renderer, int w, int h);
    ContextMenuItem createDefaultContextMenu();
    void executeContextMenuAction(const std::string& action);
    
    // Icon loading
    uint32_t loadIconTexture(const std::string& iconPath);
    void unloadIconTexture(uint32_t textureId);
    
    std::vector<std::unique_ptr<DesktopIcon>> m_icons;
    std::vector<DesktopIcon*> m_selectedIcons;
    DesktopIcon* m_draggingIcon = nullptr;
    int m_dragStartX = 0;
    int m_dragStartY = 0;
    
    DesktopConfig m_config;
    
    // State
    bool m_visible = true;
    bool m_showingDesktop = false;
    bool m_fading = false;
    float m_fadeAlpha = 0.0f;
    bool m_contextMenuVisible = false;
    int m_contextMenuX = 0;
    int m_contextMenuY = 0;
    
    // Context menu
    std::vector<ContextMenuItem> m_contextMenuItems;
    
    // Taskbar
    struct TaskbarItem {
        std::string appId;
        std::string title;
        uint32_t iconTexture;
        bool active = false;
    };
    std::vector<TaskbarItem> m_taskbarItems;
    
    // Logout bindings
    struct LogoutBinding {
        uint32_t keycode;
        uint32_t modifiers;
    };
    std::vector<LogoutBinding> m_logoutBindings;
    
    // Wallpaper state
    uint64_t m_lastSlideshowChange = 0;
    int m_currentSlideshowIndex = 0;
    void* m_videoPlayer = nullptr;
    
    // NASA wallpaper
    bool m_nasaWallpaperEnabled = true;
    std::unique_ptr<NASAWallpaperManager> m_nasaWallpaperManager;
    
    // Rendering
    uint32_t m_wallpaperTexture = 0;
    int m_screenWidth = 0;
    int m_screenHeight = 0;
    
    // Callbacks
    IconCallback m_onIconActivated;
    IconCallback m_onIconAdded;
    IconCallback m_onIconRemoved;
    StringCallback m_onWallpaperChanged;
    StringCallback m_onLogout;
    
    bool m_initialized = false;
};

/**
 * Global desktop manager access
 */
DesktopManager& getDesktopManager();

} // namespace havel
