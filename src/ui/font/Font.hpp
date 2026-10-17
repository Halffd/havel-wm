#pragma once

// Font rendering for havel-wm.
//
// One module, two font classes through one library (FreeType):
//   - vector faces   (TTF/OTF)   - outline glyphs, any pixel size
//   - bitmap faces   (PCF/BDF/SNF) - fixed strikes; the nearest strike
//                    is selected and its (possibly 1/2/4-bit) bitmaps
//                    are unpacked to 8-bit coverage
//
// Output is CPU pixels in the exact layout the wayland/scene stack
// wants: ARGB8888 little-endian with PREMULTIPLIED alpha, i.e. what
// havel_scene_pixels() attaches as a wlr_scene_buffer
// (DRM_FORMAT_ARGB8888 is the premultiplied convention in
// wlroots/pixman). Overlays therefore hand Rasterized straight to
// havel_scene_pixels(); havel_scene_text() in render_c_wrappers.c is
// the one-call convenience.
//
// Face resolution order (see Font::initialize):
//   1. $HAVEL_FONT (any format FreeType can open)
//   2. well-known vector families in the usual font directories
//   3. any vector font found there
//   4. bitmap fonts (PCF/BDF) found there
// A box with zero vector fonts installed still gets text if it ships
// the classic X bitmap fonts; before this module existed, five
// hardcoded TTF paths were the only option and every overlay silently
// rendered without text when none matched.

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace havel::font {

// Rasterized text line. pixels.empty() <=> nothing was rendered
// (no face, empty text, or no glyphs).
struct Rasterized {
    std::vector<uint32_t> pixels;  // ARGB8888 little-endian, premultiplied
    int w = 0;
    int h = 0;
    bool empty() const { return pixels.empty(); }
};

struct RGBA {
    float r = 1.0f, g = 1.0f, b = 1.0f, a = 1.0f;
};

class Font {
public:
    Font() = default;
    ~Font();
    Font(const Font&) = delete;
    Font& operator=(const Font&) = delete;

    // Load a face following the resolution order above. Re-entrant:
    // a failed initialize may be retried (the next call re-runs
    // discovery). Returns ok().
    bool initialize();

    bool ok() const { return m_face != nullptr; }

    // Human-readable provenance for the log, e.g.
    //   /usr/share/fonts/.../LiberationSans-Regular.ttf (vector)
    //   /usr/share/fonts/misc/ter-u16b.pcf.gz (bitmap, strike 16)
    std::string describe() const;

    // Why the last initialize() failed (empty when it did not).
    const std::string& reason() const { return m_reason; }

    // Rasterize one line of UTF-8 text at the given pixel size.
    // Results are cached per (text, size, color); the cache is bounded.
    Rasterized rasterize(const std::string& text, uint32_t px, const RGBA& color);

private:
    struct Key {
        std::string text;
        uint32_t px;
        uint32_t color;  // packed ARGB
        bool operator==(const Key& o) const {
            return px == o.px && color == o.color && text == o.text;
        }
    };
    struct KeyHash {
        size_t operator()(const Key& k) const {
            return std::hash<std::string>()(k.text) ^ (k.px * 31) ^ (k.color * 17);
        }
    };

    void* m_ft = nullptr;    // FT_Library (owned)
    void* m_face = nullptr;  // FT_Face (owned)
    bool m_bitmapFace = false;
    std::string m_path;
    std::string m_reason;
    std::unordered_map<Key, Rasterized, KeyHash> m_cache;
};

} // namespace havel::font
