// App Launcher Plugin - Phase 4.3
// Desktop application launcher with fuzzy search

#include <wm/plugins/Plugin.hpp>
#include <wm/plugins/CompositorAPI.hpp>
#include <cstdio>
#include <cstring>
#include <vector>
#include <string>
#include <algorithm>
#include <fstream>
#include <sstream>

namespace havel {

/**
 * Desktop entry representation
 */
struct DesktopEntry {
    std::string id;
    std::string name;
    std::string exec;
    std::string comment;
    std::string icon;
    std::string categories;
    bool noDisplay = false;
};

/**
 * App Launcher Plugin
 * 
 * Provides application launching via:
 * - .desktop file parsing
 * - Fuzzy search matching
 * - Keyboard navigation
 * 
 * Keybindings:
 * - Meta+Space: Toggle launcher
 * - Enter: Launch selected app
 * - Escape: Close launcher
 * - Up/Down: Navigate results
 */
class AppLauncherPlugin : public Plugin {
public:
    const char* name() const override { return "app_launcher"; }
    const char* version() const override { return "0.1.0"; }
    
    void init(CompositorAPI* api) override {
        m_api = api;
        m_visible = false;
        m_selectedIndex = 0;
        
        // Scan for applications
        scanDesktopFiles();
        
        printf("[AppLauncher] Initialized (%zu applications)\n", m_entries.size());
    }
    
    void fini() override {
        printf("[AppLauncher] Finalized\n");
        m_api = nullptr;
    }
    
    void loadConfig(const std::string& configPath) override {
        // Would load custom search paths, favorites, etc.
        (void)configPath;
        printf("[AppLauncher] Config loaded\n");
    }
    
    bool onKey(const KeyEvent& event) override {
        constexpr uint32_t MOD_LOGO = 1 << 6;
        
        // Meta+Space toggles launcher
        if (event.pressed && (event.modifiers & MOD_LOGO) && event.keycode == 57) {
            toggleLauncher();
            return true;
        }
        
        if (!m_visible) return false;
        if (!event.pressed) return false;
        
        switch (event.keycode) {
            case 111:  // Escape - close
                hideLauncher();
                return true;
                
            case 28:   // Enter - launch
                launchSelected();
                return true;
                
            case 103:  // Up
                navigate(-1);
                return true;
                
            case 108:  // Down
                navigate(1);
                return true;
                
            case 14:   // Backspace
                handleBackspace();
                return true;
        }
        
        // Text input (a-z, 0-9)
        if (event.keycode >= 2 && event.keycode <= 11) {  // Number row
            handleCharInput(getCharFromKeycode(event.keycode));
            return true;
        }
        if (event.keycode >= 16 && event.keycode <= 26) {  // Q-P
            handleCharInput(getCharFromKeycode(event.keycode));
            return true;
        }
        if (event.keycode >= 30 && event.keycode <= 40) {  // A-L
            handleCharInput(getCharFromKeycode(event.keycode));
            return true;
        }
        if (event.keycode >= 43 && event.keycode <= 53) {  // Z-M
            handleCharInput(getCharFromKeycode(event.keycode));
            return true;
        }
        if (event.keycode == 57) {  // Space
            handleCharInput(' ');
            return true;
        }
        
        return false;
    }
    
private:
    CompositorAPI* m_api = nullptr;
    bool m_visible;
    int m_selectedIndex;
    std::string m_searchText;
    std::vector<DesktopEntry> m_entries;
    std::vector<DesktopEntry> m_filtered;
    
    void scanDesktopFiles() {
        // Standard locations
        const char* paths[] = {
            "/usr/share/applications/",
            "/usr/local/share/applications/",
            nullptr
        };
        
        for (int i = 0; paths[i] != nullptr; i++) {
            scanDirectory(paths[i]);
        }
        
        // Sort by name
        std::sort(m_entries.begin(), m_entries.end(),
            [](const DesktopEntry& a, const DesktopEntry& b) {
                return a.name < b.name;
            });
    }
    
    void scanDirectory(const char* path) {
        // Would use opendir/readdir in real implementation
        // For now, this is a stub showing the pattern
        (void)path;
        
        // Example stub entries
        DesktopEntry term;
        term.id = "terminal";
        term.name = "Terminal";
        term.exec = "foot";
        term.comment = "Terminal emulator";
        term.icon = "utilities-terminal";
        term.noDisplay = false;
        m_entries.push_back(term);
        
        DesktopEntry browser;
        browser.id = "firefox";
        browser.name = "Firefox";
        browser.exec = "firefox";
        browser.comment = "Web browser";
        browser.icon = "firefox";
        browser.noDisplay = false;
        m_entries.push_back(browser);
        
        DesktopEntry files;
        files.id = "files";
        files.name = "File Manager";
        files.exec = "thunar";
        files.comment = "Browse files";
        files.icon = "system-file-manager";
        files.noDisplay = false;
        m_entries.push_back(files);
    }
    
