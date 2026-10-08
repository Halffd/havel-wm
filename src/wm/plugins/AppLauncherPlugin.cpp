// App Launcher Plugin - Phase 4.3
// Desktop application launcher with fuzzy search and IME support

#include <wm/plugins/Plugin.hpp>
#include <wm/plugins/CompositorAPI.hpp>
#include <wm/plugins/PluginConfig.hpp>
#include <wm/render/OverlayRenderer.hpp>
#include <wm/bridge.h>
#include <input/TextInputManager.hpp>
#include <xkbcommon/xkbcommon-keysyms.h>
#include <cstdio>
#include <chrono>
#include <cstring>
#include <vector>
#include <string>
#include <algorithm>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <dirent.h>
#include <unistd.h>

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
 * - UTF-8 text input (multi-byte support)
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
        (void)configPath;
        // Configurable toggle binding (default Meta+D)
        auto& config = PluginConfig::getInstance();
        uint32_t mods = 0, sym = 0;
        if (config.getKeybindingParsed("app_launcher", &mods, &sym)) {
            m_bindingMods = mods;
            m_bindingKeysym = sym;
            printf("[AppLauncher] Toggle binding from config: mods=0x%x keysym=0x%x\n",
                   mods, sym);
        }
        printf("[AppLauncher] Config loaded\n");
    }

    bool onKey(const KeyEvent& event) override {
        // Configurable toggle binding (default Meta+D); exact modifier match
        if (event.pressed && event.modifiers == m_bindingMods &&
            event.keysym == m_bindingKeysym) {
            toggleLauncher();
            return true;
        }

        if (!m_visible) return false;
        if (!event.pressed) return false;

        switch (event.keysym) {
            case XKB_KEY_Escape:  // Escape - close
                hideLauncher();
                return true;

            case XKB_KEY_Return:  // Enter - launch
            case XKB_KEY_KP_Enter:
                launchSelected();
                return true;

            case XKB_KEY_Up:  // Up
                navigate(-1);
                return true;

            case XKB_KEY_Down:  // Down
                navigate(1);
                return true;

            case XKB_KEY_BackSpace:  // Backspace
                handleBackspace();
                return true;
        }

        // Text input using xkbcommon (layout-aware, UTF-8)
        // event.utf8 contains the full UTF-8 string from xkb_keysym_to_utf8()
        if (event.utf8[0] != '\0') {
            // Pass full UTF-8 string (handles multi-byte characters)
            handleCharInput(event.utf8);
            return true;
        }

        // Handle special keys via keysym
        switch (event.keysym) {
            case XKB_KEY_Tab:  // Tab
                handleCharInput("\t");
                return true;
            case XKB_KEY_Return:  // Enter
                handleCharInput("\n");
                return true;
            case XKB_KEY_Escape:  // Escape - close launcher
                if (m_visible) {
                    toggleLauncher();
                }
                return true;

            // Handle additional special keysyms
            case XKB_KEY_BackSpace:  // Backspace
                handleBackspace();
                return true;
            case XKB_KEY_Delete:  // Delete
                // Could implement delete handling
                return true;
        }

        return false;
    }
    
