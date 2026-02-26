#include "AppLauncherOverlay.hpp"
#include <cstdio>
#include <algorithm>
#include <cstring>

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

void AppLauncherOverlay::drawSearchBox(void* renderer, int x, int y, int w, int h) {
    // Would draw:
    // 1. Search box background
    // 2. Search icon
    // 3. Search text with cursor
    // 4. Placeholder text if empty
    
    (void)renderer;
    (void)x;
    (void)y;
    (void)w;
    (void)h;
    
    // Stub for now
}

void AppLauncherOverlay::drawResultsList(void* renderer, int x, int y, int w, int h) {
    // Would draw:
    // 1. Scrollable list of app results
    // 2. App icons
    // 3. App names and descriptions
    // 4. Highlight selected item
    
    (void)renderer;
    (void)x;
    (void)y;
    (void)w;
    (void)h;
    
    // Stub for now
}

void AppLauncherOverlay::drawBackground(int screenWidth, int screenHeight) {
    // Would draw semi-transparent dark overlay
    (void)screenWidth;
    (void)screenHeight;
}

void AppLauncherOverlay::scanApplications() {
    // Would scan /usr/share/applications and ~/.local/share/applications
    // Parse .desktop files
    // Populate m_state.favorites
    
    // Stub: Add some dummy entries
    AppEntry term;
    term.id = "terminal";
    term.name = "Terminal";
    term.exec = "foot";
    term.icon = "utilities-terminal";
    term.isFavorite = true;
    m_state.favorites.push_back(term);
    
    AppEntry browser;
    browser.id = "firefox";
    browser.name = "Firefox";
    browser.exec = "firefox";
    browser.icon = "firefox";
    browser.isFavorite = true;
    m_state.favorites.push_back(browser);
    
    printf("[Launcher] Scanned %zu applications\n", m_state.favorites.size());
}

} // namespace havel
