// App Icon Loader - Load icons from system theme or .desktop files
// CPU-side pixel storage; rendering happens through the scene graph.

#include <wm/render/AppIconLoader.hpp>
#include <Logger.h>
#include <cstring>
#include <cstdlib>
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
    m_iconCache.clear();
    m_handleIndex.clear();
    s_instance = nullptr;
}

AppIconLoader* AppIconLoader::getInstance() {
    return s_instance;
}

uint32_t AppIconLoader::loadIcon(const std::string& appId) {
    if (appId.empty()) return 0;

    // Check cache first
    auto cached = m_iconCache.find(appId);
    if (cached != m_iconCache.end()) {
        return cached->second.handle;
    }

    // Try to load icon (0-handle entry still cached to avoid re-scanning)
    IconCacheEntry entry = loadIconFromFile(appId);
    if (entry.handle == 0) {
        entry.pixels.clear();
        entry.width = 0;
        entry.height = 0;
    }

    auto result = m_iconCache.emplace(appId, std::move(entry));
    if (result.first->second.handle != 0) {
        m_handleIndex[result.first->second.handle] = &result.first->second;
    }
    return result.first->second.handle;
}

const uint32_t* AppIconLoader::getIconPixels(uint32_t handle) const {
    auto it = m_handleIndex.find(handle);
    if (it == m_handleIndex.end()) return nullptr;
    return it->second->pixels.data();
}

void AppIconLoader::getIconDimensions(uint32_t handle, int* w, int* h) const {
    auto it = m_handleIndex.find(handle);
    if (it == m_handleIndex.end()) {
        if (w) *w = 0;
        if (h) *h = 0;
        return;
    }
    if (w) *w = it->second->width;
    if (h) *h = it->second->height;
}

AppIconLoader::IconCacheEntry AppIconLoader::loadIconFromFile(const std::string& appId) {
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
    }

    // Try the primary name (org.foo.Bar -> foo, bar)
    size_t dot1 = appId.find('.');
    if (dot1 != std::string::npos) {
        std::string rest = appId.substr(dot1 + 1);
        size_t dot2 = rest.find('.');
        std::string middle = (dot2 != std::string::npos) ? rest.substr(0, dot2) : rest;
        if (!middle.empty()) {
            std::string lower = middle;
            std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
            altNames.push_back(lower);
            altNames.push_back(lower + ".png");
        }
    }

    // Search all paths
    for (const auto& path : m_iconPaths) {
        for (const auto& name : iconNames) {
            std::string full = path + name;
            IconCacheEntry entry = loadIconFromPath(full);
            if (entry.handle != 0) return entry;
        }
    }
    for (const auto& path : m_iconPaths) {
        for (const auto& name : altNames) {
            std::string full = path + name;
            IconCacheEntry entry = loadIconFromPath(full);
            if (entry.handle != 0) return entry;
        }
    }

    // Fallback: generate colored placeholder
    LOG_DEBUG("[AppIconLoader] Using placeholder for: %s", appId.c_str());
    return generatePlaceholderIcon(appId);
}

// Full-size PNG decode into ARGB8888 little-endian pixels. Shared by the
// icon loader and the wallpaper path - the wallpaper must NOT go through
// the icon loader, which only searches theme directories and falls back
// to a 32x32 placeholder (observed: an absolute wallpaper path produced
// a stretched hash-coloured square).
bool loadPngPixels(const std::string& path,
                   std::vector<uint32_t>& pixels, int* outW, int* outH) {
    pixels.clear();
    if (outW) *outW = 0;
    if (outH) *outH = 0;
    if (path.length() < 4) return false;
    std::string ext = path.substr(path.length() - 4);
    if (ext != ".png" && ext != ".PNG") return false;

    // Open PNG file
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return false;

    // Check PNG signature
    unsigned char header[8];
    size_t readCount = fread(header, 1, 8, f);
    if (readCount != 8 || png_sig_cmp(header, 0, 8) != 0) {
        fclose(f);
        return false;
    }

    // Create PNG read struct
    png_structp png = png_create_read_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
    if (!png) {
        fclose(f);
        return false;
    }

    png_infop info = png_create_info_struct(png);
    if (!info) {
        png_destroy_read_struct(&png, nullptr, nullptr);
        fclose(f);
        return false;
    }

    if (setjmp(png_jmpbuf(png))) {
        png_destroy_read_struct(&png, &info, nullptr);
        fclose(f);
        return false;
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
    // libpng outputs [R,G,B,A] in memory; the scene buffer expects
    // [B,G,R,A] (DRM_FORMAT_ARGB8888). Always swap — for RGBA, and for
    // RGB/GRAY/PALETTE after png_set_add_alpha fills in the alpha byte.
    png_set_bgr(png);

    png_read_update_info(png, info);

    size_t rowBytes = png_get_rowbytes(png, info);
    if (rowBytes != width * 4) {
        // Unexpected packing; bail honestly
        png_destroy_read_struct(&png, &info, nullptr);
        fclose(f);
        return false;
    }

    // Read image data (ARGB8888 little-endian)
    pixels.resize((size_t)width * height);
    std::vector<png_bytep> rowPointers(height);
    for (png_uint_32 y = 0; y < height; y++) {
        rowPointers[y] = reinterpret_cast<png_bytep>(&pixels[(size_t)y * width]);
    }
    png_read_image(png, rowPointers.data());
    png_read_end(png, nullptr);

    png_destroy_read_struct(&png, &info, nullptr);
    fclose(f);

    if (outW) *outW = (int)width;
    if (outH) *outH = (int)height;
    return true;
}

AppIconLoader::IconCacheEntry AppIconLoader::loadIconFromPath(const std::string& path) {
    IconCacheEntry entry;
    // SVG would need librsvg; the icon loader uses a placeholder for it.
    if (path.length() >= 4) {
        std::string ext = path.substr(path.length() - 4);
        if (ext == ".svg" || ext == ".SVG") {
            return generatePlaceholderIcon(path);
        }
    }
    if (loadPngPixels(path, entry.pixels, &entry.width, &entry.height)) {
        entry.handle = m_nextHandle++;
        LOG_DEBUG("[AppIconLoader] Loaded PNG icon: %s (%dx%d)",
                  path.c_str(), entry.width, entry.height);
    }
    return entry;
}

AppIconLoader::IconCacheEntry AppIconLoader::generatePlaceholderIcon(const std::string& name) {
    IconCacheEntry entry;
    entry.width = 32;
    entry.height = 32;
    entry.pixels.assign(32 * 32, 0);

    // Generate color from name hash
    unsigned int hash = 0;
    for (char c : name) {
        hash = hash * 31 + (unsigned char)c;
    }
    uint32_t r = (hash >> 16) & 0xFF;
    uint32_t g = (hash >> 8) & 0xFF;
    uint32_t b = hash & 0xFF;

    // ARGB8888 little-endian: [B][G][R][A]
    uint32_t argb = (b << 24) | (g << 16) | (r << 8) | 0xFFu;
    for (auto& px : entry.pixels) {
        px = argb;
    }
    entry.handle = m_nextHandle++;
    return entry;
}

} // namespace havel
