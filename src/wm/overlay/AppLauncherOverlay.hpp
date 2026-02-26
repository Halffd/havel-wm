#pragma once

#include <wm/Types.hpp>
#include <vector>
#include <string>
#include <functional>

namespace havel {

/**
 * Application entry for launcher
 */
struct AppEntry {
    std::string id;
    std::string name;
    std::string description;
    std::string icon;
    std::string exec;
    bool isFavorite = false;
};

/**
 * App launcher overlay state
 */
struct LauncherState {
    bool visible = false;
    std::string searchText;
    int selectedIndex = 0;
    std::vector<AppEntry> results;
    std::vector<AppEntry> favorites;
    
    // Layout
    int width = 500;
    int height = 400;
    int itemHeight = 50;
    int maxVisibleItems = 8;
};

/**
 * Application Launcher overlay
 * 
 * Searchable app launcher with favorites.
 * Similar to Spotlight/Alfred/Rofi.
 */
class AppLauncherOverlay {
public:
    AppLauncherOverlay();
    ~AppLauncherOverlay();
    
    // Show/hide overlay
    void show();
    void hide();
    void toggle();
    bool isVisible() const { return m_state.visible; }
    
    // Search
    void setSearchText(const std::string& text);
    const std::string& searchText() const { return m_state.searchText; }
    
    // Navigation
    void navigateUp();
    void navigateDown();
    void select();
    void cancel();
    void backspace();
    
    // Get state
    const LauncherState& state() const { return m_state; }
    LauncherState& state() { return m_state; }
    
    // Callbacks
    using LaunchCallback = std::function<void(const std::string& appId)>;
    void setLaunchCallback(LaunchCallback cb) { m_launchCallback = cb; }
    
    // Render overlay
    void render(void* renderer, int screenWidth, int screenHeight);
    
    // Load applications from .desktop files
    void scanApplications();
    
private:
    void filterResults();
    void drawSearchBox(void* renderer, int x, int y, int w, int h);
    void drawResultsList(void* renderer, int x, int y, int w, int h);
    void drawBackground(int screenWidth, int screenHeight);
    
    LauncherState m_state;
    LaunchCallback m_launchCallback;
    
    bool m_initialized = false;
};

} // namespace havel
