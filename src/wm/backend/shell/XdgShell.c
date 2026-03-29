// XDG Shell Handler - Manages XDG toplevel surfaces
// Pure C implementation for performance

#include "XdgShell.h"
#include "../BackendTypes.h"
#include <Logger.h>
#include <stdlib.h>

// ============================================================================
// XDG View Handlers
// ============================================================================

static void xdg_view_handle_map(struct wl_listener *listener, void *data) {
    havel_xdg_view_t *view = wl_container_of(listener, view, map);
    
    LOG_INFO("[XDG] MAP: %p (xdg_surface=%p, scene_tree=%p)",
             (void*)view, (void*)view->xdg_surface, (void*)view->scene_tree);
    
    // Raise to top and enable
    wlr_scene_node_raise_to_top(&view->scene_tree->node);
    wlr_scene_node_set_enabled(&view->scene_tree->node, true);
    
    // Position window (center on output)
    // This would be handled by window manager in production
}

static void xdg_view_handle_unmap(struct wl_listener *listener, void *data) {
    havel_xdg_view_t *view = wl_container_of(listener, view, unmap);
    LOG_INFO("[XDG] UNMAP: %p", (void*)view);
}

static void xdg_view_handle_destroy(struct wl_listener *listener, void *data) {
    havel_xdg_view_t *view = wl_container_of(listener, view, destroy);
    
    LOG_INFO("[XDG] DESTROY: %p", (void*)view);
    
    // Remove all listeners
    wl_list_remove(&view->map.link);
    wl_list_remove(&view->unmap.link);
    wl_list_remove(&view->destroy.link);
    wl_list_remove(&view->surface_commit.link);
    wl_list_remove(&view->set_app_id.link);
    wl_list_remove(&view->set_title.link);
    wl_list_remove(&view->request_move.link);
    wl_list_remove(&view->request_resize.link);
    wl_list_remove(&view->request_minimize.link);
    wl_list_remove(&view->request_maximize.link);
    wl_list_remove(&view->request_fullscreen.link);
    
    // Scene tree is auto-destroyed by wlroots
    
    free(view);
}

static void xdg_surface_handle_commit(struct wl_listener *listener, void *data) {
    havel_xdg_view_t *view = wl_container_of(listener, view, surface_commit);
    
    // Remove this one-time listener
    wl_list_remove(&view->surface_commit.link);
    view->surface_commit.notify = NULL;
    
    // Send configure with initial size
    if (view->xdg_surface->toplevel) {
        LOG_INFO("[XDG] First commit - sending configure");
        wlr_xdg_toplevel_set_size(view->xdg_surface->toplevel, 800, 600);
        wlr_xdg_toplevel_set_activated(view->xdg_surface->toplevel, true);
    }
}

// Request handlers
static void xdg_handle_request_move(struct wl_listener *listener, void *data) {
    havel_xdg_view_t *view = wl_container_of(listener, view, request_move);
    LOG_INFO("[XDG] Move request: %p", (void*)view);
    // Would start interactive move
}

static void xdg_handle_request_resize(struct wl_listener *listener, void *data) {
    havel_xdg_view_t *view = wl_container_of(listener, view, request_resize);
    struct wlr_xdg_toplevel_resize_event *event = data;
    LOG_INFO("[XDG] Resize request: %p (edges: %d)", (void*)view, event->edges);
    // Would start interactive resize
}

static void xdg_handle_request_minimize(struct wl_listener *listener, void *data) {
    havel_xdg_view_t *view = wl_container_of(listener, view, request_minimize);
    LOG_INFO("[XDG] Minimize request: %p", (void*)view);
}

static void xdg_handle_request_maximize(struct wl_listener *listener, void *data) {
    havel_xdg_view_t *view = wl_container_of(listener, view, request_maximize);
    
    if (!view || !view->xdg_surface->toplevel) return;
    
    LOG_INFO("[XDG] Maximize request: %p", (void*)view);
    
    struct wlr_xdg_toplevel *toplevel = view->xdg_surface->toplevel;
    bool maximized = toplevel->current.maximized;
    wlr_xdg_toplevel_set_maximized(toplevel, !maximized);
}

static void xdg_handle_request_fullscreen(struct wl_listener *listener, void *data) {
    havel_xdg_view_t *view = wl_container_of(listener, view, request_fullscreen);
    
    if (!view || !view->xdg_surface->toplevel) return;
    
    LOG_INFO("[XDG] Fullscreen request: %p", (void*)view);
    
    struct wlr_xdg_toplevel *toplevel = view->xdg_surface->toplevel;
    bool fullscreen = toplevel->current.fullscreen;
    wlr_xdg_toplevel_set_fullscreen(toplevel, !fullscreen);
}

