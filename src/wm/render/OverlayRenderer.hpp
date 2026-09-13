#pragma once

#include <wm/Types.hpp>
#include <cstdint>
#include <string>
#include <memory>
#include <vector>

// Opaque C-boundary types (see include/wm/render_c.h)
typedef void wlr_scene_tree_t;
typedef void wlr_scene_buffer_t;

namespace havel {

/**
 * Float-based rectangle for rendering
 */
struct FloatRect {
    float x, y, w, h;

    constexpr FloatRect(float x = 0, float y = 0, float w = 0, float h = 0)
        : x(x), y(y), w(w), h(h) {}

    // Convert from int Rect
    constexpr FloatRect(const Rect& r) : x((float)r.x), y((float)r.y), w((float)r.w), h((float)r.h) {}
};

class SceneText;  // CPU text rasterization + cache (defined in the .cpp)

/**
 * Scene-graph overlay renderer.
 *
 * Backs the plugin overlay API (drawRect/drawBorder/drawCircle/drawText/
 * drawTexture) with the wlroots scene graph: rectangles are wlr_scene_rect
 * nodes, text is FreeType-rasterized into a CPU wlr_buffer attached to a
 * wlr_scene_buffer. Everything composites through wlr_scene_output_build_state
 * by construction — no GL context is required or used.
 *
 * The previous implementation issued raw GLES2 calls from the frame handler,
 * outside any render pass; with wlroots 0.20 (which has no wlr_renderer_begin
 * and no wlr_egl_make_current) those calls could never execute against a
 * real context, so no plugin overlay ever rendered.
 *
 * Rendering order:
 * 1. beginFrame(width, height): resets the frame draw list
 * 2. plugins issue draw calls (nodes are created immediately)
 * 3. endFrame(): frame accounting only — nodes stay in the tree until the
 *    next beginFrame clears them (immediate-mode plugins redraw every frame)
 */
class OverlayRenderer {
public:
    OverlayRenderer();
    ~OverlayRenderer();

    // Attach to the overlay layer of the scene. Must be called before
    // initialize(). The renderer does not take ownership of the tree.
    void setSceneTree(wlr_scene_tree_t* tree);

    bool initialize();
    void shutdown();

    // Begin/end frame rendering
    void beginFrame(int screenWidth, int screenHeight);
    void endFrame();

    // Primitive drawing
    void drawRect(const FloatRect& rect, const Color& color);
    void drawRect(float x, float y, float w, float h, const Color& color);
    void drawBorder(const FloatRect& rect, const Color& color, float thickness = 2.0f);
    void drawCircle(float cx, float cy, float radius, const Color& color);

    // Texture drawing (not backed yet: texture capture always returned 0
    // and callers fall back to rectangles — logged, not silently dropped)
    void drawTexture(uint32_t texture, const FloatRect& rect, float alpha = 1.0f);
    void drawTexture(uint32_t texture, float x, float y, float w, float h, float alpha = 1.0f);

    // CPU pixel blit (ARGB8888 little-endian) — icon rendering path.
    // Creates a scene buffer sized to the pixel data and stretches it to
    // (x, y, w, h).
    void drawPixels(const uint32_t* pixels, int pw, int ph,
                    float x, float y, float w, float h);

    // Text rendering (FreeType rasterized to a CPU scene buffer, cached)
    void drawText(const char* text, float x, float y, float size, const Color& color);
    void drawTextCentered(const char* text, float cx, float cy, float size, const Color& color);

    // State
    bool isInitialized() const { return m_initialized; }
    int getScreenWidth() const { return m_screenWidth; }
    int getScreenHeight() const { return m_screenHeight; }

private:
    bool m_initialized = false;
    int m_screenWidth = 0;
    int m_screenHeight = 0;

    wlr_scene_tree_t* m_parent = nullptr;    // overlay layer (not owned)
    wlr_scene_tree_t* m_frameTree = nullptr; // per-frame node container (scene-owned)

    // Text cache: rasterized lines keyed by (text, size, color)
    std::unique_ptr<SceneText> m_text;
    // Scene buffers created this frame (cleared on next beginFrame)
    std::vector<wlr_scene_buffer_t*> m_frameSceneBuffers;
};

} // namespace havel
