#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// Scene-graph drawing boundary for the C++ OverlayRenderer.
// C++ never includes wlroots headers directly (they are C99-only).
// ============================================================================

typedef void wlr_scene_tree_t;
typedef void wlr_scene_rect_t;
typedef void wlr_scene_buffer_t;

// Create/destroy the per-frame container under a parent tree.
// destroy also removes all child nodes created under it.
wlr_scene_tree_t* havel_scene_frame_tree_create(wlr_scene_tree_t* parent);
void havel_scene_frame_tree_destroy(wlr_scene_tree_t** tree);

// Solid rectangle (color is float[4] RGBA)
wlr_scene_rect_t* havel_scene_rect(wlr_scene_tree_t* parent, int x, int y,
                                   int width, int height, const float color[4]);

// CPU pixel buffer (ARGB8888 little-endian, pixels copied) drawn at
// (x, y) stretched to (width, height); pw/ph are the source dimensions.
wlr_scene_buffer_t* havel_scene_pixels(wlr_scene_tree_t* parent,
                                       const uint32_t* pixels, int pw, int ph,
                                       int x, int y, int width, int height);

// Text rasterization (CPU, ARGB8888) for C-side overlays. 0 = not
// renderable (no font yet). Free with havel_text_pixels_free.
int havel_text_rasterize(const char* text, int px, const float color[4],
                         uint32_t** out_pixels, int* out_w, int* out_h);
void havel_text_pixels_free(uint32_t* pixels);

// Convenience: rasterize and attach in one step (dest size = raster size).
wlr_scene_buffer_t* havel_scene_text(wlr_scene_tree_t* parent, const char* text,
                                     int x, int y, int px, const float color[4]);

#ifdef __cplusplus
}
#endif
