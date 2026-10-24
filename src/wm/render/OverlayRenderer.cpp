#include "OverlayRenderer.hpp"
#include <wm/render_c.h>
#include <ui/font/Font.hpp>
#include <Logger.h>
#include <cstring>
#include <cstdio>
#include <cmath>
#include <algorithm>
#include <memory>
#include <vector>

namespace havel {

// ============================================================================
// Text rasterization: FreeType -> ARGB8888 line bitmap, cached per
// (text, pixel size, color). Rasterization is CPU-only; the resulting
// pixels go to the scene graph through the render_c boundary.
// ============================================================================

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

    m_font = std::make_unique<font::Font>();
    if (!m_font->initialize()) {
        fprintf(stderr, "[OverlayRenderer] Warning: text unavailable (%s); overlays render without text\n",
                m_font->reason().c_str());
    } else {
        fprintf(stderr, "[OverlayRenderer] Text: %s\n", m_font->describe().c_str());
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
    m_font.reset();
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
    // Honor the target tree like every other draw: pixels into a
    // persistent tree (zoom magnifier) must survive past this frame -
    // parenting to the frame tree made them vanish on the next beginFrame
    // while the surrounding rects persisted.
    wlr_scene_buffer_t* sb = havel_scene_pixels(
        m_targetTree ? m_targetTree : m_frameTree,
        pixels, pw, ph, (int)x, (int)y, (int)w, (int)h);
    if (sb && !m_targetTree) m_frameSceneBuffers.push_back(sb);
}

void OverlayRenderer::drawText(const char* text, float x, float y, float size, const Color& color) {
    if (!m_initialized || !text || !m_font || !m_font->ok()) return;

    auto rz = m_font->rasterize(text, (uint32_t)(size + 0.5f),
                                font::RGBA{color.r, color.g, color.b, color.a});
    if (rz.empty()) return;
    wlr_scene_buffer_t* sb = havel_scene_pixels(
        m_targetTree ? m_targetTree : m_frameTree,
        rz.pixels.data(), rz.w, rz.h, (int)x, (int)y, rz.w, rz.h);
    if (sb && !m_targetTree) m_frameSceneBuffers.push_back(sb);
}

void OverlayRenderer::drawTextCentered(const char* text, float cx, float cy, float size, const Color& color) {
    if (!m_initialized || !text || !m_font || !m_font->ok()) return;

    auto rz = m_font->rasterize(text, (uint32_t)(size + 0.5f),
                                font::RGBA{color.r, color.g, color.b, color.a});
    if (rz.empty()) return;
    float x = cx - rz.w / 2.0f;
    float y = cy - rz.h / 2.0f;
    wlr_scene_buffer_t* sb = havel_scene_pixels(
        m_targetTree ? m_targetTree : m_frameTree,
        rz.pixels.data(), rz.w, rz.h, (int)x, (int)y, rz.w, rz.h);
    if (sb && !m_targetTree) m_frameSceneBuffers.push_back(sb);
}


} // namespace havel
