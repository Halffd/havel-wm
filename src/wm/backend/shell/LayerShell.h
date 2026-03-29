// Layer Shell Handler - Manages layer shell surfaces
// Pure C implementation for performance

#pragma once

#include <wayland-server-core.h>
#include <wlr/types/wlr_layer_shell_v1.h>

#ifdef __cplusplus
extern "C" {
#endif

// Forward declarations
struct havel_wlr_server;

// Layer surface handle
typedef struct havel_layer_surface {
    struct wlr_layer_surface_v1 *layer_surface;
    struct wlr_scene_tree *scene_tree;
    struct havel_wlr_server *server;
    
    struct wl_listener destroy;
    struct wl_listener map;
    struct wl_listener unmap;
    struct wl_listener surface_commit;
    
    struct wl_list link;
} havel_layer_surface_t;

// Initialize layer shell
void havel_layer_shell_init(struct havel_wlr_server *server);

// Create layer surface
havel_layer_surface_t* havel_layer_surface_create(struct havel_wlr_server *server, struct wlr_layer_surface_v1 *surface);

// Destroy layer surface
void havel_layer_surface_destroy(havel_layer_surface_t *lsurface);

#ifdef __cplusplus
}
#endif
