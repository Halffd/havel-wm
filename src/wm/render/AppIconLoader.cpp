// App Icon Loader - Load icons from system theme or .desktop files
// CPU-side pixel storage; rendering happens through the scene graph.

#include <wm/render/AppIconLoader.hpp>
#include <Logger.h>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <cmath>
#include <algorithm>
#include <dirent.h>
#include <sys/stat.h>
#include <png.h>

// SVG icons are common (alacritty ships only an SVG) and every desktop
// theme keeps a scalable/ directory. Rasterized with librsvg when it is
// available; without it the loader falls back to a placeholder, as before.
#ifdef HAVE_LIBRSVG
#include <librsvg/rsvg.h>
#include <cairo.h>
#endif

namespace havel {

AppIconLoader* AppIconLoader::s_instance = nullptr;

// Every icon is normalized to this size before caching: the consumers
// (launcher, panel, alt-tab) copy getIconSize()*getIconSize() pixels
// straight out of the handle, while decoded PNGs come in every size
// from 16 to 512 (kitty.png is 256x256, firefox 192x192 - the 32x32
// copy sheared them; observed).
static constexpr int kIconDisplaySize = 32;

static bool fileExists(const std::string& p) {
    struct stat st;
    return stat(p.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}

// Rasterize an SVG to premultiplied ARGB8888 (cairo's ARGB32 is exactly
// this layout on little-endian: [B][G][R][A]). Rendered at 2x the display
// size so the box filter below has real samples to work with.
static bool rasterizeSvg(const std::string& path, std::vector<uint32_t>& out,
                         int* outW, int* outH) {
#ifdef HAVE_LIBRSVG
    const int S = kIconDisplaySize * 2;
    GError* err = nullptr;
    RsvgHandle* handle = rsvg_handle_new_from_file(path.c_str(), &err);
    if (!handle) {
        if (err) g_error_free(err);
        return false;
    }
    cairo_surface_t* surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, S, S);
    if (cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS) {
        cairo_surface_destroy(surface);
        g_object_unref(handle);
        return false;
    }
    cairo_t* cr = cairo_create(surface);
    RsvgRectangle viewport = {0, 0, (double)S, (double)S};
    gboolean ok = rsvg_handle_render_document(handle, cr, &viewport, &err);
    cairo_destroy(cr);
    if (!ok) {
        if (err) g_error_free(err);
        cairo_surface_destroy(surface);
        g_object_unref(handle);
        return false;
    }
    cairo_surface_flush(surface);
    const unsigned char* data = cairo_image_surface_get_data(surface);
    const int stride = cairo_image_surface_get_stride(surface);
    out.resize((size_t)S * S);
    for (int y = 0; y < S; y++) {
        memcpy(out.data() + (size_t)y * S, data + (size_t)y * stride, (size_t)S * 4);
    }
    cairo_surface_destroy(surface);
    g_object_unref(handle);
    *outW = S;
    *outH = S;
    return true;
#else
    (void)path;
    (void)out;
    (void)outW;
    (void)outH;
    return false;
#endif
}

// Box-filter resample (premultiplied, so transparent pixels do not
// bleed black into the edges) into a kIconDisplaySize square, fit
// preserving aspect ratio and padding the short axis transparent.
static std::vector<uint32_t> fitIconToDisplay(const std::vector<uint32_t>& src,
                                              int sw, int sh) {
    std::vector<uint32_t> out((size_t)kIconDisplaySize * kIconDisplaySize, 0);
    if (sw <= 0 || sh <= 0 || src.size() < (size_t)sw * sh) return out;
    double scale = std::min((double)kIconDisplaySize / sw,
                            (double)kIconDisplaySize / sh);
    int dw = std::max(1, (int)std::lround(sw * scale));
    int dh = std::max(1, (int)std::lround(sh * scale));
    int ox = (kIconDisplaySize - dw) / 2;
    int oy = (kIconDisplaySize - dh) / 2;
    for (int dy = 0; dy < dh; dy++) {
        int y0 = (int)((int64_t)dy * sh / dh);
        int y1 = std::max(y0 + 1, (int)((int64_t)(dy + 1) * sh / dh));
        for (int dx = 0; dx < dw; dx++) {
            int x0 = (int)((int64_t)dx * sw / dw);
            int x1 = std::max(x0 + 1, (int)((int64_t)(dx + 1) * sw / dw));
            uint64_t accA = 0, accR = 0, accG = 0, accB = 0, n = 0;
            for (int sy = y0; sy < y1 && sy < sh; sy++) {
                for (int sx = x0; sx < x1 && sx < sw; sx++) {
                    uint32_t p = src[(size_t)sy * sw + sx];
                    uint32_t a = (p >> 24) & 0xFF;
                    accA += a;
                    accR += ((p >> 16) & 0xFF) * a;   // premultiplied
                    accG += ((p >> 8) & 0xFF) * a;
                    accB += (p & 0xFF) * a;
                    n++;
                }
            }
            if (!n) continue;
            uint32_t a = (uint32_t)(accA / n);
            uint32_t r = a ? (uint32_t)(accR / accA) : 0;
            uint32_t g = a ? (uint32_t)(accG / accA) : 0;
            uint32_t b = a ? (uint32_t)(accB / accA) : 0;
            out[(size_t)(oy + dy) * kIconDisplaySize + (ox + dx)] =
                (a << 24) | (r << 16) | (g << 8) | b;
        }
    }
    return out;
}

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
    // Try common icon names and extensions. PNG only: the loader decodes
    // PNG, and a candidate it cannot render must stay a miss so the
    // search continues - trying .svg/.xpm here made every lookup resolve
    // to a placeholder keyed by the *path* (loadIconFromPath handed out
    // a nonzero handle for any .svg it was asked about), shadowing the
    // real PNGs further down the list and in the theme dirs. Observed:
    // foot/firefox/kitty all got path-hash placeholders while their PNGs
    // sat untouched.
    // SVG last: a PNG of the same name wins, and a missing .svg is a
    // miss (see loadIconFromPath) so it cannot shadow the real files.
    std::vector<std::string> iconNames = {
        appId,
        appId + ".png",
        appId + ".svg",
    };

