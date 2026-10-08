#include "OverlayRenderer.hpp"
#include <wm/render_c.h>
#include <Logger.h>
#include <ft2build.h>
#include <freetype/freetype.h>
#include <cstring>
#include <cstdio>
#include <cmath>
#include <string>
#include <unordered_map>
#include <memory>
#include <vector>

namespace havel {

// ============================================================================
// Text rasterization: FreeType -> ARGB8888 line bitmap, cached per
// (text, pixel size, color). Rasterization is CPU-only; the resulting
// pixels go to the scene graph through the render_c boundary.
// ============================================================================

struct TextKey {
    std::string text;
    uint32_t px;
    uint32_t color;  // packed ARGB
    bool operator==(const TextKey& o) const {
        return px == o.px && color == o.color && text == o.text;
    }
};

struct TextKeyHash {
    size_t operator()(const TextKey& k) const {
        return std::hash<std::string>()(k.text) ^ (k.px * 31) ^ (k.color * 17);
    }
};

class SceneText {
public:
    SceneText() = default;
    ~SceneText() { shutdown(); }

    bool initialize() {
        if (FT_Init_FreeType(&m_ft)) {
            fprintf(stderr, "[SceneText] FreeType init failed\n");
            return false;
        }
        static const char* fontPaths[] = {
            "/usr/share/fonts/TTF/DejaVuSans.ttf",
            "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
            "/usr/share/fonts/dejavu/DejaVuSans.ttf",
            "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
            "/usr/share/fonts/gnu-free/FreeSans.ttf",
            nullptr
        };
        for (int i = 0; fontPaths[i]; i++) {
            if (FT_New_Face(m_ft, fontPaths[i], 0, &m_face) == 0) {
                m_ok = true;
                return true;
            }
        }
        fprintf(stderr, "[SceneText] No usable TTF font found\n");
        return false;
    }

    void shutdown() {
        if (m_face) { FT_Done_Face(m_face); m_face = nullptr; }
        if (m_ft) { FT_Done_FreeType(m_ft); m_ft = nullptr; }
        m_ok = false;
    }

    bool ok() const { return m_ok; }

    struct Rasterized {
        std::vector<uint32_t> pixels;  // ARGB8888 little-endian
        int w = 0, h = 0;
    };

