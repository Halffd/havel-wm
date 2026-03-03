#pragma once

#include <GLES2/gl2.h>
#include <string>
#include <unordered_map>
#include <vector>

namespace havel {

/**
 * App Icon Loader
 * 
 * Loads application icons for Alt-Tab and other overlays.
 * Searches system icon theme and caches loaded textures.
 */
class AppIconLoader {
public:
    AppIconLoader();
    ~AppIconLoader();
    
    static AppIconLoader* getInstance();
    
    // Load icon for an app (returns cached if available)
    GLuint loadIcon(const std::string& appId);
    
    // Get icon size
    int getIconSize() const { return 32; }

private:
    struct IconCacheEntry {
        GLuint textureId = 0;
        int size = 32;
    };
    
    static AppIconLoader* s_instance;
    
    std::unordered_map<std::string, IconCacheEntry> m_iconCache;
    std::vector<std::string> m_iconPaths;
    
    GLuint loadIconFromFile(const std::string& appId);
    GLuint loadIconFromPath(const std::string& path);
    GLuint generatePlaceholderIcon(const std::string& name);
};

} // namespace havel