    // Also try with common replacements
    std::vector<std::string> altNames;
    std::string lowerId = appId;
    std::transform(lowerId.begin(), lowerId.end(), lowerId.begin(), ::tolower);

    // Try lowercase
    if (lowerId != appId) {
        altNames.push_back(lowerId);
        altNames.push_back(lowerId + ".png");
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

    // Icon-theme size directories: <icons>/<theme>/<NxN>/apps/<name>.png
    // (hicolor first, then the rest). Freedesktop themes keep app icons
    // under size dirs, not at the theme root - the old flat-only search
    // missed foot (hicolor/48x48) and firefox (hicolor/192x192) even
    // though they were installed, and every such app got a placeholder
    // (observed).
    {
        static const int sizes[] = {512, 256, 192, 128, 96, 64, 48, 36, 32, 24, 22, 16};
        std::vector<std::string> flatNames;
        flatNames.reserve(iconNames.size() + altNames.size());
        for (const auto& n : iconNames) flatNames.push_back(n);
        for (const auto& n : altNames) flatNames.push_back(n);
        for (const auto& base : m_iconPaths) {
            // 6 chars, not 7: "icons/" is six. The old guard compared 7
            // chars against a 6-char needle, so it never matched and the
            // theme pass below never ran (observed: theme icons still
            // fell back to placeholders).
            if (base.size() < 6 || base.compare(base.size() - 6, 6, "icons/") != 0) continue;
            std::vector<std::string> themes;
            DIR* dir = opendir(base.c_str());
            if (!dir) continue;
            while (struct dirent* de = readdir(dir)) {
                if (de->d_name[0] == '.') continue;
                if (strcmp(de->d_name, "cursors") == 0) continue;
                struct stat st;
                std::string full = base + de->d_name;
                if (stat(full.c_str(), &st) == 0 && S_ISDIR(st.st_mode)) {
                    themes.push_back(de->d_name);
                }
            }
            closedir(dir);
            std::sort(themes.begin(), themes.end());
            auto hi = std::find(themes.begin(), themes.end(), "hicolor");
            if (hi != themes.end()) std::rotate(themes.begin(), hi, hi + 1);
            for (const auto& theme : themes) {
                for (int s : sizes) {
                    char sizeDir[32];
                    snprintf(sizeDir, sizeof sizeDir, "%dx%d/apps/", s, s);
                    for (const auto& n : flatNames) {
                        // Candidates may still carry a .png suffix
                        // (kitty-style flat hits add one); strip any
                        // known suffix so the theme-dir attempt stays a
                        // plain append instead of foot.svg.png.
                        std::string stem = n;
                        for (const char* ext : {".png", ".svg", ".xpm"}) {
                            size_t el = strlen(ext);
                            if (stem.size() > el &&
                                stem.compare(stem.size() - el, el, ext) == 0) {
                                stem.erase(stem.size() - el);
                                break;
                            }
                        }
                        IconCacheEntry entry =
                            loadIconFromPath(base + theme + "/" + sizeDir + stem + ".png");
                        if (entry.handle != 0) return entry;
                    }
                }
                // Theme scalable dirs (hicolor/scalable/apps) - the usual
                // home of SVG-only icons, e.g. foot and most GNOME apps.
                // Tried after the raster sizes so a PNG still wins.
                for (const auto& n : flatNames) {
                    std::string stem = n;
                    for (const char* ext : {".png", ".svg", ".xpm"}) {
                        size_t el = strlen(ext);
                        if (stem.size() > el &&
                            stem.compare(stem.size() - el, el, ext) == 0) {
                            stem.erase(stem.size() - el);
                            break;
                        }
                    }
                    IconCacheEntry entry = loadIconFromPath(
                        base + theme + "/scalable/apps/" + stem + ".svg");
                    if (entry.handle != 0) return entry;
                }
            }
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
    // SVG: rasterize when librsvg is available, else the historical
    // placeholder. The existence check is the whole point: without it
    // every .svg *candidate* - most of which do not exist - resolved to a
    // nonzero handle and aborted the search before it reached the real
    // PNG candidates (observed: every app on this box got a path-hash
    // placeholder). Missing files now fall through as a miss.
    if (path.length() >= 4) {
        std::string ext = path.substr(path.length() - 4);
        if ((ext == ".svg" || ext == ".SVG") && fileExists(path)) {
            int sw = 0, sh = 0;
            if (rasterizeSvg(path, entry.pixels, &sw, &sh)) {
                entry.pixels = fitIconToDisplay(entry.pixels, sw, sh);
                entry.width = kIconDisplaySize;
                entry.height = kIconDisplaySize;
                entry.handle = m_nextHandle++;
                LOG_DEBUG("[AppIconLoader] Rasterized SVG icon: %s", path.c_str());
                return entry;
            }
            return generatePlaceholderIcon(path);
        }
    }
    if (loadPngPixels(path, entry.pixels, &entry.width, &entry.height)) {
        // Normalize every icon to the display size the consumers index
        // by (getIconSize() squared). Decoded PNGs run 16..512 px; the
        // raw size used to be handed out, so a 256x256 kitty.png got
        // read as 32x32 and rendered sheared (observed).
        if (entry.width != kIconDisplaySize || entry.height != kIconDisplaySize) {
            entry.pixels = fitIconToDisplay(entry.pixels, entry.width, entry.height);
            entry.width = kIconDisplaySize;
            entry.height = kIconDisplaySize;
        }
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

    // ARGB8888 little-endian: in-memory [B][G][R][A] = 0xAARRGGBB, the
    // same layout loadPngPixels produces. The old packing put 0xFF in
    // the low byte and blue in the alpha byte - placeholders rendered
    // with swapped channels and semi-transparent (observed).
    uint32_t argb = 0xFF000000u | (r << 16) | (g << 8) | b;
    for (auto& px : entry.pixels) {
        px = argb;
    }
    entry.handle = m_nextHandle++;
    return entry;
}

} // namespace havel
