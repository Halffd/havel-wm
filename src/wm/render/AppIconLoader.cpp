// App Icon Loader - Load icons from system theme or .desktop files

#include <wm/render/AppIconLoader.hpp>
#include <Logger.h>
#include <cstring>
#include <algorithm>
#include <png.h>

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
    // Try common icon names and extensions
    std::vector<std::string> iconNames = {
        appId,
        appId + ".png",
        appId + ".svg",
        appId + ".xpm",
    };

    // Also try with common replacements
    std::vector<std::string> altNames;
    std::string lowerId = appId;
    std::transform(lowerId.begin(), lowerId.end(), lowerId.begin(), ::tolower);
    
    // Try lowercase
    if (lowerId != appId) {
        altNames.push_back(lowerId);
        altNames.push_back(lowerId + ".png");
        altNames.push_back(lowerId + ".svg");
    }
    
    // Try replacing dashes with underscores
    std::string underscoreId = appId;
    std::replace(underscoreId.begin(), underscoreId.end(), '-', '_');
    if (underscoreId != appId) {
        altNames.push_back(underscoreId);
        altNames.push_back(underscoreId + ".png");
        altNames.push_back(underscoreId + ".svg");
    }

    // Combine all names to try
    iconNames.insert(iconNames.end(), altNames.begin(), altNames.end());

    // Search through icon paths with size subdirectories
    for (const auto& iconPath : m_iconPaths) {
        for (const auto& iconName : iconNames) {
            // Try direct path first
            std::string fullPath = iconPath + iconName;
            GLuint texture = loadIconFromPath(fullPath);
            if (texture != 0) {
                LOG_DEBUG("[AppIconLoader] Loaded icon: %s", fullPath.c_str());
                return texture;
            }

            // Try with common sizes
            std::vector<std::string> sizes = {"32x32", "48x48", "64x64", "128x128", "256x256", "scalable"};
            std::vector<std::string> categories = {"apps", "applications", "mimetypes", "places", "devices"};
            
            for (const auto& size : sizes) {
                for (const auto& cat : categories) {
                    fullPath = iconPath + size + "/" + cat + "/" + iconName;
                    texture = loadIconFromPath(fullPath);
                    if (texture != 0) {
                        LOG_DEBUG("[AppIconLoader] Loaded icon: %s", fullPath.c_str());
                        return texture;
                    }
                }
            }
        }
    }

    // Fallback: generate colored placeholder
    LOG_DEBUG("[AppIconLoader] Using placeholder for: %s", appId.c_str());
    return generatePlaceholderIcon(appId);
}

GLuint AppIconLoader::loadIconFromPath(const std::string& path) {
    // Check file extension
    if (path.length() < 4) return 0;
    std::string ext = path.substr(path.length() - 4);
    
    // Only load PNG files for now
    if (ext != ".png" && ext != ".PNG") {
        // Try SVG - would need librsvg for proper rendering
        if (ext == ".svg" || ext == ".SVG") {
            // For SVG, generate placeholder with icon name
            return generatePlaceholderIcon(path);
        }
        return 0;
    }

    // Open PNG file
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return 0;

    // Check PNG signature
    unsigned char header[8];
    size_t readCount = fread(header, 1, 8, f);
    if (readCount != 8 || png_sig_cmp(header, 0, 8) != 0) {
        fclose(f);
        return 0;
    }

    // Create PNG read struct
    png_structp png = png_create_read_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
    if (!png) {
        fclose(f);
        return 0;
    }

    png_infop info = png_create_info_struct(png);
    if (!info) {
        png_destroy_read_struct(&png, nullptr, nullptr);
        fclose(f);
        return 0;
    }

    if (setjmp(png_jmpbuf(png))) {
        png_destroy_read_struct(&png, &info, nullptr);
        fclose(f);
        return 0;
    }

    png_init_io(png, f);
    png_set_sig_bytes(png, 8);
    png_read_info(png, info);

    // Get image info
    png_uint_32 width = png_get_image_width(png, info);
    png_uint_32 height = png_get_image_height(png, info);
    png_byte colorType = png_get_color_type(png, info);
    png_byte bitDepth = png_get_bit_depth(png, info);

    // Convert to 8-bit RGBA
    if (bitDepth == 16) png_set_strip_16(png);
    if (colorType == PNG_COLOR_TYPE_PALETTE) png_set_palette_to_rgb(png);
    if (colorType == PNG_COLOR_TYPE_GRAY && bitDepth < 8) png_set_expand_gray_1_2_4_to_8(png);
    if (png_get_valid(png, info, PNG_INFO_tRNS)) png_set_tRNS_to_alpha(png);
    if (colorType == PNG_COLOR_TYPE_RGB || colorType == PNG_COLOR_TYPE_GRAY || 
        colorType == PNG_COLOR_TYPE_PALETTE) {
        png_set_add_alpha(png, 0xFF, PNG_FILLER_AFTER);
    }
    if (colorType == PNG_COLOR_TYPE_GRAY || colorType == PNG_COLOR_TYPE_GRAY_ALPHA) {
        png_set_gray_to_rgb(png);
    }

    png_read_update_info(png, info);

    // Allocate row pointers and data
    std::vector<png_bytep> rowPointers(height);
    std::vector<png_byte> rowData(height * width * 4);
    
    for (png_uint_32 y = 0; y < height; y++) {
        rowPointers[y] = &rowData[y * width * 4];
    }

    // Read image data
    png_read_image(png, rowPointers.data());
    png_read_end(png, nullptr);

    // Create OpenGL texture
    GLuint texture;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);

    // Upload texture data
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, 
                 GL_UNSIGNED_BYTE, rowData.data());

    // Set texture parameters
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glBindTexture(GL_TEXTURE_2D, 0);

    // Cleanup
    png_destroy_read_struct(&png, &info, nullptr);
    fclose(f);

    LOG_DEBUG("[AppIconLoader] Loaded PNG icon: %s (%dx%d)", path.c_str(), width, height);
    return texture;
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
