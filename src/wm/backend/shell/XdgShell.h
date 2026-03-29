// XDG Shell Handler - Manages XDG toplevel surfaces
// Pure C implementation for performance

#pragma once

#include <wayland-server-core.h>
#include <wlr/types/wlr_xdg_shell.h>

#ifdef __cplusplus
extern "C" {
#endif

// Forward declarations
struct havel_wlr_server;

// XDG view handle
typedef struct havel_xdg_view {
    struct wlr_xdg_surface *xdg_surface;
    struct wlr_scene_tree *scene_tree;
    struct havel_wlr_server *server;
    
    void *cpp_view;         // C++ View*
    void *scene_graph_view; // SceneView*
    
    struct wl_listener map;
    struct wl_listener unmap;
    struct wl_listener destroy;
    struct wl_listener surface_commit;
    struct wl_listener set_app_id;
    struct wl_listener set_title;
    struct wl_listener request_move;
    struct wl_listener request_resize;
    struct wl_listener request_minimize;
    struct wl_listener request_maximize;
    struct wl_listener request_fullscreen;
    
    struct wl_list link;
} havel_xdg_view_t;

// Initialize XDG shell
void havel_xdg_shell_init(struct havel_wlr_server *server);

// Create XDG view from toplevel
havel_xdg_view_t* havel_xdg_view_create(struct havel_wlr_server *server, struct wlr_xdg_toplevel *toplevel);

// Destroy XDG view
void havel_xdg_view_destroy(havel_xdg_view_t *view);

#ifdef __cplusplus
}
#endif
