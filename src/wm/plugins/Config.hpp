#pragma once

#include <string>
#include <unordered_map>
#include <vector>
#include <fstream>
#include <sstream>

namespace havel {

/**
 * Simple JSON-like config parser
 * Minimal implementation for plugin configuration
 */
class Config {
public:
    Config() = default;
    
    bool loadFromFile(const std::string& path) {
        std::ifstream file(path);
        if (!file.is_open()) {
            return false;
        }
        
        std::stringstream buffer;
        buffer << file.rdbuf();
        parse(buffer.str());
        return true;
    }
    
    bool getBool(const std::string& key, bool defaultValue) const {
        auto it = values.find(key);
        if (it == values.end()) return defaultValue;
        
        const std::string& val = it->second;
        if (val == "true") return true;
        if (val == "false") return false;
        return defaultValue;
    }
    
    int getInt(const std::string& key, int defaultValue) const {
        auto it = values.find(key);
        if (it == values.end()) return defaultValue;
        
        try {
            return std::stoi(it->second);
        } catch (...) {
            return defaultValue;
        }
    }
    
    float getFloat(const std::string& key, float defaultValue) const {
        auto it = values.find(key);
        if (it == values.end()) return defaultValue;
        
        try {
            return std::stof(it->second);
        } catch (...) {
            return defaultValue;
        }
    }
    
    std::string getString(const std::string& key, const std::string& defaultValue) const {
        auto it = values.find(key);
        if (it == values.end()) return defaultValue;
        
        std::string val = it->second;
        // Remove quotes
        if (val.front() == '"' && val.back() == '"') {
            val = val.substr(1, val.size() - 2);
        }
        return val;
    }
    
    bool hasKey(const std::string& key) const {
        return values.find(key) != values.end();
    }

private:
    std::unordered_map<std::string, std::string> values;
    
    void parse(const std::string& json) {
        // Simple parser - handles basic JSON structure
        // Not a full JSON parser, but sufficient for config files
        
        std::string currentPath;
        bool inString = false;
        std::string currentKey;
        std::string currentValue;
        
        for (size_t i = 0; i < json.size(); ++i) {
            char c = json[i];
            
            if (c == '"') {
                inString = !inString;
                if (!inString && !currentKey.empty()) {
                    // End of key or value
                    if (currentValue.empty()) {
                        currentKey = currentKey;
                    } else {
                        values[currentPath + "." + currentKey] = currentValue;
                        currentKey.clear();
                        currentValue.clear();
                    }
                }
                continue;
            }
            
            if (inString) {
                currentValue += c;
                continue;
            }
            
            if (c == ':') {
                // Key finished
                continue;
            }
            
            if (c == ',' || c == '}') {
                // Value finished
                if (!currentKey.empty() && !currentValue.empty()) {
                    values[currentPath + "." + currentKey] = currentValue;
                    currentKey.clear();
                    currentValue.clear();
                }
                if (c == '}' && !currentPath.empty()) {
                    // Pop path
                    size_t lastDot = currentPath.rfind('.');
                    if (lastDot != std::string::npos) {
                        currentPath = currentPath.substr(0, lastDot);
                    } else {
                        currentPath.clear();
                    }
                }
                continue;
            }
            
            if (c == '{') {
                // New section
                if (!currentKey.empty()) {
                    if (!currentPath.empty()) {
                        currentPath += "." + currentKey;
                    } else {
                        currentPath = currentKey;
                    }
                    currentKey.clear();
                }
                continue;
            }
            
            if (!inString && c != ' ' && c != '\n' && c != '\r' && c != '\t') {
                if (currentKey.empty() && currentValue.empty()) {
                    // Start of key
                    currentValue += c;
                } else {
                    // Part of value
                    currentValue += c;
                }
            }
            
            if (c == ':' && !currentValue.empty()) {
                currentKey = currentValue;
                currentValue.clear();
            }
        }
    }
};

} // namespace havel
