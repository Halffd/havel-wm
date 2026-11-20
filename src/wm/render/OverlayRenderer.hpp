#pragma once

#include <wm/Types.hpp>
#include <cstdint>
#include <string>
#include <memory>
#include <vector>
#include <unordered_map>

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

// CPU text rasterization (TTF/OTF vector or PCF/BDF bitmap faces):
// ui/font/Font.hpp.
namespace font { class Font; }

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
    // outputIndex identifies the output being rendered. Each output keeps
    // its own frame tree: one shared tree was destroyed by the next
    // output's beginFrame before the first output committed, so with more
    // than one monitor NO overlay reached the screen (observed live on a
    // two-output headless setup). Pass -1 when there is no output context.
    void beginFrame(int screenWidth, int screenHeight, int outputIndex = -1);
    void endFrame();

    // Primitive drawing
    void drawRect(const FloatRect& rect, const Color& color);
    void drawRect(float x, float y, float w, float h, const Color& color);
    void drawBorder(const FloatRect& rect, const Color& color, float thickness = 2.0f);
    void drawCircle(float cx, float cy, float radius, const Color& color);

    // Persistent overlay subtree + target switch. State-static overlay
    // content (window decorations) must NOT redraw per frame: every
    // redraw re-damages the output and spins the render loop (observed
    // live: ~150 scene nodes per frame per window - shadows, 4x32-segment
    // corner circles, borders, title bars - keeping the single-threaded
    // loop CPU-bound and starving IPC and input). Persistent content
    // renders once per state change instead:
    //   tree = createPersistentTree(); setTargetTree(tree); <draws>;
    //   setTargetTree(nullptr);
    wlr_scene_tree_t* createPersistentTree();
    void destroyPersistentTree(wlr_scene_tree_t** tree);
    void setTargetTree(wlr_scene_tree_t* tree);  // nullptr = per-frame tree

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
    wlr_scene_tree_t* m_frameTree = nullptr; // frame tree of the output being drawn (scene-owned)
    // One frame tree per output index, so each output's pass only clears
    // its own container (see beginFrame).
    std::unordered_map<int, wlr_scene_tree_t*> m_frameTrees;
    wlr_scene_tree_t* m_targetTree = nullptr; // active draw target override (not owned)

    // The compositor's font (see ui/font).
    std::unique_ptr<font::Font> m_font;
    // Scene buffers created this frame (cleared on next beginFrame)
    std::vector<wlr_scene_buffer_t*> m_frameSceneBuffers;
};

} // namespace havel