    Rasterized rasterize(const std::string& text, uint32_t px, const Color& color) {
        Rasterized out;
        if (!m_ok || text.empty()) return out;

        uint32_t argb = packColor(color);
        TextKey key{text, px, argb};
        auto it = m_cache.find(key);
        if (it != m_cache.end()) return it->second;

        if (FT_Set_Pixel_Sizes(m_face, 0, px) != 0) return out;

        // First pass: measure (width from advances, ascent/descent from bitmaps)
        int width = 0;
        int asc = 0, desc = 0;
        for (unsigned char c : text) {
            if (FT_Load_Char(m_face, c, FT_LOAD_RENDER)) continue;
            FT_GlyphSlot g = m_face->glyph;
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

        // Second pass: composite glyphs. The scene buffer is
        // DRM_FORMAT_ARGB8888 = PREMULTIPLIED alpha (wlroots/pixman
        // convention): writing straight RGB with partial alpha makes
        // glyph edges render at full brightness (fringes) instead of
        // dimming with coverage. Proper premultiplied over-operator:
        //   out_a = sa + da*(1-sa)
        //   out_rgb = s_rgb*sa + d_rgb*(1-sa)   (all channels /255)
        // The buffer starts transparent (0), so dst RGB is already
        // premultiplied from any previous glyph overlap.
        int penX = 0;
        for (unsigned char c : text) {
            if (FT_Load_Char(m_face, c, FT_LOAD_RENDER)) continue;
            FT_GlyphSlot g = m_face->glyph;
            FT_Bitmap* bmp = &g->bitmap;
            int gx = penX + g->bitmap_left;
            int gy = asc - g->bitmap_top;
            for (unsigned row = 0; row < bmp->rows; row++) {
                int dy = gy + (int)row;
                if (dy < 0 || dy >= height) continue;
                for (unsigned col = 0; col < bmp->width; col++) {
                    int dx = gx + (int)col;
                    if (dx < 0 || dx >= width) continue;
                    unsigned char a = bmp->buffer[row * bmp->pitch + col];
                    if (!a) continue;
                    uint32_t& dst = out.pixels[(size_t)dy * width + dx];

                    uint32_t sr = (argb >> 8) & 0xFFu;  // unpremultiplied src RGB
                    uint32_t sg = (argb >> 16) & 0xFFu;
                    uint32_t sb = (argb >> 24) & 0xFFu;
                    // premultiplied source
                    uint32_t pr = sr * a / 255;
                    uint32_t pg = sg * a / 255;
                    uint32_t pb = sb * a / 255;

                    if (a == 255) {
                        dst = (sb << 24) | (sg << 16) | (sr << 8) | 0xFFu;
                    } else {
                        uint32_t da = dst & 0xFFu;
                        uint32_t inv = 255 - a;
                        uint32_t dstr = (dst >> 8) & 0xFFu;  // dst RGB (premult)
                        uint32_t dstg = (dst >> 16) & 0xFFu;
                        uint32_t dstb = (dst >> 24) & 0xFFu;
                        uint32_t or_ = pr + dstr * inv / 255;
                        uint32_t og = pg + dstg * inv / 255;
                        uint32_t ob = pb + dstb * inv / 255;
                        uint32_t oa = a + da * inv / 255;
                        if (or_ > 255) or_ = 255;
                        if (og > 255) og = 255;
                        if (ob > 255) ob = 255;
                        dst = (ob << 24) | (og << 16) | (or_ << 8) | oa;
                    }
                }
            }
            penX += (g->advance.x >> 6);
        }

        // Bounded cache; re-rasterize on eviction (UI strings are short)
        if (m_cache.size() > 256) m_cache.clear();
        m_cache.emplace(key, out);
        return out;
    }

private:
    static uint32_t packColor(const Color& c) {
        uint32_t r = (uint32_t)(c.r * 255.0f) & 0xFFu;
        uint32_t g = (uint32_t)(c.g * 255.0f) & 0xFFu;
        uint32_t b = (uint32_t)(c.b * 255.0f) & 0xFFu;
        uint32_t a = (uint32_t)(c.a * 255.0f) & 0xFFu;
        return (b << 24) | (g << 16) | (r << 8) | a;  // little-endian ARGB8888
    }

    FT_Library m_ft = nullptr;
    FT_Face m_face = nullptr;
    bool m_ok = false;
    std::unordered_map<TextKey, SceneText::Rasterized, TextKeyHash> m_cache;
};

// ============================================================================
// OverlayRenderer (scene-graph backed; C boundary via render_c.h)
// ============================================================================

OverlayRenderer::OverlayRenderer() = default;

OverlayRenderer::~OverlayRenderer() {
    shutdown();
}

void OverlayRenderer::setSceneTree(wlr_scene_tree_t* tree) {
    m_parent = tree;
}

bool OverlayRenderer::initialize() {
    if (m_initialized) return true;

    if (!m_parent) {
        fprintf(stderr, "[OverlayRenderer] No scene tree set (call setSceneTree)\n");
        return false;
    }

    m_frameTree = havel_scene_frame_tree_create(m_parent);
    if (!m_frameTree) {
        fprintf(stderr, "[OverlayRenderer] Failed to create frame tree\n");
        return false;
    }

    m_text = std::make_unique<SceneText>();
    if (!m_text->initialize()) {
        fprintf(stderr, "[OverlayRenderer] Warning: text unavailable; overlays render without text\n");
    }

    m_initialized = true;
    printf("[OverlayRenderer] Initialized (scene-graph backed)\n");
    return true;
}

void OverlayRenderer::shutdown() {
    // Nodes/buffers are owned by the scene graph; it tears down with the
    // compositor. Clear our handles only.
    havel_scene_frame_tree_destroy(&m_frameTree);
    m_text.reset();
    m_frameSceneBuffers.clear();
    m_initialized = false;
}

void OverlayRenderer::beginFrame(int screenWidth, int screenHeight) {
    if (!m_initialized) return;
    m_screenWidth = screenWidth;
    m_screenHeight = screenHeight;

    // Clear last frame's nodes (immediate-mode plugins redraw every frame)
    havel_scene_frame_tree_destroy(&m_frameTree);
    m_frameTree = havel_scene_frame_tree_create(m_parent);
    m_frameSceneBuffers.clear();
}

void OverlayRenderer::endFrame() {
    // Nodes stay in the tree until the next beginFrame.
}

void OverlayRenderer::drawRect(const FloatRect& rect, const Color& color) {
    drawRect(rect.x, rect.y, rect.w, rect.h, color);
}

void OverlayRenderer::drawRect(float x, float y, float w, float h, const Color& color) {
    if (!m_initialized) return;
    float col[4] = {color.r, color.g, color.b, color.a};
    havel_scene_rect(m_targetTree ? m_targetTree : m_frameTree,
                     (int)x, (int)y, (int)w, (int)h, col);
}

wlr_scene_tree_t* OverlayRenderer::createPersistentTree() {
    if (!m_initialized || !m_parent) return nullptr;
    return havel_scene_frame_tree_create(m_parent);
}

void OverlayRenderer::destroyPersistentTree(wlr_scene_tree_t** tree) {
    if (!tree) return;
    havel_scene_frame_tree_destroy(tree);
}

void OverlayRenderer::setTargetTree(wlr_scene_tree_t* tree) {
    m_targetTree = tree;
}

void OverlayRenderer::drawBorder(const FloatRect& rect, const Color& color, float thickness) {
    drawRect(rect.x, rect.y, rect.w, thickness, color);  // Top
    drawRect(rect.x, rect.y + rect.h - thickness, rect.w, thickness, color);  // Bottom
    drawRect(rect.x, rect.y, thickness, rect.h, color);  // Left
    drawRect(rect.x + rect.w - thickness, rect.y, thickness, rect.h, color);  // Right
}

void OverlayRenderer::drawCircle(float cx, float cy, float radius, const Color& color) {
    if (!m_initialized) return;
    const int segments = 32;
    const float PI = 3.14159265359f;
    float col[4] = {color.r, color.g, color.b, color.a};
    float lineWidth = 2.0f;
    for (int i = 0; i < segments; i++) {
        float a1 = 2.0f * PI * i / segments;
        float a2 = 2.0f * PI * (i + 1) / segments;
        float x1 = cx + radius * std::cos(a1);
        float y1 = cy + radius * std::sin(a1);
        float x2 = cx + radius * std::cos(a2);
        float y2 = cy + radius * std::sin(a2);
        float dx = x2 - x1, dy = y2 - y1;
        float dist = std::sqrt(dx * dx + dy * dy);
        if (dist < 0.01f) continue;
        havel_scene_rect(m_targetTree ? m_targetTree : m_frameTree,
                         (int)x1, (int)y1, (int)dist, (int)lineWidth, col);
    }
}

void OverlayRenderer::drawTexture(uint32_t texture, const FloatRect& rect, float alpha) {
    drawTexture(texture, rect.x, rect.y, rect.w, rect.h, alpha);
}

void OverlayRenderer::drawTexture(uint32_t texture, float x, float y, float w, float h, float alpha) {
    // GPU texture handles are never produced: the bridge's texture capture
    // returns 0 (no renderer access from the C layer) and AppIconLoader
    // stores CPU pixels. Callers use drawPixels / rect fallbacks.
    static int logged = 0;
    if (logged++ < 5) {
        LOG_WARN("[OverlayRenderer] drawTexture(0x%x) ignored: no GPU texture path (icons render via drawPixels)",
                 texture);
    }
    (void)alpha; (void)w; (void)h; (void)x; (void)y;
}

void OverlayRenderer::drawPixels(const uint32_t* pixels, int pw, int ph,
                                  float x, float y, float w, float h) {
    if (!m_initialized || !pixels || pw <= 0 || ph <= 0) return;
    wlr_scene_buffer_t* sb = havel_scene_pixels(
        m_frameTree, pixels, pw, ph, (int)x, (int)y, (int)w, (int)h);
    if (sb) m_frameSceneBuffers.push_back(sb);
}

void OverlayRenderer::drawText(const char* text, float x, float y, float size, const Color& color) {
    if (!m_initialized || !text || !m_text || !m_text->ok()) return;

    auto rz = m_text->rasterize(text, (uint32_t)(size + 0.5f), color);
    if (rz.pixels.empty()) return;
    wlr_scene_buffer_t* sb = havel_scene_pixels(
        m_targetTree ? m_targetTree : m_frameTree,
        rz.pixels.data(), rz.w, rz.h, (int)x, (int)y, rz.w, rz.h);
    if (sb && !m_targetTree) m_frameSceneBuffers.push_back(sb);
}

void OverlayRenderer::drawTextCentered(const char* text, float cx, float cy, float size, const Color& color) {
    if (!m_initialized || !text || !m_text || !m_text->ok()) return;

    auto rz = m_text->rasterize(text, (uint32_t)(size + 0.5f), color);
    if (rz.pixels.empty()) return;
    float x = cx - rz.w / 2.0f;
    float y = cy - rz.h / 2.0f;
    wlr_scene_buffer_t* sb = havel_scene_pixels(
        m_targetTree ? m_targetTree : m_frameTree,
        rz.pixels.data(), rz.w, rz.h, (int)x, (int)y, rz.w, rz.h);
    if (sb && !m_targetTree) m_frameSceneBuffers.push_back(sb);
}

} // namespace havel
