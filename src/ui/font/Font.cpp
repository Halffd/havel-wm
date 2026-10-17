// Font rendering implementation. See Font.hpp for the architecture.

#include <ui/font/Font.hpp>

#include <Logger.h>
#include <ft2build.h>
#include <freetype/freetype.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <system_error>

namespace havel::font {

namespace {

// Font file classification. FreeType opens all of these; the split
// matters only for discovery order (prefer vector outlines, fall back
// to bitmap strikes).
enum class Kind { Vector, Bitmap, Unreadable };

Kind classify(const std::string& path) {
    static const char* bitmapExt[] = {".pcf", ".bdf", ".snf", ".pcf.gz", ".bdf.gz", nullptr};
    static const char* vectorExt[] = {".ttf", ".otf", ".ttc", ".pfb", nullptr};
    std::string lower = path;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);

    for (int i = 0; bitmapExt[i]; i++) {
        std::string e = bitmapExt[i];
        if (lower.size() >= e.size() &&
            lower.compare(lower.size() - e.size(), e.size(), e) == 0) {
            return Kind::Bitmap;
        }
    }
    for (int i = 0; vectorExt[i]; i++) {
        std::string e = vectorExt[i];
        if (lower.size() >= e.size() &&
            lower.compare(lower.size() - e.size(), e.size(), e) == 0) {
            return Kind::Vector;
        }
    }
    return Kind::Unreadable;
}

std::string basenameOf(const std::string& path) {
    size_t slash = path.find_last_of('/');
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

} // namespace

Font::~Font() {
    if (m_face) {
        FT_Done_Face(static_cast<FT_Face>(m_face));
        m_face = nullptr;
    }
    if (m_ft) {
        FT_Done_FreeType(static_cast<FT_Library>(m_ft));
        m_ft = nullptr;
    }
}

bool Font::initialize() {
    // Clean slate: a retry after a failed or changed-font init must not
    // keep the previous face around.
    m_reason.clear();
    if (m_face) {
        FT_Done_Face(static_cast<FT_Face>(m_face));
        m_face = nullptr;
    }
    if (!m_ft) {
        FT_Library lib = nullptr;
        if (FT_Init_FreeType(&lib) != 0) {
            m_reason = "FreeType init failed";
            return false;
        }
        m_ft = lib;
    }

    // --- resolve a face ------------------------------------------------
    // 1. explicit override: whatever FreeType can open. If the override
    // is set but unusable, FAIL - falling back to discovery would
    // silently substitute a different font and hide the intent.
    std::string chosen;
    if (const char* env = getenv("HAVEL_FONT"); env && *env) {
        std::error_code ec;
        if (!std::filesystem::is_regular_file(env, ec)) {
            m_reason = std::string("HAVEL_FONT=") + env + " is not a file";
            return false;
        }
        FT_Face envFace = nullptr;
        const FT_Error err = FT_New_Face(static_cast<FT_Library>(m_ft), env, 0, &envFace);
        if (err != 0) {
            m_reason = "FreeType cannot open " + std::string(env) +
                        " (error " + std::to_string(err) + ")";
            return false;
        }
        m_face = envFace;
        m_path = env;
        m_bitmapFace = !(envFace->face_flags & FT_FACE_FLAG_SCALABLE) &&
                      (envFace->face_flags & FT_FACE_FLAG_FIXED_SIZES);
        return true;
    }

    static const char* dirs[] = {
        "/usr/share/fonts",
        "/usr/local/share/fonts",
        "/run/host/fonts",
        nullptr
    };
    // Preferred vector families, best-known first.
    static const char* preferred[] = {
        "DejaVuSans.ttf",
        "LiberationSans-Regular.ttf",
        "AdwaitaSans-Regular.ttf",
        "NotoSans-Regular.ttf",
        "FreeSans.ttf",
        "Roboto-Regular.ttf",
        nullptr
    };

    std::vector<std::string> vectorFonts;
    std::vector<std::string> bitmapFonts;
    if (chosen.empty()) {
        std::error_code ec;
        for (int d = 0; dirs[d]; d++) {
            if (!std::filesystem::is_directory(dirs[d], ec)) continue;
            // Whole-tree walk: runs once at startup.
            for (auto it = std::filesystem::recursive_directory_iterator(
                     dirs[d], std::filesystem::directory_options::skip_permission_denied, ec);
                 !ec && it != std::filesystem::recursive_directory_iterator(); it.increment(ec)) {
                if (!it->is_regular_file(ec)) continue;
                const std::string p = it->path().string();
                switch (classify(p)) {
                    case Kind::Vector: vectorFonts.push_back(p); break;
                    case Kind::Bitmap: bitmapFonts.push_back(p); break;
                    default: break;
                }
            }
            ec.clear();
        }
        for (int p = 0; preferred[p]; p++) {
            for (const auto& f : vectorFonts) {
                const std::string base = basenameOf(f);
                if (base.size() >= strlen(preferred[p]) &&
                    base.compare(base.size() - strlen(preferred[p]),
                                 strlen(preferred[p]), preferred[p]) == 0) {
                    chosen = f;
                    break;
                }
            }
            if (!chosen.empty()) break;
        }
        if (chosen.empty() && !vectorFonts.empty()) chosen = vectorFonts.front();
        if (chosen.empty() && !bitmapFonts.empty()) chosen = bitmapFonts.front();
    }

    if (chosen.empty()) {
        m_reason = "no font found (searched HAVEL_FONT, the usual font "
                   "directories for TTF/OTF/PCF/BDF)";
        return false;
    }

    // --- open it -------------------------------------------------------
    FT_Face face = nullptr;
    const FT_Error err = FT_New_Face(static_cast<FT_Library>(m_ft), chosen.c_str(), 0, &face);
    if (err != 0) {
        m_reason = "FreeType cannot open " + chosen + " (error " + std::to_string(err) + ")";
        return false;
    }
    m_face = face;
    m_path = chosen;
    m_bitmapFace = !(face->face_flags & FT_FACE_FLAG_SCALABLE) &&
                   (face->face_flags & FT_FACE_FLAG_FIXED_SIZES);
    return true;
}

std::string Font::describe() const {
    if (!m_face) return "no font";
    const FT_Face face = static_cast<FT_Face>(m_face);
    if (m_bitmapFace && face->num_fixed_sizes > 0) {
        const FT_Bitmap_Size& s = face->available_sizes[0];
        return m_path + " (bitmap, strike " + std::to_string(s.width) + "x" +
               std::to_string(s.height) + ")";
    }
    return m_path + " (vector)";
}

namespace {

// UTF-8 -> codepoints. The previous implementation fed raw BYTES to
// FreeType, so any non-ASCII string rendered as garbage; ASCII strings
// are unchanged by this decode.
std::vector<uint32_t> decodeUtf8(const std::string& text) {
    std::vector<uint32_t> out;
    out.reserve(text.size());
    size_t i = 0;
    while (i < text.size()) {
        unsigned char c = (unsigned char)text[i];
        uint32_t cp = 0;
        int extra = 0;
        if (c < 0x80) {
            cp = c;
        } else if ((c & 0xE0) == 0xC0 && i + 1 < text.size()) {
            cp = (c & 0x1F); extra = 1;
        } else if ((c & 0xF0) == 0xE0 && i + 2 < text.size()) {
            cp = (c & 0x0F); extra = 2;
        } else if ((c & 0xF8) == 0xF0 && i + 3 < text.size()) {
            cp = (c & 0x07); extra = 3;
        } else {
            cp = 0xFFFD;  // invalid byte: replacement, skip it
        }
        for (int k = 0; k < extra; k++) {
            unsigned char cc = (unsigned char)text[i + 1 + k];
            if ((cc & 0xC0) != 0x80) { cp = 0xFFFD; extra = k; break; }
            cp = (cp << 6) | (cc & 0x3F);
        }
        i += 1 + extra;
        out.push_back(cp);
    }
    return out;
}

inline uint32_t packColor(const RGBA& c) {
    uint32_t r = (uint32_t)(c.r * 255.0f) & 0xFFu;
    uint32_t g = (uint32_t)(c.g * 255.0f) & 0xFFu;
    uint32_t b = (uint32_t)(c.b * 255.0f) & 0xFFu;
    uint32_t a = (uint32_t)(c.a * 255.0f) & 0xFFu;
    return (b << 24) | (g << 16) | (r << 8) | a;  // little-endian ARGB8888
}

// 8-bit coverage for one pixel of a possibly-packed FreeType bitmap.
// Bitmap faces (PCF/BDF) deliver 1-bit, 2-bit or 4-bit rows; the old
// code assumed 8-bit gray everywhere, so mono glyphs would have read
// garbage from the packed rows.
inline unsigned coverageAt(const FT_Bitmap* bmp, unsigned row, unsigned col) {
    if (col >= bmp->width) return 0;
    switch (bmp->pixel_mode) {
        case FT_PIXEL_MODE_MONO: {
            unsigned char byte = bmp->buffer[row * bmp->pitch + col / 8];
            return (byte & (0x80u >> (col % 8))) ? 255 : 0;
        }
        case FT_PIXEL_MODE_GRAY2: {
            unsigned char byte = bmp->buffer[row * bmp->pitch + col / 4];
            unsigned v = (byte >> (6 - 2 * (col % 4))) & 3u;
            return v * 255 / 3;
        }
        case FT_PIXEL_MODE_GRAY4: {
            unsigned char byte = bmp->buffer[row * bmp->pitch + col / 2];
            unsigned v = (byte >> (4 - 4 * (col % 2))) & 15u;
            return v * 255 / 15;
        }
        case FT_PIXEL_MODE_GRAY:
        default:
            return bmp->buffer[row * bmp->pitch + col];
    }
}

} // namespace

Rasterized Font::rasterize(const std::string& text, uint32_t px, const RGBA& color) {
    Rasterized out;
    if (!m_face || text.empty()) return out;

    const uint32_t argb = packColor(color);
    const Key key{text, px, argb};
    auto hit = m_cache.find(key);
    if (hit != m_cache.end()) return hit->second;

    FT_Face face = static_cast<FT_Face>(m_face);

    // Size selection. Scalable faces set any pixel size; bitmap faces
    // have fixed strikes, so pick the nearest available one.
    if (FT_FACE_FLAG_SCALABLE & face->face_flags) {
        if (FT_Set_Pixel_Sizes(face, 0, px) != 0) return out;
    } else if (face->num_fixed_sizes > 0) {
        int best = 0;
        int bestDiff = 1 << 30;
        for (int i = 0; i < face->num_fixed_sizes; i++) {
            int h = face->available_sizes[i].height;
            int diff = h > (int)px ? h - (int)px : (int)px - h;
            if (diff < bestDiff) { bestDiff = diff; best = i; }
        }
        if (FT_Select_Size(face, best) != 0) return out;
    } else {
        return out;
    }

    const std::vector<uint32_t> codepoints = decodeUtf8(text);

    // Pass 1: measure (advances for width; bitmap extents for height)
    int width = 0, asc = 0, desc = 0;
    for (uint32_t cp : codepoints) {
        if (FT_Load_Char(face, cp, FT_LOAD_RENDER)) continue;
        FT_GlyphSlot g = face->glyph;
        width += (g->advance.x >> 6);
        int top = g->bitmap_top;
        int bot = top - (int)g->bitmap.rows;
        if (top > asc) asc = top;
        if (bot < desc) desc = bot;
    }
    if (width <= 0) return out;
    int height = asc - desc;
    if (height <= 0) height = (int)px;

    out.w = width;
    out.h = height;
    out.pixels.assign((size_t)width * height, 0);

    // Pass 2: composite glyphs with the PREMULTIPLIED over-operator.
    // The scene buffer is DRM_FORMAT_ARGB8888 = premultiplied alpha
    // (wlroots/pixman convention): straight RGB with partial alpha
    // renders glyph fringes at full brightness instead of dimming with
    // coverage. The buffer starts transparent, so dst RGB is always
    // already premultiplied.
    int penX = 0;
    for (uint32_t cp : codepoints) {
        if (FT_Load_Char(face, cp, FT_LOAD_RENDER)) continue;
        FT_GlyphSlot g = face->glyph;
        FT_Bitmap* bmp = &g->bitmap;
        const int gx = penX + g->bitmap_left;
        const int gy = asc - g->bitmap_top;

        const uint32_t sr = (argb >> 8) & 0xFFu;   // unpremultiplied src RGB
        const uint32_t sg = (argb >> 16) & 0xFFu;
        const uint32_t sb = (argb >> 24) & 0xFFu;

        for (unsigned row = 0; row < bmp->rows; row++) {
            const int dy = gy + (int)row;
            if (dy < 0 || dy >= height) continue;
            for (unsigned col = 0; col < bmp->width; col++) {
                const int dx = gx + (int)col;
                if (dx < 0 || dx >= width) continue;
                const unsigned a = coverageAt(bmp, row, col);
                if (!a) continue;
                uint32_t& dst = out.pixels[(size_t)dy * width + dx];

                if (a == 255) {
                    dst = (sb << 24) | (sg << 16) | (sr << 8) | 0xFFu;
                } else {
                    const uint32_t pr = sr * a / 255;   // premultiplied source
                    const uint32_t pg = sg * a / 255;
                    const uint32_t pb = sb * a / 255;
                    const uint32_t da = dst & 0xFFu;
                    const uint32_t inv = 255 - a;
                    const uint32_t dstr = (dst >> 8) & 0xFFu;   // premult dst
                    const uint32_t dstg = (dst >> 16) & 0xFFu;
                    const uint32_t dstb = (dst >> 24) & 0xFFu;
                    uint32_t or_ = pr + dstr * inv / 255;
                    uint32_t og = pg + dstg * inv / 255;
                    uint32_t ob = pb + dstb * inv / 255;
                    uint32_t oa = a + da * inv / 255;
                    if (or_ > 255) or_ = 255;
                    if (og > 255) og = 255;
                    if (ob > 255) ob = 255;
                    if (oa > 255) oa = 255;
                    dst = (ob << 24) | (og << 16) | (or_ << 8) | oa;
                }
            }
        }
        penX += (g->advance.x >> 6);
    }

    // Bounded cache; re-rasterize on eviction (UI strings are short).
    if (m_cache.size() > 256) m_cache.clear();
    m_cache.emplace(key, out);
    return out;
}

} // namespace havel::font

