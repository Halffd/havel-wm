// View Transform Implementation (for scale/overview effects)

#include <wm/view_transform.h>
#include <cstdio>

// Forward declare from wlr_bridge.c - don't include wlroots headers in C++
struct havel_xdg_view {
    void* xdg_surface;
    void* scene_tree;  // wlr_scene_tree* - opaque in C++
    void* server;
    int x, y;
};

// Store active transforms per view
static struct {
    void* view;
    havel_view_transform_t transform;
} g_active_transforms[256];
static int g_transform_count = 0;

static havel_view_transform_t* find_transform(void* view) {
    for (int i = 0; i < g_transform_count; i++) {
        if (g_active_transforms[i].view == view) {
            return &g_active_transforms[i].transform;
        }
    }
    return NULL;
}

static void remove_transform_entry(void* view) {
    for (int i = 0; i < g_transform_count; i++) {
        if (g_active_transforms[i].view == view) {
            g_active_transforms[i] = g_active_transforms[g_transform_count - 1];
            g_transform_count--;
            return;
        }
    }
}

void havel_cpp_apply_view_transform(void* view, const havel_view_transform_t* transform) {
    if (!view || !transform) return;
    
    // Store transform for rendering pipeline to use
    havel_view_transform_t* existing = find_transform(view);
    if (existing) {
        *existing = *transform;
    } else if (g_transform_count < 256) {
        g_active_transforms[g_transform_count].view = view;
        g_active_transforms[g_transform_count].transform = *transform;
        g_transform_count++;
    }
    
    // Note: wlroots 0.20 scene doesn't have per-node opacity/scale
    // These transforms are used by the render pipeline for post-processing
    printf("[ViewTransform] Applied transform alpha=%.2f\n", transform->alpha);
}

void havel_cpp_remove_view_transform(void* view) {
    if (!view) return;
    
    remove_transform_entry(view);
    printf("[ViewTransform] Removed transform\n");
}

havel_view_transform_t havel_cpp_get_view_transform(void* view) {
    havel_view_transform_t default_transform = {
        .scale_x = 1.0f, .scale_y = 1.0f,
        .translation_x = 0, .translation_y = 0,
        .alpha = 1.0f, .active = false
    };
    
    if (!view) return default_transform;
    
    havel_view_transform_t* existing = find_transform(view);
    if (existing) {
        existing->active = true;
        return *existing;
    }
    
    return default_transform;
}
