// Plugin Settings Implementation

#include "Plugin.hpp"
#include <Logger.h>

namespace havel {

// ============================================================================
// PluginSettings Implementation
// ============================================================================

bool PluginSettings::getBool(const std::string& key, bool defaultValue) const {
    auto it = m_values.find(key);
    if (it != m_values.end()) {
        try {
            return std::any_cast<bool>(it->second);
        } catch (...) {
            // Try to parse from string
            try {
                std::string str = std::any_cast<std::string>(it->second);
                return (str == "true" || str == "1" || str == "yes");
            } catch (...) {
                return defaultValue;
            }
        }
    }
    return defaultValue;
}

int PluginSettings::getInt(const std::string& key, int defaultValue) const {
    auto it = m_values.find(key);
    if (it != m_values.end()) {
        try {
            return std::any_cast<int>(it->second);
        } catch (...) {
            try {
                std::string str = std::any_cast<std::string>(it->second);
                return std::stoi(str);
            } catch (...) {
                return defaultValue;
            }
        }
    }
    return defaultValue;
}

float PluginSettings::getFloat(const std::string& key, float defaultValue) const {
    auto it = m_values.find(key);
    if (it != m_values.end()) {
        try {
            return std::any_cast<float>(it->second);
        } catch (...) {
            try {
                std::string str = std::any_cast<std::string>(it->second);
                return std::stof(str);
            } catch (...) {
                return defaultValue;
            }
        }
    }
    return defaultValue;
}

std::string PluginSettings::getString(const std::string& key, const std::string& defaultValue) const {
    auto it = m_values.find(key);
    if (it != m_values.end()) {
        try {
            return std::any_cast<std::string>(it->second);
        } catch (...) {
            try {
                int val = std::any_cast<int>(it->second);
                return std::to_string(val);
            } catch (...) {
                try {
                    float val = std::any_cast<float>(it->second);
                    return std::to_string(val);
                } catch (...) {
                    return defaultValue;
                }
            }
        }
    }
    return defaultValue;
}

void PluginSettings::setBool(const std::string& key, bool value) {
    m_values[key] = value;
}

void PluginSettings::setInt(const std::string& key, int value) {
    m_values[key] = value;
}

void PluginSettings::setFloat(const std::string& key, float value) {
    m_values[key] = value;
}

void PluginSettings::setString(const std::string& key, const std::string& value) {
    m_values[key] = value;
}

bool PluginSettings::hasKey(const std::string& key) const {
    return m_values.find(key) != m_values.end();
}

std::vector<std::string> PluginSettings::getKeys() const {
    std::vector<std::string> keys;
    keys.reserve(m_values.size());
    for (const auto& [key, _] : m_values) {
        keys.push_back(key);
    }
    return keys;
}

void PluginSettings::loadFromMap(const std::unordered_map<std::string, std::string>& data) {
    for (const auto& [key, value] : data) {
        // Try to auto-detect type
        if (value == "true" || value == "1" || value == "yes") {
            m_values[key] = true;
        } else if (value == "false" || value == "0" || value == "no") {
            m_values[key] = false;
        } else {
            // Try integer
            try {
                m_values[key] = std::stoi(value);
                continue;
            } catch (...) {}
            
            // Try float
            try {
                m_values[key] = std::stof(value);
                continue;
            } catch (...) {}
            
            // Fall back to string
            m_values[key] = value;
        }
    }
}

std::unordered_map<std::string, std::string> PluginSettings::exportToMap() const {
    std::unordered_map<std::string, std::string> result;
    for (const auto& [key, value] : m_values) {
        try {
            if (value.type() == typeid(bool)) {
                result[key] = std::any_cast<bool>(value) ? "true" : "false";
            } else if (value.type() == typeid(int)) {
                result[key] = std::to_string(std::any_cast<int>(value));
            } else if (value.type() == typeid(float)) {
                result[key] = std::to_string(std::any_cast<float>(value));
            } else if (value.type() == typeid(std::string)) {
                result[key] = std::any_cast<std::string>(value);
            }
        } catch (...) {
            // Skip values that can't be serialized
        }
    }
    return result;
}

// ============================================================================
// PluginLogger Implementation
// ============================================================================

PluginLogger::PluginLogger(const std::string& pluginName)
    : m_pluginName(pluginName) {}

void PluginLogger::log(LogLevel level, const std::string& message) {
    if (level < m_minLevel) return;
    
    const char* levelStr = "";
    switch (level) {
        case LogLevel::Debug: levelStr = "DEBUG"; break;
        case LogLevel::Info: levelStr = "INFO"; break;
        case LogLevel::Warning: levelStr = "WARN"; break;
        case LogLevel::Error: levelStr = "ERROR"; break;
    }
    
    printf("[%s] [%s] %s\n", m_pluginName.c_str(), levelStr, message.c_str());
}

void PluginLogger::debug(const std::string& message) {
    log(LogLevel::Debug, message);
}

void PluginLogger::info(const std::string& message) {
    log(LogLevel::Info, message);
}

void PluginLogger::warn(const std::string& message) {
    log(LogLevel::Warning, message);
}

void PluginLogger::error(const std::string& message) {
    log(LogLevel::Error, message);
}

void PluginLogger::setMinLevel(LogLevel level) {
    m_minLevel = level;
}

// ============================================================================
// Plugin Event Emission
// ============================================================================

void Plugin::emitEvent(PluginEvent& event) {
    event.source = getName();
    // Event dispatch handled by PluginManager
    // This is a placeholder - actual implementation in PluginManager
}

} // namespace havel