static void xdg_handle_set_app_id(struct wl_listener *listener, void *data) {
    havel_xdg_view_t *view = wl_container_of(listener, view, set_app_id);
    
    if (!view || !view->xdg_surface->toplevel) return;
    
    const char *app_id = view->xdg_surface->toplevel->app_id;
    LOG_INFO("[XDG] App ID set: %s", app_id ? app_id : "(null)");
}

static void xdg_handle_set_title(struct wl_listener *listener, void *data) {
    havel_xdg_view_t *view = wl_container_of(listener, view, set_title);
    
    if (!view || !view->xdg_surface->toplevel) return;
    
    const char *title = view->xdg_surface->toplevel->title;
    LOG_INFO("[XDG] Title set: %s", title ? title : "(null)");
}

// ============================================================================
// XDG View Creation
// ============================================================================

havel_xdg_view_t* havel_xdg_view_create(struct havel_wlr_server *server, struct wlr_xdg_toplevel *toplevel) {
    struct wlr_xdg_surface *xdg_surface = toplevel->base;
    
    LOG_INFO("[XDG] New toplevel: %p (surface=%p)", (void*)toplevel, (void*)xdg_surface);
    
    // Allocate view structure
    havel_xdg_view_t *view = calloc(1, sizeof(havel_xdg_view_t));
    if (!view) {
        LOG_ERROR("[XDG] Failed to allocate view");
        return NULL;
    }
    
    view->server = server;
    view->xdg_surface = xdg_surface;
    xdg_surface->data = view;
    
    // Create scene tree in active workspace
    struct wlr_scene_tree *parent = server->workspaces[server->active_workspace];
    if (!parent) parent = &server->scene->tree;
    
    view->scene_tree = wlr_scene_xdg_surface_create(parent, xdg_surface);
    if (!view->scene_tree) {
        LOG_ERROR("[XDG] Failed to create scene tree");
        free(view);
        return NULL;
    }
    
    LOG_INFO("[XDG] Scene tree created: %p", (void*)view->scene_tree);
    
    // Setup listeners - MUST be before any events can fire
    view->map.notify = xdg_view_handle_map;
    wl_signal_add(&xdg_surface->surface->events.map, &view->map);
    
    view->unmap.notify = xdg_view_handle_unmap;
    wl_signal_add(&xdg_surface->surface->events.unmap, &view->unmap);
    
    view->destroy.notify = xdg_view_handle_destroy;
    wl_signal_add(&xdg_surface->events.destroy, &view->destroy);
    
    view->surface_commit.notify = xdg_surface_handle_commit;
    wl_signal_add(&xdg_surface->surface->events.commit, &view->surface_commit);
    
    // Window metadata listeners
    view->set_app_id.notify = xdg_handle_set_app_id;
    wl_signal_add(&toplevel->events.set_app_id, &view->set_app_id);
    
    view->set_title.notify = xdg_handle_set_title;
    wl_signal_add(&toplevel->events.set_title, &view->set_title);
    
    // Window management requests
    view->request_move.notify = xdg_handle_request_move;
    wl_signal_add(&toplevel->events.request_move, &view->request_move);
    
    view->request_resize.notify = xdg_handle_request_resize;
    wl_signal_add(&toplevel->events.request_resize, &view->request_resize);
    
    view->request_minimize.notify = xdg_handle_request_minimize;
    wl_signal_add(&toplevel->events.request_minimize, &view->request_minimize);
    
    view->request_maximize.notify = xdg_handle_request_maximize;
    wl_signal_add(&toplevel->events.request_maximize, &view->request_maximize);
    
    view->request_fullscreen.notify = xdg_handle_request_fullscreen;
    wl_signal_add(&toplevel->events.request_fullscreen, &view->request_fullscreen);
    
    return view;
}

void havel_xdg_view_destroy(havel_xdg_view_t *view) {
    // Handler will be called by wlroots
}

// ============================================================================
// XDG Shell Initialization
// ============================================================================

static void handle_new_xdg_toplevel(struct wl_listener *listener, void *data) {
    struct havel_wlr_server *server = wl_container_of(listener, server, new_xdg_toplevel);
    struct wlr_xdg_toplevel *toplevel = data;
    
    havel_xdg_view_create(server, toplevel);
}

void havel_xdg_shell_init(struct havel_wlr_server *server) {
    server->xdg_shell = wlr_xdg_shell_create(server->display, 6);
    if (!server->xdg_shell) {
        LOG_ERROR("[XDG] Failed to create XDG shell");
        return;
    }
    
    server->new_xdg_toplevel.notify = handle_new_xdg_toplevel;
    wl_signal_add(&server->xdg_shell->events.new_toplevel, &server->new_xdg_toplevel);
    
    LOG_INFO("[XDG] XDG shell initialized");
}