private:
    CompositorAPI* m_api = nullptr;
    bool m_visible;
    int m_selectedIndex;
    // Toggle binding (overridable via config "keybinding", default Meta+D)
    uint32_t m_bindingMods = 1u << 6;  // Mod4 (Meta/Logo)
    uint32_t m_bindingKeysym = XKB_KEY_d;
    std::string m_searchText;
    std::vector<DesktopEntry> m_entries;
    std::chrono::steady_clock::time_point m_lastDraw;  // 2 Hz redraw throttle
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

        // Also scan user's local applications
        const char* home = getenv("HOME");
        if (home) {
            std::string localPath = std::string(home) + "/.local/share/applications/";
            scanDirectory(localPath.c_str());
        }

        // Remove duplicates (by name)
        std::sort(m_entries.begin(), m_entries.end(),
            [](const DesktopEntry& a, const DesktopEntry& b) {
                return a.name < b.name;
            });
        
        auto last = std::unique(m_entries.begin(), m_entries.end(),
            [](const DesktopEntry& a, const DesktopEntry& b) {
                return a.name == b.name;
            });
        m_entries.erase(last, m_entries.end());

        printf("[AppLauncher] Scanned %zu applications\n", m_entries.size());
    }
    
    void scanDirectory(const char* path) {
        DIR* dir = opendir(path);
        if (!dir) return;

        struct dirent* entry;
        while ((entry = readdir(dir)) != nullptr) {
            std::string filename = entry->d_name;
            
            // Only process .desktop files
            if (filename.length() > 8 && 
                filename.substr(filename.length() - 8) == ".desktop") {
                
                std::string fullPath = std::string(path) + filename;
                DesktopEntry de;
                de.id = fullPath;
                de.noDisplay = true;
                
                parseDesktopFile(fullPath, de);
                
                if (!de.noDisplay && !de.exec.empty()) {
                    // Remove field codes from Exec (%f, %F, %u, %U, etc.)
                    size_t pctPos = de.exec.find('%');
                    if (pctPos != std::string::npos) {
                        de.exec = de.exec.substr(0, pctPos);
                    }
                    // Trim trailing whitespace
                    while (!de.exec.empty() && (de.exec.back() == ' ' || de.exec.back() == '\t')) {
                        de.exec.pop_back();
                    }
                    m_entries.push_back(de);
                }
            }
        }

        closedir(dir);
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

            // Trim whitespace
            while (!key.empty() && (key.back() == ' ' || key.back() == '\t')) key.pop_back();
            while (!value.empty() && (value.front() == ' ' || value.front() == '\t')) value.erase(0, 1);

            if (key == "Name") entry.name = value;
            else if (key == "Exec") entry.exec = value;
            else if (key == "Comment") entry.comment = value;
            else if (key == "Icon") entry.icon = value;
            else if (key == "Categories") entry.categories = value;
            else if (key == "NoDisplay") entry.noDisplay = (value == "true");
            else if (key == "Hidden") entry.noDisplay = (value == "true");
            else if (key == "OnlyShowIn") {
                // Only show if it matches our desktop
                if (value.find("HAVEL") == std::string::npos && 
                    value.find("GNOME") == std::string::npos &&
                    value.find("KDE") == std::string::npos &&
                    value.find("XFCE") == std::string::npos) {
                    entry.noDisplay = true;
                }
            }
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
            
            // Enable text input for IME support
            // Get TextInputManager from Server via API
            // Text input is handled via KeyEvent.utf8 from the keyboard handler
            // This provides full UTF-8 support for international characters
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
    
    void handleCharInput(const char* utf8) {
        if (!utf8 || utf8[0] == '\0') return;

        // Append UTF-8 character to search text
        m_searchText += utf8;
        m_selectedIndex = 0;
        filterResults();
        printf("[AppLauncher] Search: %s (%zu results)\n",
               m_searchText.c_str(), m_filtered.size());
    }

    void handleBackspace() {
        if (m_searchText.empty()) return;

        // Properly remove last UTF-8 character (not just last byte)
        size_t len = m_searchText.length();
        size_t charStart = len - 1;
        
        // Find start of last UTF-8 character
        // UTF-8 continuation bytes are 10xxxxxx (0x80-0xBF)
        while (charStart > 0 && (m_searchText[charStart] & 0xC0) == 0x80) {
            charStart--;
        }
        
        m_searchText.erase(charStart);
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

        // Spawn the application via C bridge
        // The command is executed through the shell
        havel_cpp_server_spawn(
            static_cast<struct havel_cpp_server*>(m_api->getNativeHandle()),
            entry.exec.c_str());

        hideLauncher();
    }
    
    std::string toLower(const std::string& str) {
        std::string result = str;
        std::transform(result.begin(), result.end(), result.begin(), ::tolower);
        return result;
    }

    void renderOverlay(void* rendererPtr) override {
        if (!m_visible || !rendererPtr) return;

        // Throttle to 2 Hz while open: content is the result list plus a
        // blinking caret - both fine at 2 Hz. Per-frame redraw re-damages
        // at frame rate and spins the render loop (the trap the FPS
        // overlay, decorations and the info bar all had - observed live
        // again here: the loop pegged CPU inside this function's
        // drawRect calls the moment the launcher opened).
        auto now = std::chrono::steady_clock::now();
        if (m_lastDraw + std::chrono::milliseconds(500) > now) return;
        m_lastDraw = now;

        OverlayRenderer* renderer = static_cast<OverlayRenderer*>(rendererPtr);
        
        int screenWidth = renderer->getScreenWidth();
        int screenHeight = renderer->getScreenHeight();
        
        // Launcher dimensions
        int launcherWidth = 600;
        int launcherHeight = 400;
        int launcherX = (screenWidth - launcherWidth) / 2;
        int launcherY = (screenHeight - launcherHeight) / 2;
        
        // Draw semi-transparent background
        renderer->drawRect(0, 0, screenWidth, screenHeight, Color(0.0f, 0.0f, 0.0f, 0.7f));
        
        // Draw launcher background
        renderer->drawRect((float)launcherX, (float)launcherY, (float)launcherWidth, (float)launcherHeight, Color(0.15f, 0.15f, 0.2f, 0.95f));
        
        // Draw launcher border
        renderer->drawBorder(FloatRect((float)launcherX, (float)launcherY, (float)launcherWidth, (float)launcherHeight), Color(0.5f, 0.5f, 0.6f, 0.8f), 2.0f);
        
        // Draw search box
        int searchHeight = 50;
        renderer->drawRect((float)(launcherX + 10), (float)(launcherY + 10), (float)(launcherWidth - 20), (float)searchHeight, Color(0.0f, 0.0f, 0.0f, 0.5f));
        renderer->drawBorder(FloatRect((float)(launcherX + 10), (float)(launcherY + 10), (float)(launcherWidth - 20), (float)searchHeight), Color(0.6f, 0.6f, 0.7f, 0.6f), 1.0f);
        
        // Draw search text
        if (m_searchText.empty()) {
            renderer->drawText("Type to search...", (float)(launcherX + 20), (float)(launcherY + 35), 18.0f, Color(0.5f, 0.5f, 0.5f, 0.7f));
        } else {
            renderer->drawText(m_searchText.c_str(), (float)(launcherX + 20), (float)(launcherY + 35), 18.0f, Color(1.0f, 1.0f, 1.0f, 1.0f));
        }
        
        // Draw results list
        int resultY = launcherY + searchHeight + 20;
        int resultHeight = launcherHeight - searchHeight - 40;
        int itemHeight = 40;
        int maxVisibleItems = resultHeight / itemHeight;
        
        // Draw visible results
        int startIndex = 0;
        int endIndex = m_filtered.size();
        if ((int)m_filtered.size() > maxVisibleItems) {
            endIndex = startIndex + maxVisibleItems;
        }
        
        for (int i = startIndex; i < (int)endIndex && i < (int)m_filtered.size(); i++) {
            const DesktopEntry& entry = m_filtered[i];
            bool isSelected = (i == m_selectedIndex);
            
            int itemY = resultY + (i - startIndex) * itemHeight;
            
            // Draw item background
            Color itemBg = isSelected ? Color(0.3f, 0.4f, 0.5f, 0.8f) : Color(0.0f, 0.0f, 0.0f, 0.3f);
            renderer->drawRect((float)(launcherX + 10), (float)itemY, (float)(launcherWidth - 20), (float)(itemHeight - 2), itemBg);
            
            // Draw app name
            renderer->drawText(entry.name.c_str(), (float)(launcherX + 20), (float)(itemY + 25), 16.0f, isSelected ? Color(1.0f, 1.0f, 1.0f, 1.0f) : Color(0.8f, 0.8f, 0.8f, 1.0f));
        }
        
        // Draw instruction text
        const char* instruction = "Type: Search | Arrows: Navigate | Enter: Launch | Esc: Cancel";
        float textWidth = strlen(instruction) * 8.0f;
        float textX = (screenWidth - textWidth) / 2.0f;
        renderer->drawText(instruction, textX, (float)(launcherY + launcherHeight + 15), 12.0f, Color(0.6f, 0.6f, 0.6f, 1.0f));
    }
};

// Plugin factory
Plugin* create_app_launcher_plugin() {
    return new AppLauncherPlugin();
}

} // namespace havel
