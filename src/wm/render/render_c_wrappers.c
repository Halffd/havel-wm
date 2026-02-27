// Render Pipeline C Wrapper Implementation
// This file CAN include wlroots headers - it's the C boundary layer

#include <wm/render_c.h>

#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_output.h>
#include <wlr/render/wlr_renderer.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

// ============================================================================
// Render Pipeline Implementation
// ============================================================================

struct havel_render_pipeline {
    struct wlr_output* output;
    struct wlr_renderer* renderer;

    float zoom;
    float gamma;
    float brightness;
    bool effectsEnabled;
    
    // Opaque pointer to C++ OverlayRenderer
    void* overlayRenderer;

    // Future: FBO chain, effect shaders, etc.
};

havel_render_pipeline_t* havel_render_pipeline_create(wlr_output_t* output, wlr_renderer_t* renderer) {
    if (!output || !renderer) {
        return NULL;
    }
    
    struct wlr_output* wlr_out = (struct wlr_output*)output;
    struct wlr_renderer* wlr_rend = (struct wlr_renderer*)renderer;
    
    havel_render_pipeline_t* pipeline = calloc(1, sizeof(*pipeline));
    if (!pipeline) {
        return NULL;
    }
    
    pipeline->output = wlr_out;
    pipeline->renderer = wlr_rend;
    pipeline->zoom = 1.0f;
    pipeline->gamma = 1.0f;
    pipeline->brightness = 1.0f;
    pipeline->effectsEnabled = true;
    
    printf("[RenderPipeline] Created for output %s (%dx%d)\n", 
           wlr_out->name, wlr_out->width, wlr_out->height);
    
    return pipeline;
}

void havel_render_pipeline_destroy(havel_render_pipeline_t* pipeline) {
    if (pipeline) {
        free(pipeline);
    }
}

void havel_render_pipeline_render(havel_render_pipeline_t* pipeline, wlr_scene_t* scene, wlr_scene_output_t* scene_output) {
    if (!pipeline || !scene || !scene_output) {
        return;
    }
    
    struct wlr_scene_output* wlr_scene_out = (struct wlr_scene_output*)scene_output;
    
    // For now: commit scene directly
    // Future: render to FBO → apply effects → present
    
    wlr_scene_output_commit(wlr_scene_out, NULL);
}

void havel_render_pipeline_add_effect(havel_render_pipeline_t* pipeline, const char* name) {
    if (!pipeline || !name) return;
    printf("[RenderPipeline] Adding effect: %s\n", name);
    // Future: load shader, add to effect chain
}

void havel_render_pipeline_remove_effect(havel_render_pipeline_t* pipeline, const char* name) {
    if (!pipeline || !name) return;
    printf("[RenderPipeline] Removing effect: %s\n", name);
}

void havel_render_pipeline_set_effects_enabled(havel_render_pipeline_t* pipeline, bool enabled) {
    if (!pipeline) return;
    pipeline->effectsEnabled = enabled;
    printf("[RenderPipeline] Effects %s\n", enabled ? "enabled" : "disabled");
}

void havel_render_pipeline_set_zoom(havel_render_pipeline_t* pipeline, float zoom) {
    if (!pipeline) return;
    pipeline->zoom = zoom;
    // Future: apply zoom via shader or FBO crop
}

void havel_render_pipeline_set_gamma(havel_render_pipeline_t* pipeline, float gamma) {
    if (!pipeline) return;
    pipeline->gamma = gamma;
    // Future: apply gamma via shader
}

void havel_render_pipeline_set_brightness(havel_render_pipeline_t* pipeline, float brightness) {
    if (!pipeline) return;
    pipeline->brightness = brightness;
    // Future: apply brightness via shader
}

float havel_render_pipeline_get_zoom(havel_render_pipeline_t* pipeline) {
    return pipeline ? pipeline->zoom : 1.0f;
}

// ============================================================================
// Overlay Scene Implementation
// ============================================================================

struct havel_overlay_scene {
    struct wlr_scene_tree* tree;
    havel_overlay_type_t type;
    int x, y, width, height;
    bool visible;
};

havel_overlay_scene_t* havel_overlay_scene_create(wlr_scene_t* root_scene) {
    if (!root_scene) {
        return NULL;
    }
    
    struct wlr_scene* wlr_root = (struct wlr_scene*)root_scene;
    
    // Create overlay tree as child of root
    struct wlr_scene_tree* overlay_root = wlr_scene_tree_create(&wlr_root->tree);
    if (!overlay_root) {
        return NULL;
    }
    
    // Raise to top
    wlr_scene_node_raise_to_top(&overlay_root->node);
    
    havel_overlay_scene_t* overlay = calloc(1, sizeof(*overlay));
    if (!overlay) {
        return NULL;
    }
    
    overlay->tree = overlay_root;
    overlay->visible = true;
    
    printf("[Overlay] Created overlay scene tree\n");
    return overlay;
}

void havel_overlay_scene_destroy(havel_overlay_scene_t* overlay) {
    if (!overlay) return;
    
    if (overlay->tree) {
        wlr_scene_node_destroy(&overlay->tree->node);
    }
    
    free(overlay);
}

havel_overlay_scene_t* havel_overlay_create(havel_overlay_type_t type, int x, int y, int width, int height, bool centered) {
    // This creates an individual overlay, not the overlay root
    // For now, return NULL - overlays are managed by the overlay root
    // Individual overlays would be scene_rect or scene_buffer children
    (void)type;
    (void)x;
    (void)y;
    (void)width;
    (void)height;
    (void)centered;
    
    // Future: create scene_rect or scene_buffer as overlay child
    return NULL;
}

void havel_overlay_show(havel_overlay_scene_t* overlay) {
    if (!overlay || !overlay->tree) return;
    
    wlr_scene_node_set_enabled(&overlay->tree->node, true);
    overlay->visible = true;
    printf("[Overlay] Shown\n");
}

void havel_overlay_hide(havel_overlay_scene_t* overlay) {
    if (!overlay || !overlay->tree) return;
    
    wlr_scene_node_set_enabled(&overlay->tree->node, false);
    overlay->visible = false;
    printf("[Overlay] Hidden\n");
}

void havel_overlay_toggle(havel_overlay_scene_t* overlay) {
    if (!overlay) return;
    
    if (overlay->visible) {
        havel_overlay_hide(overlay);
    } else {
        havel_overlay_show(overlay);
    }
}

void havel_overlay_destroy_wrapper(havel_overlay_scene_t* overlay) {
    havel_overlay_scene_destroy(overlay);
}

bool havel_overlay_is_any_visible(void) {
    // Future: track all overlays and check visibility
    return false;
}

void havel_render_pipeline_set_overlay_renderer(havel_render_pipeline_t* pipeline, void* overlayRenderer) {
    if (!pipeline) return;
    pipeline->overlayRenderer = overlayRenderer;
}
