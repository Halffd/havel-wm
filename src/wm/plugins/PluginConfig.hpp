#pragma once

#include <string>
#include <unordered_map>
#include <vector>
#include <fstream>
#include <sstream>
#include <cstdio>
#include <cstdlib>
#include <cctype>
#include <xkbcommon/xkbcommon.h>
#include <xkbcommon/xkbcommon-keysyms.h>

namespace havel {

/**
 * Plugin Configuration - JSON-based plugin settings
 * 
 * Format:
 * {
 *   "plugin_name": {
 *     "enabled": true,
 *     "keybinding": "Meta+Space",
 *     "setting": "value"
 *   }
 * }
 */
class PluginConfig {
public:
    static PluginConfig& getInstance() {
        static PluginConfig instance;
        return instance;
    }

    bool load(const std::string& path) {
        // Try paths in order
        std::vector<std::string> tryPaths = {
            path,
            std::string(getenv("HOME") ? getenv("HOME") : "~") + "/.config/havel-wm/plugins.json",
            "/etc/havel-wm/plugins.json",
            "plugins.json"
        };
        
        for (const auto& tryPath : tryPaths) {
            std::ifstream file(tryPath);
            if (file.is_open()) {
                std::stringstream buffer;
                buffer << file.rdbuf();
                file.close();
                
                printf("[PluginConfig] Loaded: %s\n", tryPath.c_str());
                return parse(buffer.str());
            }
        }
        
        printf("[PluginConfig] No config file found, using defaults\n");
        return false;
    }

    bool isEnabled(const std::string& pluginName) const {
        auto it = m_plugins.find(pluginName);
        if (it != m_plugins.end()) {
            auto enabledIt = it->second.find("enabled");
            if (enabledIt != it->second.end()) {
                return enabledIt->second == "true" || enabledIt->second == "1";
            }
        }
        return true;  // Enabled by default
    }

    std::string getValue(const std::string& pluginName, const std::string& key, const std::string& defaultValue = "") const {
        auto pluginIt = m_plugins.find(pluginName);
        if (pluginIt != m_plugins.end()) {
            auto valueIt = pluginIt->second.find(key);
            if (valueIt != pluginIt->second.end()) {
                return valueIt->second;
            }
        }
        return defaultValue;
    }

    int getIntValue(const std::string& pluginName, const std::string& key, int defaultValue = 0) const {
        std::string value = getValue(pluginName, key, "");
        if (value.empty()) return defaultValue;
        try {
            return std::stoi(value);
        } catch (...) {
            return defaultValue;
        }
    }

    float getFloatValue(const std::string& pluginName, const std::string& key, float defaultValue = 0.0f) const {
        std::string value = getValue(pluginName, key, "");
        if (value.empty()) return defaultValue;
        try {
            return std::stof(value);
        } catch (...) {
            return defaultValue;
        }
    }

    std::string getKeybinding(const std::string& pluginName) const {
        return getValue(pluginName, "keybinding", "");
    }

    // Parsed keybinding: modifier mask + XKB keysym.
    // Modifier mask bits (xkbcommon convention, matches the bits the
    // compositor reports): Shift=0x1, CapsLock=0x2, Ctrl=0x4, Alt=0x8,
    // Mod2=0x10, Mod3=0x20, Meta/Logo=0x40, Mod5=0x80.
    // Accepts "Meta"/"Super"/"Logo" for the logo key, "Control"/"Ctrl",
    // "Alt", "Shift", and any single keysym name via xkbcommon
    // (xkb_keysym_from_name, case-insensitive): "Meta+Shift+W",
    // "Alt+Tab", "Meta+D", "Meta+Return".
    // Returns false if any token is unrecognized.
    bool getKeybindingParsed(const std::string& pluginName,
                            uint32_t* modifiers, uint32_t* keysym) const {
        std::string spec = getKeybinding(pluginName);
        if (spec.empty()) return false;
        return parseKeybindingSpec(spec, modifiers, keysym);
    }

    // Parse an arbitrary "Mods+Key" spec (no config lookup).
    static bool parseKeybindingSpec(const std::string& spec,
                                    uint32_t* modifiers, uint32_t* keysym);

    bool isEmpty() const {
        return m_plugins.empty();
    }