    void parseDesktopFile(const std::string& path, DesktopEntry& entry) {
        std::ifstream file(path);
        if (!file.is_open()) return;
        
        std::string line;
        std::string currentGroup;
        
        while (std::getline(file, line)) {
            // Skip comments and empty lines
            if (line.empty() || line[0] == '#') continue;
            
            // Section header
            if (line[0] == '[') {
                size_t end = line.find(']');
                if (end != std::string::npos) {
                    currentGroup = line.substr(1, end - 1);
                }
                continue;
            }
            
            // Only parse [Desktop Entry] section
            if (currentGroup != "Desktop Entry") continue;
            
            // Parse key=value
            size_t eq = line.find('=');
            if (eq == std::string::npos) continue;
            
            std::string key = line.substr(0, eq);
            std::string value = line.substr(eq + 1);
            
            // Remove locale suffix (e.g., [en_US])
            size_t bracket = key.find('[');
            if (bracket != std::string::npos) continue;
            
            if (key == "Name") entry.name = value;
            else if (key == "Exec") entry.exec = value;
            else if (key == "Comment") entry.comment = value;
            else if (key == "Icon") entry.icon = value;
            else if (key == "Categories") entry.categories = value;
            else if (key == "NoDisplay") entry.noDisplay = (value == "true");
        }
        
        file.close();
    }
    
    void toggleLauncher() {
        m_visible = !m_visible;
        if (m_visible) {
            m_searchText.clear();
            m_selectedIndex = 0;
            filterResults();
            printf("[AppLauncher] Shown\n");
        } else {
            printf("[AppLauncher] Hidden\n");
        }
    }
    
    void hideLauncher() {
        m_visible = false;
        m_searchText.clear();
        printf("[AppLauncher] Hidden\n");
    }
    
    void navigate(int delta) {
        if (m_filtered.empty()) return;
        
        m_selectedIndex += delta;
        if (m_selectedIndex < 0) m_selectedIndex = 0;
        if (m_selectedIndex >= (int)m_filtered.size()) {
            m_selectedIndex = (int)m_filtered.size() - 1;
        }
        
        printf("[AppLauncher] Selected: %s\n", m_filtered[m_selectedIndex].name.c_str());
    }
    
    void handleCharInput(char c) {
        m_searchText += c;
        m_selectedIndex = 0;
        filterResults();
        printf("[AppLauncher] Search: %s (%zu results)\n", 
               m_searchText.c_str(), m_filtered.size());
    }
    
    void handleBackspace() {
        if (m_searchText.empty()) return;
        
        m_searchText.pop_back();
        m_selectedIndex = 0;
        filterResults();
        printf("[AppLauncher] Search: %s (%zu results)\n",
               m_searchText.c_str(), m_filtered.size());
    }
    
    void filterResults() {
        m_filtered.clear();
        
        if (m_searchText.empty()) {
            // Show all (or favorites)
            for (const auto& entry : m_entries) {
                if (!entry.noDisplay) {
                    m_filtered.push_back(entry);
                }
            }
            return;
        }
        
        // Fuzzy search
        std::string searchLower = toLower(m_searchText);
        
        for (const auto& entry : m_entries) {
            if (entry.noDisplay) continue;
            
            std::string nameLower = toLower(entry.name);
            std::string execLower = toLower(entry.exec);
            
            // Simple substring match (fuzzy would use Levenshtein)
            if (nameLower.find(searchLower) != std::string::npos ||
                execLower.find(searchLower) != std::string::npos ||
                toLower(entry.comment).find(searchLower) != std::string::npos) {
                m_filtered.push_back(entry);
            }
        }
    }
    
    void launchSelected() {
        if (m_filtered.empty() || m_selectedIndex < 0 ||
            m_selectedIndex >= (int)m_filtered.size()) {
            return;
        }
        
        const DesktopEntry& entry = m_filtered[m_selectedIndex];
        printf("[AppLauncher] Launching: %s (%s)\n", 
               entry.name.c_str(), entry.exec.c_str());
        
        // Would spawn the application
        // For now, just log
        hideLauncher();
    }
    
    std::string toLower(const std::string& str) {
        std::string result = str;
        std::transform(result.begin(), result.end(), result.begin(), ::tolower);
        return result;
    }
    
    char getCharFromKeycode(uint32_t keycode) {
        // Simple mapping (would need proper xkb for real implementation)
        static const char keymap[] = {
            0, 0, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=',
            0, 0, 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']',
            0, 0, 'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`',
            0, 0, '\\', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0
        };
        
        if (keycode < sizeof(keymap)) {
            return keymap[keycode];
        }
        return 0;
    }
};

// Plugin factory
Plugin* create_app_launcher_plugin() {
    return new AppLauncherPlugin();
}

} // namespace havel
