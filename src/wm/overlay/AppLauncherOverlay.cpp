#include "AppLauncherOverlay.hpp"
#include <wm/render/OverlayRenderer.hpp>
#include <cstdio>
#include <algorithm>
#include <cstring>
#include <fstream>
#include <sstream>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

namespace havel {

AppLauncherOverlay::AppLauncherOverlay() = default;

AppLauncherOverlay::~AppLauncherOverlay() {
    hide();
}

void AppLauncherOverlay::show() {
    m_state.visible = true;
    m_state.searchText.clear();
    m_state.selectedIndex = 0;
    filterResults();
    printf("[Launcher] Shown\n");
}

void AppLauncherOverlay::hide() {
    if (m_state.visible) {
        m_state.visible = false;
        m_state.searchText.clear();
        m_state.results.clear();
        printf("[Launcher] Hidden\n");
    }
}

void AppLauncherOverlay::toggle() {
    if (m_state.visible) {
        hide();
    } else {
        show();
    }
}

void AppLauncherOverlay::setSearchText(const std::string& text) {
    m_state.searchText = text;
    filterResults();
    m_state.selectedIndex = 0;
}

void AppLauncherOverlay::navigateUp() {
    if (!m_state.visible) return;
    
    if (m_state.selectedIndex > 0) {
        m_state.selectedIndex--;
    }
    printf("[Launcher] Selected item %d\n", m_state.selectedIndex + 1);
}

void AppLauncherOverlay::navigateDown() {
    if (!m_state.visible) return;
    
    if (m_state.selectedIndex + 1 < static_cast<int>(m_state.results.size())) {
        m_state.selectedIndex++;
    }
    printf("[Launcher] Selected item %d\n", m_state.selectedIndex + 1);
}

void AppLauncherOverlay::select() {
    if (!m_state.visible || m_state.results.empty()) return;
    
    if (m_state.selectedIndex >= 0 && 
        m_state.selectedIndex < static_cast<int>(m_state.results.size())) {
        
        const AppEntry& selected = m_state.results[m_state.selectedIndex];
        printf("[Launcher] Launching: %s (%s)\n", selected.name.c_str(), selected.exec.c_str());
        
        hide();
        
        if (m_launchCallback) {
            m_launchCallback(selected.exec);
        }
    }
}

void AppLauncherOverlay::cancel() {
    printf("[Launcher] Cancelled\n");
    hide();
}

void AppLauncherOverlay::backspace() {
    if (!m_state.visible) return;
    
    if (!m_state.searchText.empty()) {
        m_state.searchText.pop_back();
        filterResults();
        m_state.selectedIndex = 0;
    }
}

void AppLauncherOverlay::filterResults() {
    m_state.results.clear();
    
    if (m_state.searchText.empty()) {
        // Show favorites when no search text
        m_state.results = m_state.favorites;
        return;
    }
    
    // Filter by search text (case-insensitive)
    std::string searchLower = m_state.searchText;
    std::transform(searchLower.begin(), searchLower.end(), searchLower.begin(), ::tolower);
    
    for (const auto& app : m_state.favorites) {
        std::string nameLower = app.name;
        std::transform(nameLower.begin(), nameLower.end(), nameLower.begin(), ::tolower);
        
        if (nameLower.find(searchLower) != std::string::npos ||
            app.id.find(searchLower) != std::string::npos) {
            m_state.results.push_back(app);
        }
    }
    
    printf("[Launcher] Filtered to %zu results\n", m_state.results.size());
}

void AppLauncherOverlay::render(void* renderer, int screenWidth, int screenHeight) {
    if (!m_state.visible || !renderer) return;
    
    // Center launcher
    int x = (screenWidth - m_state.width) / 2;
    int y = (screenHeight - m_state.height) / 2;
    
    // Draw background
    drawBackground(screenWidth, screenHeight);
    
    // Draw search box at top
    drawSearchBox(renderer, x, y, m_state.width, 60);
    
    // Draw results list below search
    drawResultsList(renderer, x, y + 70, m_state.width, m_state.height - 80);
}

void AppLauncherOverlay::drawSearchBox(void* rendererPtr, int x, int y, int w, int h) {
    if (!rendererPtr) return;
    
    OverlayRenderer* renderer = static_cast<OverlayRenderer*>(rendererPtr);
    
    // Draw search box background (rounded rectangle effect)
    Color searchBg(0.15f, 0.15f, 0.2f, 0.95f);
    renderer->drawRect(x, y, w, h, searchBg);
    
    // Draw search icon (simple magnifying glass representation)
    renderer->drawText("🔍", x + 15, y + 18, 24.0f, Color(0.6f, 0.6f, 0.6f, 1.0f));
    
    // Draw search text
    if (!m_state.searchText.empty()) {
        renderer->drawText(m_state.searchText.c_str(), x + 50, y + 20, 20.0f, Color(1.0f, 1.0f, 1.0f, 1.0f));
        
        // Draw cursor
        size_t cursorPos = m_state.searchText.length();
        float cursorX = x + 50 + (cursorPos * 12);  // Approximate character width
        renderer->drawRect(cursorX, y + 18, 2, 22, Color(1.0f, 1.0f, 1.0f, 1.0f));
    } else {
        // Draw placeholder text
        renderer->drawText("Type to search applications...", x + 50, y + 20, 20.0f, Color(0.5f, 0.5f, 0.5f, 1.0f));
    }
    
    // Draw bottom border line
    renderer->drawRect(x, y + h - 1, w, 1, Color(0.3f, 0.3f, 0.4f, 1.0f));
}

void AppLauncherOverlay::drawResultsList(void* rendererPtr, int x, int y, int w, int h) {
    if (!rendererPtr) return;
    
    OverlayRenderer* renderer = static_cast<OverlayRenderer*>(rendererPtr);
    
    const int itemHeight = 60;
    const int maxVisibleItems = h / itemHeight;
    const int iconSize = 32;
    const int padding = 15;
    
    // Calculate scroll offset to keep selected item visible
    int scrollOffset = 0;
    if (m_state.selectedIndex >= maxVisibleItems) {
        scrollOffset = (m_state.selectedIndex - maxVisibleItems + 1) * itemHeight;
    }
    
    // Draw visible items
    int visibleCount = std::min(static_cast<int>(m_state.results.size()), maxVisibleItems);
    
    for (int i = 0; i < visibleCount; i++) {
        int itemIndex = i + (scrollOffset / itemHeight);
        if (itemIndex >= static_cast<int>(m_state.results.size())) break;
        
        const AppEntry& entry = m_state.results[itemIndex];
        bool isSelected = (itemIndex == m_state.selectedIndex);
        
        int itemY = y + i * itemHeight;
        
        // Draw item background
        Color itemBg = isSelected ? Color(0.3f, 0.4f, 0.5f, 0.9f) : Color(0.1f, 0.1f, 0.15f, 0.8f);
        renderer->drawRect(x + 1, itemY, w - 2, itemHeight - 1, itemBg);
        
        // Draw app icon (placeholder colored square)
        int iconX = x + padding;
        int iconY = itemY + (itemHeight - iconSize) / 2;
        
        // Draw colored placeholder based on first letter
        float hue = (entry.name[0] % 26) / 26.0f;
        Color iconColor(0.2f + hue * 0.3f, 0.3f + hue * 0.2f, 0.4f, 1.0f);
        renderer->drawRect(iconX, iconY, iconSize, iconSize, iconColor);
        
        // Draw first letter
        char firstChar[2] = {entry.name[0], '\0'};
        renderer->drawText(firstChar, iconX + 10, iconY + 20, 20.0f, Color(1.0f, 1.0f, 1.0f, 1.0f));
        
        // Draw app name
        int textX = iconX + iconSize + padding;
        int textY = itemY + 20;
        Color textColor = isSelected ? Color(1.0f, 1.0f, 1.0f, 1.0f) : Color(0.9f, 0.9f, 0.9f, 1.0f);
        renderer->drawText(entry.name.c_str(), textX, textY, 18.0f, textColor);
        
        // Draw app description (if available)
        if (!entry.description.empty()) {
            Color descColor = isSelected ? Color(0.8f, 0.8f, 0.8f, 1.0f) : Color(0.6f, 0.6f, 0.6f, 1.0f);
            renderer->drawText(entry.description.c_str(), textX, textY + 20, 14.0f, descColor);
        }
        
        // Draw keyboard shortcut hint for selected item
        if (isSelected) {
            renderer->drawText("Press Enter", x + w - 100, itemY + 22, 14.0f, Color(0.5f, 0.5f, 0.5f, 1.0f));
        }
    }
}

void AppLauncherOverlay::drawBackground(int screenWidth, int screenHeight) {
    // Draw semi-transparent dark overlay
    // Background is drawn by render() before calling other draw methods
    (void)screenWidth;
    (void)screenHeight;
}

static std::string trim(const std::string& str) {
    size_t first = str.find_first_not_of(" \t\n\r");
    if (first == std::string::npos) return "";
    size_t last = str.find_last_not_of(" \t\n\r");
    return str.substr(first, last - first + 1);
}

static std::string parseDesktopValue(const std::string& content, const std::string& key) {
    std::string searchKey = key + "=";
    size_t pos = content.find(searchKey);
    if (pos == std::string::npos) return "";
    
    size_t valueStart = pos + searchKey.length();
    size_t valueEnd = content.find('\n', valueStart);
    if (valueEnd == std::string::npos) valueEnd = content.length();
    
    return trim(content.substr(valueStart, valueEnd - valueStart));
}

void AppLauncherOverlay::scanApplications() {
    m_state.favorites.clear();
    
    // Scan standard application directories
    const char* searchPaths[] = {
        "/usr/share/applications",
        "/usr/local/share/applications",
        nullptr
    };
    
    // Also scan user's local applications
    const char* home = getenv("HOME");
    char userAppDir[512];
    if (home) {
        snprintf(userAppDir, sizeof(userAppDir), "%s/.local/share/applications", home);
        searchPaths[2] = userAppDir;
    }
    
    int totalFound = 0;
    
    for (int i = 0; searchPaths[i] != nullptr; i++) {
        DIR* dir = opendir(searchPaths[i]);
        if (!dir) continue;
        
        struct dirent* entry;
        while ((entry = readdir(dir)) != nullptr) {
            // Only process .desktop files
            const char* filename = entry->d_name;
            size_t len = strlen(filename);
            if (len < 8 || strcmp(filename + len - 8, ".desktop") != 0) continue;
            
            // Build full path
            char fullPath[512];
            snprintf(fullPath, sizeof(fullPath), "%s/%s", searchPaths[i], filename);
            
            // Check if file is readable
            struct stat st;
            if (stat(fullPath, &st) != 0 || !S_ISREG(st.st_mode)) continue;
            
            // Read file content
            std::ifstream file(fullPath);
            if (!file.is_open()) continue;
            
            std::stringstream buffer;
            buffer << file.rdbuf();
            std::string content = buffer.str();
            file.close();
            
            // Check if it's a valid application (not a link or directory entry)
            std::string type = parseDesktopValue(content, "Type");
            if (type != "Application") continue;
            
            // Check if hidden
            std::string hidden = parseDesktopValue(content, "Hidden");
            if (hidden == "true") continue;
            
            // Check for NoDisplay
            std::string noDisplay = parseDesktopValue(content, "NoDisplay");
            if (noDisplay == "true") continue;
            
            // Parse desktop entry
            AppEntry app;
            app.id = filename;  // Use filename as ID
            app.id = app.id.substr(0, app.id.length() - 8);  // Remove .desktop
            
            app.name = parseDesktopValue(content, "Name");
            if (app.name.empty()) continue;  // Name is required
            
            app.exec = parseDesktopValue(content, "Exec");
            if (app.exec.empty()) continue;  // Exec is required
            
            app.icon = parseDesktopValue(content, "Icon");
            app.description = parseDesktopValue(content, "Comment");
            app.isFavorite = false;  // Will be set based on usage
            
            // Remove field codes from exec (like %f, %u, etc.)
            size_t percentPos = app.exec.find('%');
            if (percentPos != std::string::npos) {
                app.exec = app.exec.substr(0, percentPos);
            }
            app.exec = trim(app.exec);
            
            m_state.favorites.push_back(app);
            totalFound++;
        }
        
        closedir(dir);
    }
    
    // Sort by name
    std::sort(m_state.favorites.begin(), m_state.favorites.end(),
        [](const AppEntry& a, const AppEntry& b) {
            return a.name < b.name;
        });
    
    printf("[Launcher] Scanned %d applications from system directories\n", totalFound);
}

} // namespace havel
