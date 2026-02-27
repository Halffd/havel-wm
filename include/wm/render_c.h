#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Opaque types - C++ never sees wlroots internals
typedef void wlr_scene_t;
typedef void wlr_output_t;
typedef void wlr_renderer_t;
typedef void wlr_scene_output_t;

// Render pipeline C interface
typedef struct havel_render_pipeline havel_render_pipeline_t;

// Create/destroy pipeline
havel_render_pipeline_t* havel_render_pipeline_create(wlr_output_t* output, wlr_renderer_t* renderer);
void havel_render_pipeline_destroy(havel_render_pipeline_t* pipeline);

// Render scene through pipeline
void havel_render_pipeline_render(havel_render_pipeline_t* pipeline, wlr_scene_t* scene, wlr_scene_output_t* scene_output);

// Effects
void havel_render_pipeline_add_effect(havel_render_pipeline_t* pipeline, const char* name);
void havel_render_pipeline_remove_effect(havel_render_pipeline_t* pipeline, const char* name);
void havel_render_pipeline_set_effects_enabled(havel_render_pipeline_t* pipeline, bool enabled);

// Properties
void havel_render_pipeline_set_zoom(havel_render_pipeline_t* pipeline, float zoom);
void havel_render_pipeline_set_gamma(havel_render_pipeline_t* pipeline, float gamma);
void havel_render_pipeline_set_brightness(havel_render_pipeline_t* pipeline, float brightness);
float havel_render_pipeline_get_zoom(havel_render_pipeline_t* pipeline);

// Overlay rendering
void havel_render_pipeline_draw_overlays(havel_render_pipeline_t* pipeline, int width, int height, void* pluginManager);
void havel_render_pipeline_set_overlay_renderer(havel_render_pipeline_t* pipeline, void* overlayRenderer);

// Overlay scene C interface
typedef struct havel_overlay_scene havel_overlay_scene_t;

havel_overlay_scene_t* havel_overlay_scene_create(wlr_scene_t* root_scene);
void havel_overlay_scene_destroy(havel_overlay_scene_t* overlay);

// Overlay types
typedef enum {
    HAVEL_OVERLAY_ALT_TAB,
    HAVEL_OVERLAY_OVERVIEW,
    HAVEL_OVERLAY_LAUNCHER,
    HAVEL_OVERLAY_DEBUG,
    HAVEL_OVERLAY_CUSTOM
} havel_overlay_type_t;

// Create/show/hide overlays
havel_overlay_scene_t* havel_overlay_create(havel_overlay_type_t type, int x, int y, int width, int height, bool centered);
void havel_overlay_show(havel_overlay_scene_t* overlay);
void havel_overlay_hide(havel_overlay_scene_t* overlay);
void havel_overlay_toggle(havel_overlay_scene_t* overlay);
void havel_overlay_destroy_wrapper(havel_overlay_scene_t* overlay);

// Check if any overlay is visible
bool havel_overlay_is_any_visible(void);

#ifdef __cplusplus
}
#endif