// ============================================================================
// C ABI for the rest of the compositor (render_c.h). Same signatures as
// the ones that used to live in OverlayRenderer.cpp; both the C++ and
// the C callers now share this one implementation.
// ============================================================================

extern "C" int havel_text_rasterize(const char* text, int px, const float color[4],
                                    uint32_t** out_pixels, int* out_w, int* out_h) {
    using namespace havel::font;
    if (!text || !out_pixels || !out_w || !out_h || px <= 0) return 0;

    static Font st;          // C-side overlays (the boot splash) have no
    static bool tried = false;  // renderer instance; one shared face.
    if (!tried) {
        tried = true;
        if (!st.initialize()) {
            LOG_WARN("[font] text unavailable for C overlays: %s", st.reason().c_str());
        } else {
            LOG_INFO("[font] C overlay face: %s", st.describe().c_str());
        }
    }
    if (!st.ok()) return 0;

    RGBA rgba{color[0], color[1], color[2], color[3]};
    Rasterized rz = st.rasterize(text, (uint32_t)px, rgba);
    if (rz.empty()) return 0;

    *out_pixels = (uint32_t*)malloc(rz.pixels.size() * sizeof(uint32_t));
    if (!*out_pixels) return 0;
    memcpy(*out_pixels, rz.pixels.data(), rz.pixels.size() * sizeof(uint32_t));
    *out_w = rz.w;
    *out_h = rz.h;
    return 1;
}

extern "C" void havel_text_pixels_free(uint32_t* pixels) {
    free(pixels);
}
