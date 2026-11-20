// App Launcher Plugin - Phase 4.3
// Desktop application launcher with fuzzy search and IME support

#include <wm/plugins/Plugin.hpp>
#include <wm/plugins/CompositorAPI.hpp>
#include <wm/plugins/PluginConfig.hpp>
#include <wm/render/OverlayRenderer.hpp>
#include <wm/render/AppIconLoader.hpp>
#include <wm/bridge.h>
#include <input/TextInputManager.hpp>
#include <xkbcommon/xkbcommon-keysyms.h>
#include <cstdio>
#include <chrono>
#include <set>
#include <unordered_map>
#include <functional>
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
    bool terminal = false;   // Terminal=true: launch inside a terminal
    // Resolved icon pixels (AppIconLoader handle; 0 = none). Filled once
    // when the entry first draws - icon lookup is disk I/O, so it must
    // not run per frame.
    uint32_t iconHandle = 0;
    int iconW = 0, iconH = 0;
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
    wlr_scene_tree_t* m_tree = nullptr;     // persistent launcher tree
    OverlayRenderer* m_renderer = nullptr;  // cached for hide-time tree teardown
    std::vector<DesktopEntry> m_filtered;
    int m_hoverIndex = -1;   // result row under the pointer
    int m_scrollStart = 0;   // first visible row in the result list
    bool m_treeDirty = false; // hover/scroll change: rebuild tree now
    
    void scanDesktopFiles() {
        // XDG base dir spec: $XDG_DATA_HOME/applications (default
        // ~/.local/share) first, then every $XDG_DATA_DIRS entry (default
        // /usr/local/share:/usr/share).
        std::vector<std::string> dirs;
        const char* dataHome = getenv("XDG_DATA_HOME");
        const char* home = getenv("HOME");
        if (dataHome && dataHome[0]) {
            dirs.push_back(std::string(dataHome) + "/applications");
        } else if (home) {
            dirs.push_back(std::string(home) + "/.local/share/applications");
        }
        const char* dataDirs = getenv("XDG_DATA_DIRS");
        std::string sysDirs = dataDirs && dataDirs[0] ? dataDirs : "/usr/local/share:/usr/share";
        size_t start = 0;
        while (start <= sysDirs.size()) {
            size_t colon = sysDirs.find(':', start);
            std::string one = colon == std::string::npos
                                  ? sysDirs.substr(start)
                                  : sysDirs.substr(start, colon - start);
            if (!one.empty()) dirs.push_back(one + "/applications");
            if (colon == std::string::npos) break;
            start = colon + 1;
        }

        for (const auto& dir : dirs) {
            scanDirectory(dir.c_str(), 0);
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

        printf("[AppLauncher] Scanned %zu applications from %zu dirs\n",
               m_entries.size(), dirs.size());
    }

    void scanDirectory(const char* path, int depth) {
        DIR* dir = opendir(path);
        if (!dir) return;

        struct dirent* entry;
        while ((entry = readdir(dir)) != nullptr) {
            std::string filename = entry->d_name;
            if (filename == "." || filename == "..") continue;

            // Subdirectories (e.g. applications/kde/) - one nesting level
            if (depth < 2) {
                std::string sub = std::string(path) + "/" + filename;
                DIR* probe = opendir(sub.c_str());
                if (probe) {
                    closedir(probe);
                    scanDirectory(sub.c_str(), depth + 1);
                    continue;
                }
            }

            // Only process .desktop files
            if (filename.length() > 8 &&
                filename.substr(filename.length() - 8) == ".desktop") {

                std::string fullPath = std::string(path) + "/" + filename;
                DesktopEntry de;
                de.id = fullPath;

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
            else if (key == "Terminal") entry.terminal = (value == "true");
            else if (key == "NoDisplay") entry.noDisplay = (value == "true");
            else if (key == "Hidden") entry.noDisplay = (value == "true");
        }

        file.close();
    }
    
    void toggleLauncher() {
        m_visible = !m_visible;
        if (m_visible) {
            m_searchText.clear();
            m_selectedIndex = 0;
            m_hoverIndex = -1;
            m_scrollStart = 0;
            filterResults();
            printf("[AppLauncher] Shown (%zu entries, %zu visible)\n",
                   m_entries.size(), m_filtered.size());

            // Enable text input for IME support
            // Get TextInputManager from Server via API
            // Text input is handled via KeyEvent.utf8 from the keyboard handler
            // This provides full UTF-8 support for international characters
        } else {
            dropTree();
            printf("[AppLauncher] Hidden\n");
        }
    }

    void hideLauncher() {
        m_visible = false;
        m_searchText.clear();
        m_hoverIndex = -1;
        m_scrollStart = 0;
        dropTree();
        printf("[AppLauncher] Hidden\n");
    }

    // Tear down the persistent overlay tree - callers that hide the
    // launcher must do this or the last drawn launcher stays frozen on
    // screen (nothing redraws while hidden).
    void dropTree() {
        if (m_renderer && m_tree) {
            m_renderer->destroyPersistentTree(&m_tree);
        }
    }
    
    void navigate(int delta) {
        if (m_filtered.empty()) return;

        m_selectedIndex += delta;
        if (m_selectedIndex < 0) m_selectedIndex = 0;
        if (m_selectedIndex >= (int)m_filtered.size()) {
            m_selectedIndex = (int)m_filtered.size() - 1;
        }
        m_treeDirty = true;  // selection moved: rebuild the tree now

        printf("[AppLauncher] Selected: %s\n", m_filtered[m_selectedIndex].name.c_str());
    }
    
    void handleCharInput(const char* utf8) {
        if (!utf8 || utf8[0] == '\0') return;

        // Append UTF-8 character to search text
        m_searchText += utf8;
        m_selectedIndex = 0;
        m_scrollStart = 0;
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
        m_scrollStart = 0;
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

        // Terminal apps need a terminal or their output is nowhere and
        // TUI apps have nowhere to run. Wrap in the configured emulator
        // (spawn is argv-based, so no shell quoting is involved).
        std::string cmd = entry.exec;
        if (entry.terminal) {
            auto& config = PluginConfig::getInstance();
            std::string term = config.getValue("app_launcher", "terminal", "foot");
            cmd = term + " -e " + entry.exec;
        }

        // Spawn the application via C bridge
        havel_cpp_server_spawn(
            static_cast<struct havel_cpp_server*>(m_api->getNativeHandle()),
            cmd.c_str());

        hideLauncher();
    }

    // Command surface: the panel's launcher button, IPC and scripts.
    bool onCommand(const std::string& cmd, const std::string& arg) override {
        (void)arg;
        if (cmd == "open" || cmd == "show") {
            if (!m_visible) toggleLauncher();
            return true;
        }
        if (cmd == "close" || cmd == "hide") {
            if (m_visible) hideLauncher();
            return true;
        }
        if (cmd == "toggle") {
            toggleLauncher();
            return true;
        }
        return false;
    }

    void onMouseMotion(int x, int y) override {
        if (!m_visible) return;
        int idx = itemAt(x, y);
        if (idx != m_hoverIndex) {
            m_hoverIndex = idx;
            m_treeDirty = true;  // hover highlight redraws soon
        }
    }

    bool onMouseButton(uint32_t button, bool pressed, int x, int y) override {
        if (!m_visible || !pressed) return false;
        if (button == 0x110) {  // left press: launch the clicked entry
            int idx = itemAt(x, y);
            if (idx >= 0) {
                m_selectedIndex = idx;
                launchSelected();
                return true;
            }
            return false;
        }
        if (button == 4) { navigate(-1); return true; }   // wheel up
        if (button == 5) { navigate(1); return true; }    // wheel down
        return false;
    }

    // Launcher geometry is computed ONCE per draw and stored here; the
    // hit-testers below read the same box, so clicks always land on the
    // pixels the last pass drew. (The old itemAt re-derived the box from
    // renderer screen size - a different output's pass than the one that
    // drew the launcher made hover/clicks land on the wrong rows or
    // nowhere.)
    Rect m_lastDrawBox{0, 0, 0, 0};

    // Icon pixel cache: entry.id -> decoded ARGB pixels (resolved once
    // per entry; m_iconResolved guards repeated disk lookups for entries
    // with no icon on disk).
    std::set<std::string> m_iconResolved;
    std::unordered_map<std::string, std::vector<uint32_t>> m_iconPixels;
    std::unordered_map<std::string, int> m_iconSize;

    // Row under the cursor in the result list, -1 when outside it.
    // Coordinates are the same screen space renderOverlay drew in.
    int itemAt(int x, int y) {
        int launcherWidth = m_lastDrawBox.w;
        int launcherHeight = m_lastDrawBox.h;
        int launcherX = m_lastDrawBox.x;
        int launcherY = m_lastDrawBox.y;
        int resultY = launcherY + 50 + 20;
        int listX0 = launcherX + 10, listX1 = launcherX + launcherWidth - 10;
        if (x < listX0 || x >= listX1) return -1;
        if (y < resultY) return -1;
        int itemHeight = 40;
        int idx = (y - resultY) / itemHeight + m_scrollStart;
        if (idx < 0 || idx >= (int)m_filtered.size()) return -1;
        int maxVisible = (launcherHeight - 50 - 40) / itemHeight;
        if (idx >= m_scrollStart + maxVisible) return -1;
        return idx;
    }
    
    std::string toLower(const std::string& str) {
        std::string result = str;
        std::transform(result.begin(), result.end(), result.begin(), ::tolower);
        return result;
    }

    void renderOverlay(void* rendererPtr) override {
        if (!m_visible || !rendererPtr) return;

        // Draw into a PERSISTENT tree rebuilt at 2 Hz while open (content
        // is the result list plus a blinking caret). The frame tree is
        // cleared every frame, so a throttled draw there would blink off
        // between draws; the persistent tree stays visible at 2 damage
        // frames a second instead of per-frame redraws that spun the
        // render loop at full CPU (observed live). Hover/scroll changes
        // set m_treeDirty for an immediate rebuild.
        auto now = std::chrono::steady_clock::now();
        bool dirty = m_treeDirty;
        m_treeDirty = false;
        if (m_tree && !dirty && m_lastDraw + std::chrono::milliseconds(500) > now) return;
        m_lastDraw = now;

        OverlayRenderer* renderer = static_cast<OverlayRenderer*>(rendererPtr);
        m_renderer = renderer;
        if (m_tree) renderer->destroyPersistentTree(&m_tree);
        m_tree = renderer->createPersistentTree();
        if (!m_tree) return;
        renderer->setTargetTree(m_tree);

        int screenWidth = renderer->getScreenWidth();
        int screenHeight = renderer->getScreenHeight();

        // Launcher dimensions - recorded for the hit-testers (itemAt)
        // so clicks land on the box this pass actually drew.
        int launcherWidth = 600;
        int launcherHeight = 400;
        int launcherX = (screenWidth - launcherWidth) / 2;
        int launcherY = (screenHeight - launcherHeight) / 2;
        m_lastDrawBox = Rect{launcherX, launcherY, launcherWidth, launcherHeight};

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

        // Scroll window follows the selection so keyboard navigation stays
        // visible past the first screenful of results.
        int startIndex = m_scrollStart;
        if (m_selectedIndex < startIndex) startIndex = m_selectedIndex;
        if (m_selectedIndex >= startIndex + maxVisibleItems)
            startIndex = m_selectedIndex - maxVisibleItems + 1;
        if (startIndex < 0) startIndex = 0;
        if (!m_filtered.empty() && startIndex > (int)m_filtered.size() - 1)
            startIndex = (int)m_filtered.size() - 1;
        m_scrollStart = startIndex;

        // Draw visible results
        int endIndex = (int)m_filtered.size();
        if (endIndex > startIndex + maxVisibleItems) {
            endIndex = startIndex + maxVisibleItems;
        }

        for (int i = startIndex; i < (int)endIndex && i < (int)m_filtered.size(); i++) {
            const DesktopEntry& entry = m_filtered[i];
            bool isSelected = (i == m_selectedIndex);
            bool isHovered = (i == m_hoverIndex);

            int itemY = resultY + (i - startIndex) * itemHeight;

            // Draw item background
            Color itemBg = isSelected ? Color(0.3f, 0.4f, 0.5f, 0.8f)
                                      : isHovered ? Color(0.22f, 0.28f, 0.35f, 0.7f)
                                                  : Color(0.0f, 0.0f, 0.0f, 0.3f);
            renderer->drawRect((float)(launcherX + 10), (float)itemY, (float)(launcherWidth - 20), (float)(itemHeight - 2), itemBg);

            // Draw app icon (24x24, vertically centered in the row).
            // Icon lookup is disk I/O: resolve once per entry, cache the
            // pixels in the plugin - the persistent tree keeps the drawn
            // buffer, and per-frame lookups would hammer the disk.
            const int iconSize = 24;
            int iconX = launcherX + 20;
            int iconY = itemY + (itemHeight - 2 - iconSize) / 2;
            if (m_iconPixels.find(entry.id) == m_iconPixels.end()) {
                m_iconResolved.insert(entry.id);
                auto* loader = havel::AppIconLoader::getInstance();
                std::string key = !entry.icon.empty() ? entry.icon : entry.exec;
                // basename of exec (strip args and path)
                auto sp = key.find(' ');
                if (sp != std::string::npos) key = key.substr(0, sp);
                auto slash = key.rfind('/');
                if (slash != std::string::npos) key = key.substr(slash + 1);
                uint32_t h = loader->loadIcon(!entry.icon.empty() ? entry.icon : key);
                if (h) {
                    const uint32_t* px = loader->getIconPixels(h);
                    int s = loader->getIconSize();
                    if (px && s > 0) {
                        m_iconPixels[entry.id] = std::vector<uint32_t>(px, px + (size_t)s * s);
                        m_iconSize[entry.id] = s;
                    }
                }
            }
            bool hasIcon = false;
            auto ipx = m_iconPixels.find(entry.id);
            if (ipx != m_iconPixels.end() && !ipx->second.empty()) {
                hasIcon = true;
                int s = m_iconSize[entry.id];
                renderer->drawPixels(ipx->second.data(), s, s,
                                     (float)iconX, (float)iconY,
                                     (float)iconSize, (float)iconSize);
            }

            // App name: vertically centered on the row. The old +25 pushed
            // 16px glyphs into the bottom of the 38px row - descenders
            // clipped and the text hugged the row bottom (user report).
            float textH = 16.0f;
            float ty = (float)itemY + ((itemHeight - 2) - textH) / 2.0f;
            int textX = launcherX + 20 + (hasIcon ? iconSize + 8 : 0);
            renderer->drawText(entry.name.c_str(), (float)textX, ty, textH, isSelected ? Color(1.0f, 1.0f, 1.0f, 1.0f) : Color(0.8f, 0.8f, 0.8f, 1.0f));
        }
        
        // Draw instruction text
        const char* instruction = "Type: Search | Arrows: Navigate | Enter: Launch | Esc: Cancel";
        float textWidth = strlen(instruction) * 8.0f;
        float textX = (screenWidth - textWidth) / 2.0f;
        renderer->drawText(instruction, textX, (float)(launcherY + launcherHeight + 15), 12.0f, Color(0.6f, 0.6f, 0.6f, 1.0f));

        renderer->setTargetTree(nullptr);
    }
};

// Plugin factory
Plugin* create_app_launcher_plugin() {
    return new AppLauncherPlugin();
}

} // namespace havel
