// Layer Shell Handler - Manages layer shell surfaces (waybar, notifications, etc.)
// Pure C implementation for performance

#include "LayerShell.h"
#include "../BackendTypes.h"
#include <Logger.h>
#include <stdlib.h>

// ============================================================================
// Layer Surface Handlers
// ============================================================================

static void layer_surface_handle_map(struct wl_listener *listener, void *data) {
    havel_layer_surface_t *lsurface = wl_container_of(listener, lsurface, map);
    LOG_INFO("[LAYER] Surface mapped: %p", (void*)lsurface);
    
    wlr_scene_node_set_enabled(&lsurface->scene_tree->node, true);
}

static void layer_surface_handle_unmap(struct wl_listener *listener, void *data) {
    havel_layer_surface_t *lsurface = wl_container_of(listener, lsurface, unmap);
    LOG_INFO("[LAYER] Surface unmapped: %p", (void*)lsurface);
    
    wlr_scene_node_set_enabled(&lsurface->scene_tree->node, false);
}

static void layer_surface_handle_commit(struct wl_listener *listener, void *data) {
    havel_layer_surface_t *lsurface = wl_container_of(listener, lsurface, surface_commit);
    // Handle layer surface commits if needed
    (void)lsurface;
}

static void layer_surface_handle_destroy(struct wl_listener *listener, void *data) {
    havel_layer_surface_t *lsurface = wl_container_of(listener, lsurface, destroy);
    LOG_INFO("[LAYER] Surface destroyed: %p", (void*)lsurface);
    
    wl_list_remove(&lsurface->destroy.link);
    wl_list_remove(&lsurface->map.link);
    wl_list_remove(&lsurface->unmap.link);
    wl_list_remove(&lsurface->surface_commit.link);
    
    free(lsurface);
}

// ============================================================================
// Layer Surface Creation
// ============================================================================

havel_layer_surface_t* havel_layer_surface_create(struct havel_wlr_server *server, struct wlr_layer_surface_v1 *surface) {
    LOG_INFO("[LAYER] New layer surface: %p", (void*)surface);
    
    // Allocate layer surface structure
    havel_layer_surface_t *lsurface = calloc(1, sizeof(havel_layer_surface_t));
    if (!lsurface) {
        LOG_ERROR("[LAYER] Failed to allocate layer surface");
        return NULL;
    }
    
    lsurface->server = server;
    lsurface->layer_surface = surface;
    surface->data = lsurface;
    
    // Create scene tree in overlay layer
    lsurface->scene_tree = wlr_scene_tree_create(server->overlay_layer);
    if (!lsurface->scene_tree) {
        LOG_ERROR("[LAYER] Failed to create scene tree");
        free(lsurface);
        return NULL;
    }
    
    // Setup listeners
    lsurface->destroy.notify = layer_surface_handle_destroy;
    wl_signal_add(&surface->events.destroy, &lsurface->destroy);
    
    lsurface->map.notify = layer_surface_handle_map;
    wl_signal_add(&surface->surface->events.map, &lsurface->map);
    
    lsurface->unmap.notify = layer_surface_handle_unmap;
    wl_signal_add(&surface->surface->events.unmap, &lsurface->unmap);
    
    lsurface->surface_commit.notify = layer_surface_handle_commit;
    wl_signal_add(&surface->surface->events.commit, &lsurface->surface_commit);
    
    // Configure the layer surface
    surface->surface->current.scale = surface->output->scale;
    wlr_surface_send_enter(surface->surface, surface->output);
    
    LOG_INFO("[LAYER] Layer surface created: %p", (void*)lsurface);
    
    return lsurface;
}

void havel_layer_surface_destroy(havel_layer_surface_t *lsurface) {
    // Handler will be called by wlroots
}

// ============================================================================
// Layer Shell Initialization
// ============================================================================

static void handle_new_layer_surface(struct wl_listener *listener, void *data) {
    struct havel_wlr_server *server = wl_container_of(listener, server, new_layer_surface);
    struct wlr_layer_surface_v1 *surface = data;
    
    havel_layer_surface_create(server, surface);
}

void havel_layer_shell_init(struct havel_wlr_server *server) {
    server->layer_shell = wlr_layer_shell_v1_create(server->display, 4);
    if (!server->layer_shell) {
        LOG_ERROR("[LAYER] Failed to create layer shell");
        return;
    }
    
    server->new_layer_surface.notify = handle_new_layer_surface;
    wl_signal_add(&server->layer_shell->events.new_surface, &server->new_layer_surface);
    
    LOG_INFO("[LAYER] Layer shell initialized");
}