    PluginConfig() = default;

private:
    bool parse(const std::string& content) {
        m_plugins.clear();

        std::string currentPlugin;
        bool inPlugin = false;

        std::istringstream stream(content);
        std::string line;

        while (std::getline(stream, line)) {
            // Trim whitespace
            size_t start = line.find_first_not_of(" \t\r\n");
            if (start == std::string::npos) continue;
            size_t end = line.find_last_not_of(" \t\r\n");
            line = line.substr(start, end - start + 1);

            // Skip comments
            if (line[0] == '/' || line[0] == '#') continue;

            // Remove trailing comma
            if (line.back() == ',') line.pop_back();

            // Plugin section start: "name": {
            if (line.find("\"") == 0 && line.find("{") != std::string::npos) {
                size_t nameStart = line.find('"', 1);
                size_t nameEnd = line.find('"', nameStart + 1);
                if (nameStart != std::string::npos && nameEnd != std::string::npos) {
                    currentPlugin = line.substr(nameStart + 1, nameEnd - nameStart - 1);
                    inPlugin = true;
                }
                continue;
            }

            // Plugin section end
            if (line == "}") {
                inPlugin = false;
                currentPlugin.clear();
                continue;
            }

            // Key-value pair: "key": "value"
            if (inPlugin && line.find(':') != std::string::npos) {
                size_t colonPos = line.find(':');
                std::string key = line.substr(0, colonPos);
                std::string value = line.substr(colonPos + 1);

                // Trim key
                size_t keyStart = key.find_first_not_of(" \t\"");
                size_t keyEnd = key.find_last_not_of(" \t\"");
                if (keyStart != std::string::npos && keyEnd != std::string::npos) {
                    key = key.substr(keyStart, keyEnd - keyStart + 1);
                }

                // Trim value
                size_t valueStart = value.find_first_not_of(" \t\"");
                size_t valueEnd = value.find_last_not_of(" \t\"");
                if (valueStart != std::string::npos && valueEnd != std::string::npos) {
                    value = value.substr(valueStart, valueEnd - valueStart + 1);
                }

                if (!key.empty()) {
                    m_plugins[currentPlugin][key] = value;
                }
            }
        }

        return !m_plugins.empty();
    }

    // plugin_name -> (key -> value)
    std::unordered_map<std::string, std::unordered_map<std::string, std::string>> m_plugins;
};

// Parsed keybinding spec, shared by plugins that honor config bindings.
inline bool PluginConfig::parseKeybindingSpec(const std::string& spec,
                                              uint32_t* modifiers, uint32_t* keysym) {
    if (spec.empty() || !modifiers || !keysym) return false;

    *modifiers = 0;
    *keysym = 0;

    // Tokenize on '+'
    std::string token;
    std::string key;
    size_t pos = 0;
    auto next = [&](std::string* out) -> bool {
        if (pos >= spec.size()) return false;
        size_t plus = spec.find('+', pos);
        if (plus == std::string::npos) {
            *out = spec.substr(pos);
            pos = spec.size();
        } else {
            *out = spec.substr(pos, plus - pos);
            pos = plus + 1;
        }
        return !out->empty();
    };

    while (next(&token)) {
        // Lowercase compare for modifiers
        std::string lower = token;
        for (char& c : lower) c = (char)tolower((unsigned char)c);

        if (lower == "shift") { *modifiers |= 1u << 0; }
        else if (lower == "capslock" || lower == "lock") { *modifiers |= 1u << 1; }
        else if (lower == "ctrl" || lower == "control") { *modifiers |= 1u << 2; }
        else if (lower == "alt" || lower == "mod1") { *modifiers |= 1u << 3; }
        else if (lower == "meta" || lower == "super" || lower == "logo" || lower == "win") { *modifiers |= 1u << 6; }
        else {
            // Final token: the keysym
            if (!key.empty()) return false;  // two non-modifier tokens
            key = token;
        }
    }

    if (key.empty()) return false;

    // Keysym lookup, case-insensitive so "w" and "W", "return", "tab",
    // "escape", "page_up" etc. all resolve. XKB_KEY_NoSymbol (0) = fail.
    xkb_keysym_t sym = xkb_keysym_from_name(key.c_str(), XKB_KEYSYM_CASE_INSENSITIVE);
    if (sym == XKB_KEY_NoSymbol) return false;

    *keysym = (uint32_t)sym;
    return true;
}

} // namespace havel
