// App Icon Loader - Load icons from system theme or .desktop files

#include <wm/render/AppIconLoader.hpp>
#include <Logger.h>
#include <cstring>
#include <algorithm>

namespace havel {

AppIconLoader* AppIconLoader::s_instance = nullptr;

AppIconLoader::AppIconLoader() {
    // Initialize icon theme search paths
    m_iconPaths = {
        "/usr/share/icons/",
        "/usr/share/pixmaps/",
        "/usr/local/share/icons/",
        "/usr/local/share/pixmaps/",
    };
    
    // Add user icon directory if exists
    const char* home = getenv("HOME");
    if (home) {
        char userIconPath[512];
        snprintf(userIconPath, sizeof(userIconPath), "%s/.icons/", home);
        m_iconPaths.push_back(userIconPath);
        
        snprintf(userIconPath, sizeof(userIconPath), "%s/.local/share/icons/", home);
        m_iconPaths.push_back(userIconPath);
    }
    
    s_instance = this;
    LOG_INFO("[AppIconLoader] Initialized with %zu icon paths", m_iconPaths.size());
}

AppIconLoader::~AppIconLoader() {
    // Clean up cached textures
    for (auto& pair : m_iconCache) {
        if (pair.second.textureId != 0) {
            glDeleteTextures(1, &pair.second.textureId);
        }
    }
    m_iconCache.clear();
    s_instance = nullptr;
}

AppIconLoader* AppIconLoader::getInstance() {
    return s_instance;
}

GLuint AppIconLoader::loadIcon(const std::string& appId) {
    if (appId.empty()) return 0;
    
    // Check cache first
    auto it = m_iconCache.find(appId);
    if (it != m_iconCache.end()) {
        return it->second.textureId;
    }
    
    // Try to load icon
    GLuint textureId = loadIconFromFile(appId);
    
    // Cache result (even if failed - use 0)
    IconCacheEntry entry;
    entry.textureId = textureId;
    entry.size = 32;
    m_iconCache[appId] = entry;
    
    return textureId;
}

GLuint AppIconLoader::loadIconFromFile(const std::string& appId) {
    // Try common icon names
    std::vector<std::string> iconNames = {
        appId,
        appId + ".png",
        appId + ".svg",
        appId + ".xpm",
    };
    
    // Search through icon paths
    for (const auto& iconPath : m_iconPaths) {
        for (const auto& iconName : iconNames) {
            std::string fullPath = iconPath + iconName;
            
            // Try exact path first
            GLuint texture = loadIconFromPath(fullPath);
            if (texture != 0) {
                LOG_DEBUG("[AppIconLoader] Loaded icon: %s", fullPath.c_str());
                return texture;
            }
            
            // Try with common sizes
            std::vector<std::string> sizes = {"32x32", "48x48", "64x64", "128x128", "scalable"};
            for (const auto& size : sizes) {
                fullPath = iconPath + size + "/apps/" + iconName;
                texture = loadIconFromPath(fullPath);
                if (texture != 0) {
                    LOG_DEBUG("[AppIconLoader] Loaded icon: %s", fullPath.c_str());
                    return texture;
                }
                
                fullPath = iconPath + size + "/apps/" + appId + ".png";
                texture = loadIconFromPath(fullPath);
                if (texture != 0) {
                    LOG_DEBUG("[AppIconLoader] Loaded icon: %s", fullPath.c_str());
                    return texture;
                }
            }
        }
    }
    
    // Fallback: generate colored placeholder
    LOG_DEBUG("[AppIconLoader] Using placeholder for: %s", appId.c_str());
    return generatePlaceholderIcon(appId);
}

GLuint AppIconLoader::loadIconFromPath(const std::string& path) {
    // For now, we'll generate a placeholder
    // A full implementation would use stb_image.h or similar to load PNG/SVG
    // This is a stub that checks if file exists and returns a placeholder
    
    FILE* f = fopen(path.c_str(), "r");
    if (f) {
        fclose(f);
        // File exists - in real implementation, load the image
        // For now, just return a placeholder with the path hash
        return generatePlaceholderIcon(path);
    }
    
    return 0;
}

GLuint AppIconLoader::generatePlaceholderIcon(const std::string& name) {
    GLuint texture;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    
    // Generate color from name hash
    unsigned int hash = 0;
    for (char c : name) {
        hash = hash * 31 + c;
    }
    float r = ((hash >> 16) & 0xFF) / 255.0f;
    float g = ((hash >> 8) & 0xFF) / 255.0f;
    float b = (hash & 0xFF) / 255.0f;
    
    // Create 32x32 solid color texture
    unsigned char pixels[32 * 32 * 4];
    for (int i = 0; i < 32 * 32; i++) {
        pixels[i * 4 + 0] = (unsigned char)(r * 255);
        pixels[i * 4 + 1] = (unsigned char)(g * 255);
        pixels[i * 4 + 2] = (unsigned char)(b * 255);
        pixels[i * 4 + 3] = 255;  // Alpha
    }
    
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 32, 32, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glBindTexture(GL_TEXTURE_2D, 0);
    
    return texture;
}

} // namespace havel
