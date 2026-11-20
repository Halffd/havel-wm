#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace havel {

// Full-size PNG decode into ARGB8888 little-endian pixels (shared by the
// icon loader and the wallpaper path). False when the file is not a
// decodable PNG.
bool loadPngPixels(const std::string& path,
                   std::vector<uint32_t>& pixels, int* outW, int* outH);

/**
 * App Icon Loader
 *
 * Loads application icons for Alt-Tab and other overlays into CPU-side
 * RGBA pixel data. Searches system icon themes and caches loaded icons.
 *
 * Handles are opaque: a nonzero handle means pixel data is available via
 * getIconPixels()/getIconSize(); 0 means no icon found (callers fall back
 * to placeholder rendering). The previous GL-texture backend could never
 * work: it was created without any GL context (wlroots 0.20 exposes no
 * make_current), so glGenTextures was either a no-op or crashed.
 */
class AppIconLoader {
public:
    AppIconLoader();
    ~AppIconLoader();

    static AppIconLoader* getInstance();

    // Load icon for an app (returns cached handle if available, 0 if none)
    uint32_t loadIcon(const std::string& appId);

    // Icon display size in pixels
    int getIconSize() const { return 32; }

    // Pixel access for a loaded handle (nullptr if handle unknown)
    const uint32_t* getIconPixels(uint32_t handle) const;
    void getIconDimensions(uint32_t handle, int* w, int* h) const;

private:
    struct IconCacheEntry {
        uint32_t handle = 0;
        int width = 0;
        int height = 0;
        std::vector<uint32_t> pixels;  // ARGB8888 little-endian
    };

    static AppIconLoader* s_instance;

    std::unordered_map<std::string, IconCacheEntry> m_iconCache;   // appId -> icon
    std::unordered_map<uint32_t, IconCacheEntry*> m_handleIndex;  // handle -> icon
    uint32_t m_nextHandle = 1;

    std::vector<std::string> m_iconPaths;

    IconCacheEntry loadIconFromFile(const std::string& appId);
    IconCacheEntry loadIconFromPath(const std::string& path);
    IconCacheEntry generatePlaceholderIcon(const std::string& name);
};

} // namespace havel
