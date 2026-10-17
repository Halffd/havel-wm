#include "OverlayRenderer.hpp"
#include <wm/render_c.h>
#include <Logger.h>
#include <ft2build.h>
#include <freetype/freetype.h>
#include <cstring>
#include <cstdio>
#include <cmath>
#include <cstdlib>
#include <algorithm>
#include <filesystem>
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

    // Find a usable TTF. The old code listed five hardcoded paths and
    // silently gave up when none existed - on a box with only Adwaita,
    // Liberation and Noto that meant EVERY overlay rendered without text:
    // the workspace bar was a blank black strip and the FPS/usage plugins
    // were unlabelled squares (user report: "they dont tell anything have
    // no text or info, or numbmers").
    //
    // Order: explicit override, then well-known families by preference,
    // then any TTF in the usual font directories.
    static std::string findFontFile() {
        if (const char* env = getenv("HAVEL_FONT")) {
            if (*env && std::filesystem::is_regular_file(env)) return env;
        }

        static const char* dirs[] = {
            "/usr/share/fonts",
            "/usr/local/share/fonts",
            "/run/host/fonts",
            nullptr
        };
        static const char* preferred[] = {
            "DejaVuSans.ttf",
            "LiberationSans-Regular.ttf",
            "AdwaitaSans-Regular.ttf",
            "NotoSans-Regular.ttf",
            "FreeSans.ttf",
            "Roboto-Regular.ttf",
            nullptr
        };

        std::vector<std::string> ttf;
        std::error_code ec;
        for (int d = 0; dirs[d]; d++) {
            if (!std::filesystem::is_directory(dirs[d], ec)) continue;
            // A whole-tree walk is fine here: it runs once at startup.
            for (auto it = std::filesystem::recursive_directory_iterator(
                     dirs[d], std::filesystem::directory_options::skip_permission_denied, ec);
                 !ec && it != std::filesystem::recursive_directory_iterator(); it.increment(ec)) {
                if (!it->is_regular_file(ec)) continue;
                const std::string p = it->path().string();
                if (p.size() > 4 && p.compare(p.size() - 4, 4, ".ttf") == 0) {
                    ttf.push_back(p);
                }
            }
            ec.clear();
        }
        for (int p = 0; preferred[p]; p++) {
            for (const auto& f : ttf) {
                if (f.size() >= strlen(preferred[p]) &&
                    f.compare(f.size() - strlen(preferred[p]), strlen(preferred[p]),
                              preferred[p]) == 0) {
                    return f;
                }
            }
        }
        if (!ttf.empty()) return ttf.front();
        return {};
    }

    bool initialize() {
        if (FT_Init_FreeType(&m_ft)) {
            fprintf(stderr, "[SceneText] FreeType init failed\n");
            return false;
        }
        const std::string font = findFontFile();
        if (font.empty()) {
            fprintf(stderr, "[SceneText] No TTF font found - overlays will render without text. "
                            "Set HAVEL_FONT=<path.ttf> to choose one explicitly.\n");
            return false;
        }
        if (FT_New_Face(m_ft, font.c_str(), 0, &m_face) != 0) {
            fprintf(stderr, "[SceneText] Failed to load font %s\n", font.c_str());
            return false;
        }
        fprintf(stderr, "[SceneText] Using font %s\n", font.c_str());
        m_ok = true;
        return true;
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
    m_frameTrees[-1] = m_frameTree;
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
    for (auto& entry : m_frameTrees) {
        havel_scene_frame_tree_destroy(&entry.second);
    }
    m_frameTrees.clear();
    m_frameTree = nullptr;
    m_text.reset();
    m_frameSceneBuffers.clear();
    m_initialized = false;
}

void OverlayRenderer::beginFrame(int screenWidth, int screenHeight, int outputIndex) {
    if (!m_initialized) return;
    m_screenWidth = screenWidth;
    m_screenHeight = screenHeight;

    // Clear only THIS output's last frame (immediate-mode plugins redraw
    // every frame). A single shared tree was cleared by every output's
    // pass, so on a multi-monitor setup each output committed a tree the
    // other pass had already emptied - no overlay ever reached the
    // screen. One tree per output index fixes that and keeps memory
    // bounded.
    wlr_scene_tree_t** slot = &m_frameTrees[outputIndex];
    havel_scene_frame_tree_destroy(slot);
    *slot = havel_scene_frame_tree_create(m_parent);
    m_frameTree = *slot;
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

// C-callable text rasterization, for overlays that live on the C side of
// the bridge (the boot splash) and cannot reach OverlayRenderer. Uses its
// own SceneText instance so it works before any renderer exists; the
// font search logs which file it picked, same as the renderer's.
extern "C" int havel_text_rasterize(const char* text, int px, const float color[4],
                                     uint32_t** out_pixels, int* out_w, int* out_h) {
    using namespace havel;
    if (!text || !out_pixels || !out_w || !out_h || px <= 0) return 0;
    static SceneText st;
    static bool tried = false;
    if (!tried) {
        tried = true;
        st.initialize();
    }
    if (!st.ok()) return 0;

    SceneText::Rasterized rz = st.rasterize(
        text, (uint32_t)px, Color(color[0], color[1], color[2], color[3]));
    if (rz.pixels.empty()) return 0;

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

} // namespace havel
