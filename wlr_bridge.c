// Enable unstable wlroots features (layer-shell, etc.)
#define WLR_USE_UNSTABLE

#include <wm/wlr_bridge.h>
#include <Logger.h>
#include <wm/render_c.h>

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <linux/input-event-codes.h>

#include <wayland-server-core.h>

#include <wlr/backend.h>
#include <wlr/render/allocator.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_data_device.h>
#include <wlr/types/wlr_input_device.h>
#include <wlr/types/wlr_keyboard.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_pointer.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_subcompositor.h>
#include <wlr/types/wlr_xcursor_manager.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/types/wlr_layer_shell_v1.h>
#include <wlr/types/wlr_xdg_output_v1.h>
#include <wlr/types/wlr_server_decoration.h>
#include <wlr/types/wlr_xdg_activation_v1.h>
#include <wlr/types/wlr_primary_selection_v1.h>
#include <wlr/types/wlr_screencopy_v1.h>
#include <wlr/util/log.h>
#include <wlr/xwayland.h>

#include <xkbcommon/xkbcommon.h>
#include <xkbcommon/xkbcommon-keysyms.h>

// Gamma control v1 protocol
#include <wlr/types/wlr_gamma_control_v1.h>

// Session/VT switching
#include <wlr/backend/session.h>

#define HAVEL_WORKSPACE_COUNT 10

// ============================================================================
// C++ → C Callback Storage
// ============================================================================

// Global running server pointer (for quit functionality)
struct havel_wlr_server *g_running_server = NULL;

static cpp_view_set_position_fn g_view_set_position = NULL;
static cpp_view_set_size_fn g_view_set_size = NULL;
static cpp_view_focus_fn g_view_focus = NULL;
static cpp_view_raise_fn g_view_raise = NULL;
static cpp_view_get_geometry_fn g_view_get_geometry = NULL;
static cpp_view_close_fn g_view_close = NULL;
static cpp_view_set_fullscreen_fn g_view_set_fullscreen = NULL;
static cpp_view_minimize_fn g_view_minimize = NULL;
static cpp_workspace_arrange_fn g_workspace_arrange = NULL;
static cpp_workspace_set_active_fn g_workspace_set_active = NULL;
static cpp_server_quit_fn g_server_quit = NULL;
static cpp_server_spawn_fn g_server_spawn = NULL;

void havel_cpp_register_view_callbacks(
    cpp_view_set_position_fn set_position,
    cpp_view_set_size_fn set_size,
    cpp_view_focus_fn focus,
    cpp_view_raise_fn raise,
    cpp_view_get_geometry_fn get_geometry,
    cpp_view_close_fn close,
    cpp_view_set_fullscreen_fn set_fullscreen,
    cpp_view_minimize_fn minimize
) {
    g_view_set_position = set_position;
    g_view_set_size = set_size;
    g_view_focus = focus;
    g_view_raise = raise;
    g_view_get_geometry = get_geometry;
    g_view_close = close;
    g_view_set_fullscreen = set_fullscreen;
    g_view_minimize = minimize;
}

void havel_cpp_register_workspace_callbacks(
    cpp_workspace_arrange_fn arrange,
    cpp_workspace_set_active_fn set_active
) {
    g_workspace_arrange = arrange;
    g_workspace_set_active = set_active;
}

void havel_cpp_register_server_callbacks(
    cpp_server_quit_fn quit,
    cpp_server_spawn_fn spawn
) {
    g_server_quit = quit;
    g_server_spawn = spawn;
}

// Forward declarations
struct havel_output;
struct havel_keyboard;
struct havel_xdg_view;
struct havel_xwayland_view;

// Global server pointer for overlay access
static struct havel_wlr_server *server = NULL;

struct havel_wlr_server {
    struct wl_display *display;
    struct wlr_backend *backend;
    struct wlr_renderer *renderer;
    struct wlr_allocator *allocator;

    struct wlr_compositor *compositor;
    struct wlr_output_layout *output_layout;
    struct wlr_scene *scene;

    struct wlr_xdg_shell *xdg_shell;
    struct wl_listener new_xdg_toplevel;
    struct wl_listener new_xdg_surface;

    // Overlay scene layer (for Alt-Tab, Overview, etc.)
    struct wlr_scene_tree *overlay_layer;

    struct wlr_xwayland *xwayland;
    struct wl_listener new_xwayland_surface;

    // Layer-shell v1 (for waybar, notifications, etc.)
    struct wlr_layer_shell_v1 *layer_shell;
    struct wl_listener new_layer_surface;

    struct wlr_seat *seat;
    struct wlr_cursor *cursor;
    struct wlr_xcursor_manager *cursor_mgr;

    struct wl_listener new_output;
    struct wl_listener new_input;

    struct wl_listener cursor_motion;
    struct wl_listener cursor_motion_absolute;
    struct wl_listener cursor_button;
    struct wl_listener cursor_axis;
    struct wl_listener cursor_frame;

    uint32_t active_workspace;
    struct wl_list outputs; // havel_output::link

    // Global workspace trees (shared across all outputs)
    struct wlr_scene_tree *workspaces[HAVEL_WORKSPACE_COUNT];

    // Gamma control v1 manager
    struct wlr_gamma_control_manager_v1 *gamma_control_manager;

    // Session for VT switching
    struct wlr_session *session;

    // Interactive move/resize state
    struct {
        struct havel_xdg_view *view;
        double start_x, start_y;      // Cursor position at grab start
        int view_start_x, view_start_y; // View position at grab start
        int view_start_w, view_start_h; // View size at grab start (for resize)
        enum {
            INTERACTIVE_NONE = 0,
            INTERACTIVE_MOVE,
            INTERACTIVE_RESIZE,
        } mode;
        uint32_t edges;  // Resize edges
    } grab;

    // C++ server handle - owns WM policy/state
    struct havel_cpp_server *cpp_server;
};

struct havel_output {
    struct wlr_output *output;
    struct wlr_scene_output *scene_output;
    struct wl_listener frame;
    struct wl_listener destroy;

    struct havel_wlr_server *server;
    bool is_primary;
    struct wl_list link;

    // Gamma control v1 manager
    struct wlr_gamma_control_manager_v1 *gamma_control_manager;

    // Gamma/temperature/brightness state
    float gamma;
    int temperature;
    float brightness;

    // Gamma LUT buffers (allocated once per output)
    uint16_t *gamma_ramp_red;
    uint16_t *gamma_ramp_green;
    uint16_t *gamma_ramp_blue;
    size_t gamma_ramp_size;
    bool gamma_ramp_dirty;  // Flag to indicate LUT needs regeneration
};

struct havel_keyboard {
    struct wlr_keyboard *keyboard;
    struct wl_listener modifiers;
    struct wl_listener key;
    struct wl_listener destroy;

    struct havel_wlr_server *server;
    
    // XKB state for keysym lookup (international keyboard support)
    struct xkb_state *xkb_state;
    struct xkb_keymap *keymap;
};

struct havel_xdg_view {
    struct wlr_xdg_surface *xdg_surface;
    struct wlr_scene_tree *scene_tree;
    struct havel_wlr_server *server;

    // NO workspace_id - C++ owns this
    // NO mapped flag - C++ owns this
    // NO geometry - C++ owns this

    // Only wlroots handles and C++ opaque pointer
    void *cpp_view;  // Opaque pointer to C++ View object

    struct wl_listener map;
    struct wl_listener unmap;
    struct wl_listener destroy;
    struct wl_listener surface_commit;  // Waits for first commit before sending configure
    struct wl_listener set_app_id;      // Called when app_id is set
    struct wl_listener set_title;       // Called when title is set
};

struct havel_xwayland_view {
    struct wlr_xwayland_surface *xsurface;
    struct wlr_scene_tree *scene_tree;
    struct havel_wlr_server *server;

    // NO workspace_id - C++ owns this
    // NO geometry - C++ owns this
    
    void *cpp_view;  // Opaque pointer to C++ View object

    struct wl_listener destroy;
};

// Forward function declarations
static void xdg_view_set_position(struct havel_xdg_view *view, int x, int y);
static void xwayland_view_set_position(struct havel_xwayland_view *view, int x, int y);

// ============================================================================
// Callback Implementations (C++ → C)
// ============================================================================

static void cpp_impl_view_set_position(void* view, int x, int y) {
    if (!view) return;
    // View could be either XDG or XWayland - try XDG first
    struct havel_xdg_view *xdg_view = (struct havel_xdg_view*)view;
    if (xdg_view->scene_tree) {
        xdg_view_set_position(xdg_view, x, y);
    }
}

static void cpp_impl_view_set_size(void* view, int w, int h) {
    if (!view) return;
    struct havel_xdg_view *xdg_view = (struct havel_xdg_view*)view;
    if (xdg_view->xdg_surface && xdg_view->xdg_surface->toplevel) {
        wlr_xdg_toplevel_set_size(xdg_view->xdg_surface->toplevel, w, h);
    }
}

static void cpp_impl_view_focus(void* view) {
    LOG_INFO("[C] cpp_impl_view_focus: %p", view);
    if (!view) return;
    struct havel_xdg_view *xdg_view = (struct havel_xdg_view*)view;
    if (xdg_view->xdg_surface && xdg_view->xdg_surface->surface) {
        struct wlr_seat *seat = xdg_view->server->seat;
        struct wlr_keyboard *keyboard = wlr_seat_get_keyboard(seat);
        LOG_INFO("[C] Calling wlr_seat_keyboard_notify_enter");
        if (keyboard) {
            wlr_seat_keyboard_notify_enter(seat, xdg_view->xdg_surface->surface,
                keyboard->keycodes, keyboard->num_keycodes, &keyboard->modifiers);
            LOG_INFO("[C] wlr_seat_keyboard_notify_enter complete");
        } else {
            LOG_WARN("[C] No keyboard available");
        }
    } else {
        LOG_WARN("[C] Invalid xdg_surface or surface");
    }
}

static void cpp_impl_view_raise(void* view) {
    LOG_INFO("[C] cpp_impl_view_raise: %p", view);
    if (!view || !((struct havel_xdg_view*)view)->scene_tree) {
        LOG_WARN("[C] Invalid view or scene_tree");
        return;
    }
    struct havel_xdg_view *xdg_view = (struct havel_xdg_view*)view;
    LOG_INFO("[C] Calling wlr_scene_node_raise_to_top");
    wlr_scene_node_raise_to_top(&xdg_view->scene_tree->node);
    LOG_INFO("[C] wlr_scene_node_raise_to_top complete");
}

static void cpp_impl_view_get_geometry(void* view, int* x, int* y, int* w, int* h) {
    if (!view) return;
    struct havel_xdg_view *xdg_view = (struct havel_xdg_view*)view;
    
    // Get position from scene node (single source of truth)
    if (xdg_view->scene_tree) {
        if (x) *x = xdg_view->scene_tree->node.x;
        if (y) *y = xdg_view->scene_tree->node.y;
    } else {
        if (x) *x = 0;
        if (y) *y = 0;
    }
    
    // Get size from XDG surface geometry
    if (xdg_view->xdg_surface && xdg_view->xdg_surface->surface) {
        struct wlr_box geo = xdg_view->xdg_surface->current.geometry;
        if (w) *w = geo.width;
        if (h) *h = geo.height;
    } else {
        if (w) *w = 0;
        if (h) *h = 0;
    }
}

static void cpp_impl_view_close(void* view) {
    if (!view) return;
    struct havel_xdg_view *xdg_view = (struct havel_xdg_view*)view;
    if (xdg_view->xdg_surface && xdg_view->xdg_surface->toplevel) {
        wlr_xdg_toplevel_send_close(xdg_view->xdg_surface->toplevel);
    }
}

static void cpp_impl_view_set_fullscreen(void* view, bool fullscreen) {
    if (!view) return;
    struct havel_xdg_view *xdg_view = (struct havel_xdg_view*)view;
    if (xdg_view->xdg_surface && xdg_view->xdg_surface->toplevel) {
        wlr_xdg_toplevel_set_fullscreen(xdg_view->xdg_surface->toplevel, fullscreen);
    }
}

static void cpp_impl_view_minimize(void* view) {
    if (!view) return;
    struct havel_xdg_view *xdg_view = (struct havel_xdg_view*)view;
    // Hide the scene node to minimize (doesn't unmap, just hides)
    if (xdg_view->scene_tree) {
        wlr_scene_node_set_enabled(&xdg_view->scene_tree->node, false);
    }
}

static void cpp_impl_workspace_arrange(uint32_t workspace_id) {
    // Triggered from C++ when layout needs to be recalculated
    // The actual arrangement logic is now in C++
}

static void cpp_impl_workspace_set_active(uint32_t workspace_id) {
    // Handled by C++ Server
}

static void cpp_impl_server_quit(void) {
    // This is called from C++ layer when quit is requested
    // We need to terminate the wl_display event loop
    // The display pointer is stored in the server struct
    // For now, we'll use a global pointer (initialized in havel_wlr_run)
    extern struct havel_wlr_server *g_running_server;
    if (g_running_server && g_running_server->display) {
        wl_display_terminate(g_running_server->display);
    }
}

static void cpp_impl_server_spawn(const char* command) {
    pid_t pid = fork();
    if (pid < 0) return;
    if (pid == 0) {
        execl("/bin/sh", "/bin/sh", "-c", command, (char*)NULL);
        _exit(127);
    }
}

// ============================================================================
// XDG View Handlers
// ============================================================================

// XDG surface commit listener - sends configure after first client commit
static void xdg_surface_handle_commit(struct wl_listener *listener, void *data) {
    struct havel_xdg_view *view = wl_container_of(listener, view, surface_commit);

    // Safety check: view might be destroyed before first commit
    if (!view || !view->xdg_surface) {
        return;
    }

    // Remove this listener - only needed once
    wl_list_remove(&view->surface_commit.link);
    view->surface_commit.notify = NULL;  // Mark as removed

    // NOW we can safely send configure - surface->initialized is true
    if (view->xdg_surface->toplevel) {
        LOG_INFO("[XDG] First commit received, sending configure");
        wlr_xdg_toplevel_set_size(view->xdg_surface->toplevel, 800, 600);
        wlr_xdg_toplevel_set_activated(view->xdg_surface->toplevel, true);
    }
}

static void xdg_view_set_position(struct havel_xdg_view *view, int x, int y) {
    if (!view || !view->scene_tree) return;
    // Geometry stored in C++ View now, not in C struct
    wlr_scene_node_set_position(&view->scene_tree->node, x, y);
}

static void xwayland_view_set_position(struct havel_xwayland_view *view, int x, int y) {
    if (!view || !view->scene_tree) return;
    // Geometry stored in C++ View now, not in C struct
    wlr_scene_node_set_position(&view->scene_tree->node, x, y);
}

static void xdg_view_handle_map(struct wl_listener *listener, void *data) {
    struct havel_xdg_view *view = wl_container_of(listener, view, map);
    
    // Safety check: view might be in inconsistent state
    if (!view || !view->xdg_surface || !view->scene_tree || !view->cpp_view) {
        LOG_WARN("[XDG] MAP: Invalid view or null pointers");
        return;
    }
    
    LOG_INFO("[XDG] MAP: %p (xdg_surface=%p, scene_tree=%p, cpp_view=%p)", 
             (void*)view, (void*)view->xdg_surface, (void*)view->scene_tree, view->cpp_view);
    
    // Notify C++ layer - C++ owns mapped state
    havel_cpp_on_view_mapped(view->server->cpp_server, view->cpp_view);

    // Force node to top and enabled
    wlr_scene_node_raise_to_top(&view->scene_tree->node);
    wlr_scene_node_set_enabled(&view->scene_tree->node, true);

    // Position window in center of output (using output layout coordinates)
    // Get first output from list
    struct havel_output *havel_out = NULL;
    if (!wl_list_empty(&view->server->outputs)) {
        struct wl_list *next = view->server->outputs.next;
        havel_out = wl_container_of(next, havel_out, link);
    }
    struct wlr_output *wlr_out = havel_out ? havel_out->output : NULL;

    if (wlr_out && wlr_out->width > 0 && wlr_out->height > 0) {
        // Get surface size
        struct wlr_box geo = view->xdg_surface->current.geometry;
        int win_w = geo.width > 0 ? geo.width : 800;
        int win_h = geo.height > 0 ? geo.height : 600;

        // Get output position in layout (for multi-monitor support)
        struct wlr_box output_box;
        wlr_output_layout_get_box(view->server->output_layout, wlr_out, &output_box);

        // Center window in output using output_box dimensions (accounts for scale)
        int x = output_box.x + (output_box.width - win_w) / 2;
        int y = output_box.y + (output_box.height - win_h) / 2;
        wlr_scene_node_set_position(&view->scene_tree->node, x, y);
        LOG_INFO("[XDG] Positioned window at (%d, %d) size (%dx%d) on output %s",
                 x, y, win_w, win_h, wlr_out->name);
    }
    
    // Set keyboard focus to this surface
    struct wlr_seat *seat = view->server->seat;
    struct wlr_keyboard *keyboard = wlr_seat_get_keyboard(seat);
    if (keyboard && view->xdg_surface->surface) {
        LOG_INFO("[XDG] Setting keyboard focus to surface %p", (void*)view->xdg_surface->surface);
        wlr_seat_keyboard_notify_enter(seat, view->xdg_surface->surface,
            keyboard->keycodes, keyboard->num_keycodes, &keyboard->modifiers);
    }
    
    LOG_INFO("[DEBUG] Node raised, positioned, keyboard focus set");
}

static void xdg_view_handle_unmap(struct wl_listener *listener, void *data) {
    struct havel_xdg_view *view = wl_container_of(listener, view, unmap);

    if (!view || !view->cpp_view) return;

    // Notify C++ layer - C++ owns mapped state
    havel_cpp_on_view_unmapped(view->server->cpp_server, view->cpp_view);
}

static void xdg_handle_app_id(struct wl_listener *listener, void *data) {
    struct havel_xdg_view *view = wl_container_of(listener, view, set_app_id);
    
    if (!view || !view->cpp_view || !view->xdg_surface->toplevel) return;
    
    const char *app_id = view->xdg_surface->toplevel->app_id;
    LOG_INFO("[XDG] App ID set: %s", app_id ? app_id : "(null)");
    
    // C++ layer owns View metadata - it will query via CompositorAPI
    // The View object stores appId, we just need to ensure it's available
}

static void xdg_handle_title(struct wl_listener *listener, void *data) {
    struct havel_xdg_view *view = wl_container_of(listener, view, set_title);
    
    if (!view || !view->cpp_view || !view->xdg_surface->toplevel) return;
    
    const char *title = view->xdg_surface->toplevel->title;
    LOG_INFO("[XDG] Title set: %s", title ? title : "(null)");
    
    // C++ layer owns View metadata - it will query via CompositorAPI
}

static void xdg_view_handle_destroy(struct wl_listener *listener, void *data) {
    struct havel_xdg_view *view = wl_container_of(listener, view, destroy);

    LOG_INFO("[XDG] DESTROY: %p (cpp_view=%p)", (void*)view, view->cpp_view);

    // CRITICAL: Remove set_app_id and set_title listeners FIRST
    // These are on toplevel->events which wlroots cleans up during destroy
    // wlroots asserts that these lists are empty
    wl_list_remove(&view->set_app_id.link);
    wl_list_remove(&view->set_title.link);

    // Remove remaining listeners
    wl_list_remove(&view->map.link);
    wl_list_remove(&view->unmap.link);
    wl_list_remove(&view->destroy.link);

    // surface_commit may have already been removed by the commit handler
    if (view->surface_commit.notify != NULL) {
        wl_list_remove(&view->surface_commit.link);
    }

    // Notify C++ layer BEFORE freeing - C++ destroys View object
    if (view->cpp_view) {
        havel_cpp_on_view_destroyed(view->server->cpp_server, view->cpp_view);
    }

    // Scene node is destroyed automatically by wlr_scene_xdg_surface_create
    // Do NOT manually destroy it

    LOG_INFO("[XDG] View freed");
    free(view);
}

static void server_new_xdg_toplevel(struct wl_listener *listener, void *data) {
    struct havel_wlr_server *server = wl_container_of(listener, server, new_xdg_toplevel);
    struct wlr_xdg_toplevel *toplevel = data;
    struct wlr_xdg_surface *xdg_surface = toplevel->base;

    LOG_INFO("[XDG] New toplevel: %p (surface=%p)", (void*)toplevel, (void*)xdg_surface);

    if (xdg_surface->role != WLR_XDG_SURFACE_ROLE_TOPLEVEL) {
        LOG_WARN("[XDG] Toplevel has wrong role: %d", xdg_surface->role);
        return;
    }

    struct havel_xdg_view *view = calloc(1, sizeof(*view));
    view->server = server;
    view->xdg_surface = xdg_surface;
    xdg_surface->data = view;
    // NO workspace_id in C - C++ owns this
    // NO mapped in C - C++ owns this

    // Use global workspace tree for this workspace
    struct wlr_scene_tree *parent = &server->scene->tree;  // Default to root
    if (server->active_workspace < HAVEL_WORKSPACE_COUNT) {
        parent = server->workspaces[server->active_workspace];
    }
    LOG_INFO("[XDG] Using parent=%p (workspace %d tree)", (void*)parent, server->active_workspace);

    // CRITICAL: Add listeners BEFORE creating scene surface
    // This ensures we don't miss the map event
    LOG_INFO("[XDG] Adding map listener to surface %p", (void*)xdg_surface->surface);
    view->map.notify = xdg_view_handle_map;
    wl_signal_add(&xdg_surface->surface->events.map, &view->map);

    LOG_INFO("[XDG] Adding unmap listener");
    view->unmap.notify = xdg_view_handle_unmap;
    wl_signal_add(&xdg_surface->surface->events.unmap, &view->unmap);

    LOG_INFO("[XDG] Adding destroy listener");
    view->destroy.notify = xdg_view_handle_destroy;
    wl_signal_add(&xdg_surface->events.destroy, &view->destroy);

    // Add commit listener - will send configure after first client commit
    LOG_INFO("[XDG] Adding commit listener (will send configure after first commit)");
    view->surface_commit.notify = xdg_surface_handle_commit;
    wl_signal_add(&xdg_surface->surface->events.commit, &view->surface_commit);

    // Add app_id and title listeners - these fire when client sets window metadata
    LOG_INFO("[XDG] Adding set_app_id listener");
    view->set_app_id.notify = xdg_handle_app_id;
    wl_signal_add(&toplevel->events.set_app_id, &view->set_app_id);

    LOG_INFO("[XDG] Adding set_title listener");
    view->set_title.notify = xdg_handle_title;
    wl_signal_add(&toplevel->events.set_title, &view->set_title);

    // NOW create scene surface attached to workspace tree (SINGLE CALL)
    LOG_INFO("[XDG] Calling wlr_scene_xdg_surface_create (parent=%p)...", (void*)parent);
    view->scene_tree = wlr_scene_xdg_surface_create(parent, xdg_surface);
    if (!view->scene_tree) {
        LOG_ERROR("[XDG] wlr_scene_xdg_surface_create returned NULL!");
        return;
    }
    LOG_INFO("[XDG] scene_tree created: %p, node.enabled=%d", 
             (void*)view->scene_tree, view->scene_tree->node.enabled);

    // DO NOT call wlr_xdg_toplevel_set_size here!
    // surface->initialized is false at this point - will crash!
    // Size/activation must be set AFTER first commit (handled by commit listener).

    // Get window metadata from XDG toplevel
    const char* appId = NULL;
    const char* title = NULL;
    if (xdg_surface->toplevel) {
        appId = xdg_surface->toplevel->app_id ? xdg_surface->toplevel->app_id : "";
        title = xdg_surface->toplevel->title ? xdg_surface->toplevel->title : "";
    }

    // Notify C++ layer - it creates the View object and owns all state
    view->cpp_view = havel_cpp_on_xdg_surface_new(server->cpp_server, view, server->active_workspace, appId, title);

    LOG_INFO("[XDG] View setup complete for %p (cpp_view=%p, parent=%p, appId=%s, title=%s)",
             (void*)view, view->cpp_view, (void*)parent, appId ? appId : "unknown", title ? title : "unknown");
}

static void server_new_xdg_surface(struct wl_listener *listener, void *data) {
    // Don't handle new_surface - wait for new_toplevel event instead
    // This ensures the role is already assigned
    (void)listener;
    (void)data;
}

// ============================================================================
// XWayland View Handlers
// ============================================================================

static void xwayland_view_handle_destroy(struct wl_listener *listener, void *data) {
    struct havel_xwayland_view *view = wl_container_of(listener, view, destroy);
    
    LOG_INFO("[XWayland] DESTROY: %p (cpp_view=%p)", (void*)view, view->cpp_view);
    
    wl_list_remove(&view->destroy.link);
    
    // Notify C++ layer BEFORE freeing - C++ destroys View object
    if (view->cpp_view) {
        havel_cpp_on_view_destroyed(view->server->cpp_server, view->cpp_view);
    }
    
    free(view);
}

static void server_new_xwayland_surface(struct wl_listener *listener, void *data) {
    struct havel_wlr_server *server = wl_container_of(listener, server, new_xwayland_surface);
    struct wlr_xwayland_surface *xsurface = data;

    if (xsurface->override_redirect) {
        return;
    }

    struct havel_xwayland_view *view = calloc(1, sizeof(*view));
    view->server = server;
    view->xsurface = xsurface;
    xsurface->data = view;
    // NO workspace_id in C - C++ owns this
    
    // Create scene tree first
    view->scene_tree = wlr_scene_tree_create(&server->scene->tree);
    wlr_scene_surface_create(view->scene_tree, xsurface->surface);

    // Apply geometry
    bool has_position = (xsurface->x != 0 || xsurface->y != 0) &&
                        (xsurface->x != -1 && xsurface->y != -1);
    bool position_valid = false;
    if (has_position) {
        struct wlr_output *wlr_out = wlr_output_layout_output_at(server->output_layout,
                                                               xsurface->x, xsurface->y);
        position_valid = (wlr_out != NULL);
    }

    if (position_valid) {
        xwayland_view_set_position(view, xsurface->x, xsurface->y);
    } else {
        xwayland_view_set_position(view, xsurface->x, xsurface->y);
        // Geometry stored in C++ View now, not in C struct
    }

    // Get window metadata from XWayland surface
    const char* appId = xsurface->class ? xsurface->class : "";
    const char* title = xsurface->title ? xsurface->title : "";

    // Notify C++ layer - it creates the View object and owns all state
    view->cpp_view = havel_cpp_on_xdg_surface_new(server->cpp_server, view, server->active_workspace, appId, title);

    view->destroy.notify = xwayland_view_handle_destroy;
    wl_signal_add(&xsurface->events.destroy, &view->destroy);

    LOG_INFO("[XWayland] View setup complete for %p (cpp_view=%p, appId=%s, title=%s)",
             (void*)view, view->cpp_view, appId, title);
}

// ============================================================================
// Layer-Shell v1 Handlers (for waybar, notifications, etc.)
// ============================================================================

struct havel_layer_surface {
    struct wlr_scene_layer_surface_v1 *scene_layer_surface;
    struct wlr_scene_tree *scene_tree;
    struct havel_wlr_server *server;
    struct wlr_output *output;

    struct wl_listener destroy;
    struct wl_listener map;
    struct wl_listener unmap;
};

static void layer_surface_handle_destroy(struct wl_listener *listener, void *data) {
    struct havel_layer_surface *lsurface = wl_container_of(listener, lsurface, destroy);

    LOG_INFO("[LayerShell] Surface destroyed");

    wl_list_remove(&lsurface->destroy.link);
    wl_list_remove(&lsurface->map.link);
    wl_list_remove(&lsurface->unmap.link);

    if (lsurface->scene_tree) {
        wlr_scene_node_destroy(&lsurface->scene_tree->node);
    }

    free(lsurface);
}

static void layer_surface_handle_map(struct wl_listener *listener, void *data) {
    struct havel_layer_surface *lsurface = wl_container_of(listener, lsurface, map);
    struct wlr_output *output = lsurface->output;

    LOG_INFO("[LayerShell] Surface mapped: namespace=%s, layer=%d",
             lsurface->scene_layer_surface->layer_surface->namespace,
             lsurface->scene_layer_surface->layer_surface->current.layer);

    // Configure now that surface is initialized - fixes assertion failure
    if (output) {
        struct wlr_box full_area = {0, 0, output->width, output->height};
        struct wlr_box usable_area = {0, 0, output->width, output->height};
        wlr_scene_layer_surface_v1_configure(lsurface->scene_layer_surface, &full_area, &usable_area);
    }

    // Raise to top when mapped
    wlr_scene_node_raise_to_top(&lsurface->scene_tree->node);
}

static void layer_surface_handle_unmap(struct wl_listener *listener, void *data) {
    struct havel_layer_surface *lsurface = wl_container_of(listener, lsurface, unmap);

    LOG_INFO("[LayerShell] Surface unmapped");
}

static void server_new_layer_surface(struct wl_listener *listener, void *data) {
    struct havel_wlr_server *server = wl_container_of(listener, server, new_layer_surface);
    struct wlr_layer_surface_v1 *layer_surface = data;

    LOG_INFO("[LayerShell] New surface: namespace=%s", layer_surface->namespace);

    // Create layer surface wrapper
    struct havel_layer_surface *lsurface = calloc(1, sizeof(*lsurface));
    lsurface->server = server;

    // If no output assigned, use primary output
    struct wlr_output *output = layer_surface->output;
    if (!output && !wl_list_empty(&server->outputs)) {
        struct havel_output *havel_out = wl_container_of(server->outputs.next, havel_out, link);
        output = havel_out->output;
        layer_surface->output = output;  // Assign output to layer surface
    }
    lsurface->output = output;

    // Create scene tree for this layer surface
    lsurface->scene_tree = wlr_scene_tree_create(&server->scene->tree);

    // Create scene layer surface - this handles all the layer-shell protocol details
    lsurface->scene_layer_surface = wlr_scene_layer_surface_v1_create(
        lsurface->scene_tree, layer_surface);

    if (!lsurface->scene_layer_surface) {
        LOG_ERROR("[LayerShell] Failed to create scene layer surface");
        wlr_scene_node_destroy(&lsurface->scene_tree->node);
        free(lsurface);
        return;
    }

    // Add listeners
    lsurface->destroy.notify = layer_surface_handle_destroy;
    wl_signal_add(&layer_surface->events.destroy, &lsurface->destroy);

    lsurface->map.notify = layer_surface_handle_map;
    wl_signal_add(&layer_surface->surface->events.map, &lsurface->map);

    lsurface->unmap.notify = layer_surface_handle_unmap;
    wl_signal_add(&layer_surface->surface->events.unmap, &lsurface->unmap);

    // Raise to top
    wlr_scene_node_raise_to_top(&lsurface->scene_tree->node);

    LOG_INFO("[LayerShell] Surface setup complete: %p (output=%s)",
             (void*)lsurface, output ? output->name : "none");
}

// ============================================================================
// Output Handlers
// ============================================================================

static void output_frame(struct wl_listener *listener, void *data) {
    struct havel_output *output = wl_container_of(listener, output, frame);
    struct havel_wlr_server *server = output->server;

    // DEBUG: Confirm frame callback is firing
    static int frame_count = 0;
    if (++frame_count % 60 == 0) {
        LOG_INFO("[DEBUG] Frame #%d on %s", frame_count, output->output->name);
    }

    LOG_INFO("[FRAME] %s: >>> START", output->output->name);

    // Update animations before rendering
    havel_cpp_update_animations(server->cpp_server);

    // Dispatch frame event to plugins (via C++ server)
    havel_cpp_dispatch_output_frame(server->cpp_server, output->output, output->scene_output);

    // Get background color from wallpaper plugin
    float bgR, bgG, bgB;
    havel_cpp_get_background_color(server->cpp_server, &bgR, &bgG, &bgB);

    // Note: Background color clear requires proper render pass integration
    // For now, the wallpaper plugin sets the color but clearing happens elsewhere
    // TODO: Integrate with wlroots 0.20 render pass API for proper clear
    (void)bgR; (void)bgG; (void)bgB;

    // Commit scene output using wlroots 0.20 API
    const struct wlr_scene_output_state_options options = {
        .timer = NULL,
    };

    if (!output->scene_output) {
        LOG_ERROR("[FRAME] %s: scene_output is NULL!", output->output->name);
        return;
    }

    // Render overlays BEFORE committing scene
    // This ensures overlays are composited into the frame
    havel_cpp_draw_overlays(server->cpp_server, output->output->width, output->output->height);

    LOG_INFO("[FRAME] %s: calling wlr_scene_output_commit", output->output->name);

    wlr_scene_output_commit(output->scene_output, &options);
    LOG_INFO("[FRAME] %s: <<< COMMIT COMPLETE", output->output->name);
}

static void output_destroy(struct wl_listener *listener, void *data) {
    struct havel_output *output = wl_container_of(listener, output, destroy);

    // Free gamma LUT buffers
    if (output->gamma_ramp_red) {
        free(output->gamma_ramp_red);
    }
    if (output->gamma_ramp_green) {
        free(output->gamma_ramp_green);
    }
    if (output->gamma_ramp_blue) {
        free(output->gamma_ramp_blue);
    }

    wl_list_remove(&output->frame.link);
    wl_list_remove(&output->destroy.link);
    wl_list_remove(&output->link);
    free(output);
}

static void server_new_output(struct wl_listener *listener, void *data) {
    struct havel_wlr_server *server = wl_container_of(listener, server, new_output);
    struct wlr_output *wlr_output = data;

    LOG_INFO("[OUTPUT] New output: %s", wlr_output->name);

    wlr_output_init_render(wlr_output, server->allocator, server->renderer);

    if (!wl_list_empty(&wlr_output->modes)) {
        struct wlr_output_mode *mode = wlr_output_preferred_mode(wlr_output);
        struct wlr_output_state state;
        wlr_output_state_init(&state);
        wlr_output_state_set_mode(&state, mode);
        wlr_output_state_set_enabled(&state, true);
        if (!wlr_output_commit_state(wlr_output, &state)) {
            wlr_output_state_finish(&state);
            return;
        }
        wlr_output_state_finish(&state);
    }

    struct havel_output *output = calloc(1, sizeof(*output));
    output->server = server;
    output->output = wlr_output;
    output->scene_output = wlr_scene_output_create(server->scene, wlr_output);

    // Initialize gamma state to defaults
    output->gamma = 1.0f;
    output->temperature = 6500;
    output->brightness = 1.0f;

    // Allocate gamma LUT buffers once per output
    size_t gamma_size = wlr_output_get_gamma_size(wlr_output);
    if (gamma_size > 0) {
        output->gamma_ramp_size = gamma_size;
        output->gamma_ramp_red = calloc(gamma_size, sizeof(uint16_t));
        output->gamma_ramp_green = calloc(gamma_size, sizeof(uint16_t));
        output->gamma_ramp_blue = calloc(gamma_size, sizeof(uint16_t));
        output->gamma_ramp_dirty = true;  // Force initial LUT generation
        LOG_INFO("[OUTPUT] Gamma LUT allocated for %s (size=%zu)", wlr_output->name, gamma_size);
    } else {
        output->gamma_ramp_size = 0;
        output->gamma_ramp_red = NULL;
        output->gamma_ramp_green = NULL;
        output->gamma_ramp_blue = NULL;
        output->gamma_ramp_dirty = false;
        LOG_WARN("[OUTPUT] %s does not support gamma control", wlr_output->name);
    }

    wl_list_insert(server->outputs.prev, &output->link);  // Append to list (not prepend)
    output->is_primary = wl_list_empty(&server->outputs) || (server->outputs.next == &output->link);

    LOG_DEBUG("[OUTPUT] %s is %s", wlr_output->name, output->is_primary ? "primary" : "secondary");

    output->frame.notify = output_frame;
    wl_signal_add(&wlr_output->events.frame, &output->frame);
    LOG_INFO("[OUTPUT] Frame handler installed for %s", wlr_output->name);

    output->destroy.notify = output_destroy;
    wl_signal_add(&wlr_output->events.destroy, &output->destroy);

    wlr_output_layout_add_auto(server->output_layout, wlr_output);

    // Verify output state and scene graph
    LOG_INFO("[OUTPUT] %s setup COMPLETE (enabled=%d)",
             wlr_output->name, wlr_output->enabled);
    LOG_INFO("[SCENE] Scene root: %p, enabled=%d",
             (void*)&server->scene->tree, server->scene->tree.node.enabled);
    LOG_INFO("[SCENE_OUTPUT] %s: scene_output=%p",
             wlr_output->name, (void*)output->scene_output);
}

// ============================================================================
// Overlay Rendering (Scene Graph Based)
// ============================================================================

// Simple Alt-Tab overlay state
static struct {
    struct wlr_scene_rect *background;
    struct wlr_scene_rect *box;
    struct wlr_scene_rect *highlight;  // Highlight bar for selection
    bool visible;
    int selected_index;
} alt_tab_overlay = {0};

static void alt_tab_init(void) {
    if (alt_tab_overlay.background) return;  // Already initialized
    
    // Create semi-transparent dark background (full screen)
    alt_tab_overlay.background = wlr_scene_rect_create(
        server->overlay_layer, 1, 1, (float[4]){0.0f, 0.0f, 0.0f, 0.7f});
    
    // Create centered box (400x200)
    alt_tab_overlay.box = wlr_scene_rect_create(
        server->overlay_layer, 400, 200, (float[4]){0.2f, 0.2f, 0.3f, 0.95f});
    
    // Create highlight bar (shows selected window)
    alt_tab_overlay.highlight = wlr_scene_rect_create(
        server->overlay_layer, 360, 40, (float[4]){0.4f, 0.4f, 0.5f, 0.8f});
    
    alt_tab_overlay.visible = false;
    alt_tab_overlay.selected_index = 0;
    LOG_INFO("[Overlay] Alt-Tab overlay initialized");
}

static void alt_tab_show(int screen_width, int screen_height) {
    if (!alt_tab_overlay.background) alt_tab_init();
    
    // Size background to screen
    wlr_scene_rect_set_size(alt_tab_overlay.background, screen_width, screen_height);
    
    // Center the box
    int box_x = (screen_width - 400) / 2;
    int box_y = (screen_height - 200) / 2;
    wlr_scene_node_set_position(&alt_tab_overlay.box->node, box_x, box_y);
    
    // Position highlight bar
    wlr_scene_node_set_position(&alt_tab_overlay.highlight->node, box_x + 20, box_y + 60);
    
    wlr_scene_node_set_enabled(&alt_tab_overlay.background->node, true);
    wlr_scene_node_set_enabled(&alt_tab_overlay.box->node, true);
    wlr_scene_node_set_enabled(&alt_tab_overlay.highlight->node, true);
    alt_tab_overlay.visible = true;
    alt_tab_overlay.selected_index = 0;
    
    LOG_INFO("[Overlay] Alt-Tab shown (%dx%d)", screen_width, screen_height);
}

static void alt_tab_hide(void) {
    if (!alt_tab_overlay.visible) return;
    
    wlr_scene_node_set_enabled(&alt_tab_overlay.background->node, false);
    wlr_scene_node_set_enabled(&alt_tab_overlay.box->node, false);
    wlr_scene_node_set_enabled(&alt_tab_overlay.highlight->node, false);
    alt_tab_overlay.visible = false;
    
    LOG_INFO("[Overlay] Alt-Tab hidden");
}

static void alt_tab_cycle(void) {
    if (!alt_tab_overlay.visible) return;
    
    // Cycle selected index
    alt_tab_overlay.selected_index = (alt_tab_overlay.selected_index + 1) % 10;
    
    // Update highlight position (3 windows visible, 50px each including spacing)
    int box_x = alt_tab_overlay.box->node.x;
    int box_y = alt_tab_overlay.box->node.y;
    int new_y = box_y + 60 + (alt_tab_overlay.selected_index * 50);
    wlr_scene_node_set_position(&alt_tab_overlay.highlight->node, box_x + 20, new_y);
    
    LOG_DEBUG("[AltTab] Cycle to index %d", alt_tab_overlay.selected_index);
}

static void alt_tab_select(void) {
    if (!alt_tab_overlay.visible) return;

    LOG_INFO("[AltTab] Select window at index %d", alt_tab_overlay.selected_index);
    
    // Focus selected window via C++ layer
    // The C++ Server class handles the actual focus logic
    if (server->cpp_server) {
        havel_cpp_alt_tab_select(server->cpp_server, alt_tab_overlay.selected_index);
    }
    
    alt_tab_hide();
}

// ============================================================================
// Input Handlers
// ============================================================================

static void keyboard_handle_modifiers(struct wl_listener *listener, void *data) {
    struct havel_keyboard *keyboard = wl_container_of(listener, keyboard, modifiers);
    wlr_seat_set_keyboard(keyboard->server->seat, keyboard->keyboard);
    wlr_seat_keyboard_notify_modifiers(keyboard->server->seat, &keyboard->keyboard->modifiers);
}

static void keyboard_handle_key(struct wl_listener *listener, void *data) {
    struct havel_keyboard *keyboard = wl_container_of(listener, keyboard, key);
    struct havel_wlr_server *server = keyboard->server;
    struct wlr_keyboard_key_event *event = data;

    wlr_seat_set_keyboard(server->seat, keyboard->keyboard);

    // Update XKB state
    if (keyboard->xkb_state) {
        xkb_state_update_key(keyboard->xkb_state, event->keycode + 8,
                            event->state == WL_KEYBOARD_KEY_STATE_PRESSED ? XKB_KEY_DOWN : XKB_KEY_UP);
    }

    if (event->state == WL_KEYBOARD_KEY_STATE_PRESSED) {
        const uint32_t keycode = event->keycode + 8;
        uint32_t modifiers = keyboard->keyboard->modifiers.depressed;

        // Get keysym from XKB state (layout-aware)
        xkb_keysym_t keysym = 0;
        char key_char = 0;
        char utf8_buffer[8] = {0};  // Buffer for UTF-8 character
        if (keyboard->xkb_state) {
            keysym = xkb_state_key_get_one_sym(keyboard->xkb_state, keycode);

            // Convert keysym to UTF-8 character (handles shift, layout, etc.)
            int len = xkb_keysym_to_utf8(keysym, utf8_buffer, sizeof(utf8_buffer));
            if (len > 0 && len <= 4) {
                // For single-byte ASCII, use directly
                if (len == 1 && utf8_buffer[0] >= 32 && utf8_buffer[0] <= 126) {
                    key_char = utf8_buffer[0];
                }
                // For multi-byte UTF-8, key_char stays 0 and AppLauncher should use utf8_buffer
            }
        }

        // Handle shift-modified symbols for common keys
        // This ensures shift+number produces correct symbols (!@#$%^&*() etc.)
        if (keyboard->xkb_state && key_char == 0) {
            // Check if shift is pressed
            xkb_mod_index_t shift_idx = xkb_keymap_mod_get_index(keyboard->keymap, XKB_MOD_NAME_SHIFT);
            bool shift_pressed = (shift_idx != XKB_MOD_INVALID) &&
                               (xkb_state_mod_index_is_active(keyboard->xkb_state, shift_idx, XKB_STATE_MODS_DEPRESSED) > 0);

            if (shift_pressed && keysym >= XKB_KEY_exclam && keysym <= XKB_KEY_asciitilde) {
                // Shift-modified symbol keys (! " # $ % & ' ( ) * + , - . / : ; < = > ? @ [ \ ] ^ _ ` { | } ~)
                int len = xkb_keysym_to_utf8(keysym, utf8_buffer, sizeof(utf8_buffer));
                if (len == 1) {
                    key_char = utf8_buffer[0];
                }
            }
        }

        // ====================================================================
        // VT Switching (Ctrl+Alt+F1..F12) - MUST be handled before C++ layer
        // ====================================================================
        // Check for Ctrl+Alt modifiers using XKB state (layout-independent)
        xkb_mod_index_t ctrl_idx = xkb_keymap_mod_get_index(keyboard->keymap, XKB_MOD_NAME_CTRL);
        xkb_mod_index_t alt_idx = xkb_keymap_mod_get_index(keyboard->keymap, XKB_MOD_NAME_ALT);

        bool ctrl_pressed = (ctrl_idx != XKB_MOD_INVALID) &&
                           (xkb_state_mod_index_is_active(keyboard->xkb_state, ctrl_idx, XKB_STATE_MODS_DEPRESSED) > 0);
        bool alt_pressed = (alt_idx != XKB_MOD_INVALID) &&
                          (xkb_state_mod_index_is_active(keyboard->xkb_state, alt_idx, XKB_STATE_MODS_DEPRESSED) > 0);

        // Fallback: check raw modifier mask (Mod1=Alt=bit 3, Control=bit 2)
        #define MOD_CTRL (1 << 2)
        #define MOD_ALT (1 << 3)
        if (!ctrl_pressed && (modifiers & MOD_CTRL)) ctrl_pressed = true;
        if (!alt_pressed && (modifiers & MOD_ALT)) alt_pressed = true;
        #undef MOD_CTRL
        #undef MOD_ALT

        LOG_INFO("[VT] keysym=0x%x ctrl=%d alt=%d modifiers=0x%x", keysym, ctrl_pressed, alt_pressed, modifiers);

        if (ctrl_pressed && alt_pressed && keysym >= XKB_KEY_F1 && keysym <= XKB_KEY_F12) {
            unsigned int vt = keysym - XKB_KEY_F1 + 1;
            LOG_INFO("[VT] >>> Switching to VT%u (keysym=0x%x) <<<", vt, keysym);
            if (server->session) {
                int result = wlr_session_change_vt(server->session, vt);
                LOG_INFO("[VT] wlr_session_change_vt returned: %d", result);
            } else {
                LOG_ERROR("[VT] No session available for VT switch!");
            }
            return;  // Consume the event, don't forward
        }

        // ====================================================================
        // Alt+Tab Overlay (basic scene-graph based)
        // ====================================================================
        LOG_INFO("[AltTab] Check: alt=%d keysym=0x%x(XKB_Tab=0x%x) ctrl=%d", 
                 alt_pressed, keysym, XKB_KEY_Tab, ctrl_pressed);
        
        if (alt_pressed && keysym == XKB_KEY_Tab && !ctrl_pressed) {
            LOG_INFO("[AltTab] Condition matched!");
            // Get primary output dimensions
            struct havel_output *output;
            int width = 1920, height = 1080;  // Default fallback
            wl_list_for_each(output, &server->outputs, link) {
                if (output->output && output->output->enabled) {
                    width = output->output->width;
                    height = output->output->height;
                    break;
                }
            }

            LOG_INFO("[AltTab] Overlay visible=%d", alt_tab_overlay.visible);
            if (alt_tab_overlay.visible) {
                // Cycle to next window
                LOG_INFO("[AltTab] Cycling...");
                alt_tab_cycle();
            } else {
                // Show overlay
                LOG_INFO("[AltTab] Showing overlay %dx%d", width, height);
                alt_tab_show(width, height);
            }
            return;  // Consume the event
        }
        
        // Enter selects window
        if (alt_tab_overlay.visible && (keysym == XKB_KEY_Return || keysym == XKB_KEY_KP_Enter)) {
            alt_tab_select();
            return;  // Consume the event
        }
        
        // Escape hides Alt-Tab
        if (alt_tab_overlay.visible && keysym == XKB_KEY_Escape) {
            alt_tab_hide();
            return;  // Consume the event
        }

        LOG_DEBUG("[KEY] keycode=%u modifiers=%u keysym=0x%x char='%c'",
                  keycode, modifiers, keysym, key_char ? key_char : ' ');

        // DEBUG: Log before forwarding to C++
        LOG_INFO("[DEBUG] Forwarding key to C++: keycode=%u, mods=0x%x", keycode, modifiers);

        // Forward to C++ layer for policy decisions
        // Returns true if event was consumed (e.g., keybinding matched)
        bool consumed = havel_cpp_on_key(server->cpp_server, keycode, true, modifiers, keysym, key_char, utf8_buffer);

        LOG_INFO("[DEBUG] Key consumed=%d", consumed);

        // Only forward to seat if not consumed by compositor
        if (!consumed) {
            wlr_seat_keyboard_notify_key(server->seat, event->time_msec, event->keycode, event->state);
        }
        return;
    }

    wlr_seat_keyboard_notify_key(server->seat, event->time_msec, event->keycode, event->state);
}

static void keyboard_handle_destroy(struct wl_listener *listener, void *data) {
    struct havel_keyboard *keyboard = wl_container_of(listener, keyboard, destroy);
    
    // Free XKB resources
    if (keyboard->xkb_state) {
        xkb_state_unref(keyboard->xkb_state);
    }
    if (keyboard->keymap) {
        xkb_keymap_unref(keyboard->keymap);
    }
    
    wl_list_remove(&keyboard->modifiers.link);
    wl_list_remove(&keyboard->key.link);
    wl_list_remove(&keyboard->destroy.link);
    free(keyboard);
}

static void server_new_keyboard(struct havel_wlr_server *server, struct wlr_input_device *device) {
    struct wlr_keyboard *wlr_keyboard = wlr_keyboard_from_input_device(device);

    // Check for duplicate keyboard (same device pointer)
    // This can happen if hotplug events fire multiple times
    struct wlr_seat *seat = server->seat;
    if (seat->keyboard_state.keyboard == wlr_keyboard) {
        LOG_DEBUG("[INPUT] Keyboard already active (device=%p), skipping", (void*)device);
        return;
    }

    // Log device pointer to detect duplicates
    LOG_INFO("[INPUT] Keyboard added to seat (device=%p, keyboard=%p)", (void*)device, (void*)wlr_keyboard);

    struct havel_keyboard *keyboard = calloc(1, sizeof(*keyboard));
    keyboard->server = server;
    keyboard->keyboard = wlr_keyboard;

    struct xkb_context *context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    struct xkb_keymap *keymap = xkb_keymap_new_from_names(context, NULL, XKB_KEYMAP_COMPILE_NO_FLAGS);

    wlr_keyboard_set_keymap(wlr_keyboard, keymap);
    
    // Store keymap and create state for keysym lookup
    keyboard->keymap = keymap;
    keyboard->xkb_state = xkb_state_new(keymap);
    
    xkb_context_unref(context);

    wlr_keyboard_set_repeat_info(wlr_keyboard, 25, 600);

    keyboard->modifiers.notify = keyboard_handle_modifiers;
    wl_signal_add(&wlr_keyboard->events.modifiers, &keyboard->modifiers);

    keyboard->key.notify = keyboard_handle_key;
    wl_signal_add(&wlr_keyboard->events.key, &keyboard->key);

    keyboard->destroy.notify = keyboard_handle_destroy;
    wl_signal_add(&device->events.destroy, &keyboard->destroy);

    wlr_seat_set_keyboard(server->seat, wlr_keyboard);
    
    LOG_INFO("[INPUT] Keyboard added to seat");
}

static void server_new_pointer(struct havel_wlr_server *server, struct wlr_input_device *device) {
    wlr_cursor_attach_input_device(server->cursor, device);
}

static void server_new_input(struct wl_listener *listener, void *data) {
    struct havel_wlr_server *server = wl_container_of(listener, server, new_input);
    struct wlr_input_device *device = data;

    switch (device->type) {
    case WLR_INPUT_DEVICE_KEYBOARD:
        server_new_keyboard(server, device);
        break;
    case WLR_INPUT_DEVICE_POINTER:
        server_new_pointer(server, device);
        break;
    default:
        break;
    }

    uint32_t caps = WL_SEAT_CAPABILITY_POINTER;
    if (server->seat->keyboard_state.keyboard) {
        caps |= WL_SEAT_CAPABILITY_KEYBOARD;
    }
    wlr_seat_set_capabilities(server->seat, caps);
}

// ============================================================================
// Cursor Handlers
// ============================================================================

static struct wlr_surface *seat_surface_at(struct havel_wlr_server *server, double lx, double ly, double *sx, double *sy) {
    struct wlr_scene_node *node = wlr_scene_node_at(&server->scene->tree.node, lx, ly, sx, sy);
    if (!node || node->type != WLR_SCENE_NODE_BUFFER) {
        return NULL;
    }

    struct wlr_scene_buffer *scene_buffer = wl_container_of(node, scene_buffer, node);
    struct wlr_scene_surface *scene_surface = wlr_scene_surface_try_from_buffer(scene_buffer);
    if (!scene_surface) {
        return NULL;
    }

    return scene_surface->surface;
}

static void process_cursor_motion(struct havel_wlr_server *server, uint32_t time_msec) {
    double sx = 0.0, sy = 0.0;
    struct wlr_surface *surface = seat_surface_at(server, server->cursor->x, server->cursor->y, &sx, &sy);

    if (!surface) {
        wlr_cursor_set_xcursor(server->cursor, server->cursor_mgr, "left_ptr");
        wlr_seat_pointer_clear_focus(server->seat);
        return;
    }

    wlr_seat_pointer_notify_enter(server->seat, surface, sx, sy);
    wlr_seat_pointer_notify_motion(server->seat, time_msec, sx, sy);
}

static void server_cursor_motion(struct wl_listener *listener, void *data) {
    struct havel_wlr_server *server = wl_container_of(listener, server, cursor_motion);
    struct wlr_pointer_motion_event *event = data;

    wlr_cursor_move(server->cursor, &event->pointer->base, event->delta_x, event->delta_y);
    
    // Handle interactive move/resize
    if (server->grab.mode != INTERACTIVE_NONE && server->grab.view) {
        if (server->grab.mode == INTERACTIVE_MOVE) {
            // Move: update view position based on cursor delta
            double dx = server->cursor->x - server->grab.start_x;
            double dy = server->cursor->y - server->grab.start_y;
            int new_x = server->grab.view_start_x + (int)dx;
            int new_y = server->grab.view_start_y + (int)dy;
            wlr_scene_node_set_position(&server->grab.view->scene_tree->node, new_x, new_y);
        } else if (server->grab.mode == INTERACTIVE_RESIZE) {
            // Resize: update view size based on cursor delta
            double dx = server->cursor->x - server->grab.start_x;
            double dy = server->cursor->y - server->grab.start_y;
            int new_w = server->grab.view_start_w + (int)dx;
            int new_h = server->grab.view_start_h + (int)dy;
            // Minimum size constraints
            if (new_w < 100) new_w = 100;
            if (new_h < 60) new_h = 60;
            wlr_scene_node_set_position(&server->grab.view->scene_tree->node, 
                                        server->grab.view->scene_tree->node.x,
                                        server->grab.view->scene_tree->node.y);
            // Note: wlr_scene doesn't have direct resize, need to update xdg toplevel
            if (server->grab.view->xdg_surface && server->grab.view->xdg_surface->toplevel) {
                wlr_xdg_toplevel_set_size(server->grab.view->xdg_surface->toplevel, new_w, new_h);
            }
        }
        havel_cpp_on_pointer_motion(server->cpp_server, server->cursor->x, server->cursor->y);
        return;
    }
    
    // Normal cursor motion - notify for decoration hover
    havel_cpp_on_pointer_decoration_motion(server->cpp_server, 
                                           (int)server->cursor->x, 
                                           (int)server->cursor->y);
    havel_cpp_on_pointer_motion(server->cpp_server, server->cursor->x, server->cursor->y);
    process_cursor_motion(server, event->time_msec);
}

static void server_cursor_motion_absolute(struct wl_listener *listener, void *data) {
    struct havel_wlr_server *server = wl_container_of(listener, server, cursor_motion_absolute);
    struct wlr_pointer_motion_absolute_event *event = data;
    
    wlr_cursor_warp_absolute(server->cursor, &event->pointer->base, event->x, event->y);
    havel_cpp_on_pointer_motion(server->cpp_server, server->cursor->x, server->cursor->y);
    process_cursor_motion(server, event->time_msec);
}

static void server_cursor_button(struct wl_listener *listener, void *data) {
    struct havel_wlr_server *server = wl_container_of(listener, server, cursor_button);
    struct wlr_pointer_button_event *event = data;

    wlr_seat_pointer_notify_button(server->seat, event->time_msec, event->button, event->state);

    if (event->state != WL_POINTER_BUTTON_STATE_PRESSED) {
        // Button released - end interactive move/resize
        if (server->grab.mode != INTERACTIVE_NONE) {
            LOG_INFO("[Cursor] Interactive %s ended", 
                     server->grab.mode == INTERACTIVE_MOVE ? "move" : "resize");
            server->grab.view = NULL;
            server->grab.mode = INTERACTIVE_NONE;
        }
        // Notify decoration plugin of button release
        havel_cpp_on_pointer_decoration_button(server->cpp_server, event->button, false,
                                               (int)server->cursor->x, (int)server->cursor->y);
        havel_cpp_on_pointer_button(server->cpp_server, event->button, false, server->cursor->x, server->cursor->y);
        return;
    }

    // Check for Meta (Mod4) modifier for move/resize
    struct wlr_keyboard *keyboard = wlr_seat_get_keyboard(server->seat);
    uint32_t mods = keyboard ? keyboard->modifiers.depressed : 0;
    bool meta_pressed = (mods & (1 << 6)) != 0;  // Mod4 = Meta/Windows key
    
    // Hit test for surface under cursor
    double sx = 0, sy = 0;
    struct wlr_surface *surface = seat_surface_at(server, server->cursor->x, server->cursor->y, &sx, &sy);

    if (surface) {
        // Get the view from the surface
        struct wlr_xdg_surface *xdg_surface = wlr_xdg_surface_try_from_wlr_surface(surface);
        struct havel_xdg_view *xdg_view = xdg_surface ? (struct havel_xdg_view*)xdg_surface->data : NULL;

        if (xdg_view) {
            // Focus the view
            if (xdg_view->xdg_surface && xdg_view->xdg_surface->toplevel) {
                wlr_xdg_toplevel_set_activated(xdg_view->xdg_surface->toplevel, true);
            }
            if (xdg_view->scene_tree) {
                wlr_scene_node_raise_to_top(&xdg_view->scene_tree->node);
            }
            // CRITICAL: Give keyboard focus
            keyboard = wlr_seat_get_keyboard(server->seat);
            if (keyboard) {
                wlr_seat_keyboard_notify_enter(server->seat, surface,
                    keyboard->keycodes, keyboard->num_keycodes, &keyboard->modifiers);
            }
            LOG_DEBUG("[FOCUS] XDG surface focused");
            
            // Meta+click for move/resize (compositor-driven, manual scene node manipulation)
            if (meta_pressed) {
                if (event->button == BTN_LEFT) {
                    // Meta+Left = Move
                    LOG_INFO("[Cursor] Meta+Left: Starting manual window move");
                    server->grab.view = xdg_view;
                    server->grab.mode = INTERACTIVE_MOVE;
                    server->grab.start_x = server->cursor->x;
                    server->grab.start_y = server->cursor->y;
                    server->grab.view_start_x = xdg_view->scene_tree->node.x;
                    server->grab.view_start_y = xdg_view->scene_tree->node.y;
                } else if (event->button == BTN_RIGHT) {
                    // Meta+Right = Resize
                    LOG_INFO("[Cursor] Meta+Right: Starting manual window resize");
                    server->grab.view = xdg_view;
                    server->grab.mode = INTERACTIVE_RESIZE;
                    server->grab.start_x = server->cursor->x;
                    server->grab.start_y = server->cursor->y;
                    // Get current size from xdg surface geometry
                    struct wlr_box geo = xdg_view->xdg_surface->current.geometry;
                    server->grab.view_start_w = geo.width > 0 ? geo.width : 800;
                    server->grab.view_start_h = geo.height > 0 ? geo.height : 600;
                }
                return;  // Don't process further for Meta+click
            }
        }

        // Also check XWayland
        if (!xdg_view && server->xwayland) {
            struct wlr_xwayland_surface *xsurface = wlr_xwayland_surface_try_from_wlr_surface(surface);
            if (xsurface && xsurface->data) {
                struct havel_xwayland_view *xwayland_view = (struct havel_xwayland_view*)xsurface->data;
                wlr_xwayland_surface_activate(xsurface, true);
                wlr_scene_node_raise_to_top(&xwayland_view->scene_tree->node);
                // CRITICAL: Give keyboard focus
                keyboard = wlr_seat_get_keyboard(server->seat);
                if (keyboard) {
                    wlr_seat_keyboard_notify_enter(server->seat, surface,
                        keyboard->keycodes, keyboard->num_keycodes, &keyboard->modifiers);
                }
                LOG_DEBUG("[FOCUS] XWayland surface focused");
            }
        }
    }

    // Notify decoration plugin of button press (for click handling)
    havel_cpp_on_pointer_decoration_button(server->cpp_server, event->button, true,
                                           (int)server->cursor->x, (int)server->cursor->y);
    havel_cpp_on_pointer_button(server->cpp_server, event->button, true, server->cursor->x, server->cursor->y);
}

static void server_cursor_axis(struct wl_listener *listener, void *data) {
    struct havel_wlr_server *server = wl_container_of(listener, server, cursor_axis);
    struct wlr_pointer_axis_event *event = data;
    wlr_seat_pointer_notify_axis(server->seat, event->time_msec, event->orientation,
        event->delta, event->delta_discrete, event->source, event->relative_direction);
}

static void server_cursor_frame(struct wl_listener *listener, void *data) {
    struct havel_wlr_server *server = wl_container_of(listener, server, cursor_frame);
    wlr_seat_pointer_notify_frame(server->seat);
}

// ============================================================================
// Server Lifecycle
// ============================================================================

havel_wlr_server_t* havel_wlr_create(void) {
    // Initialize logger first
    logger_init(LOG_DEBUG);
    
    wlr_log_init(WLR_DEBUG, NULL);

    struct havel_wlr_server *server_local = calloc(1, sizeof(*server));
    server = server_local;  // Set global server pointer for overlay access

    wl_list_init(&server->outputs);
    server->active_workspace = 0;
    server->grab.mode = INTERACTIVE_NONE;  // Initialize grab state

    LOG_DEBUG("Initializing havel_wlr_server");

    // Register callbacks before creating C++ server
    havel_cpp_register_view_callbacks(
        cpp_impl_view_set_position,
        cpp_impl_view_set_size,
        cpp_impl_view_focus,
        cpp_impl_view_raise,
        cpp_impl_view_get_geometry,
        cpp_impl_view_close,
        cpp_impl_view_set_fullscreen,
        cpp_impl_view_minimize
    );
    havel_cpp_register_workspace_callbacks(
        cpp_impl_workspace_arrange,
        cpp_impl_workspace_set_active
    );
    havel_cpp_register_server_callbacks(
        cpp_impl_server_quit,
        cpp_impl_server_spawn
    );

    // Create C++ server
    server->cpp_server = havel_cpp_server_create();
    havel_cpp_server_set_native_handle(server->cpp_server, server);

    server->display = wl_display_create();
    if (!server->display) {
        havel_cpp_server_destroy(server->cpp_server);
        free(server);
        return NULL;
    }

    server->backend = wlr_backend_autocreate(wl_display_get_event_loop(server->display), &server->session);
    if (!server->backend) {
        wl_display_destroy(server->display);
        havel_cpp_server_destroy(server->cpp_server);
        free(server);
        return NULL;
    }

    if (server->session) {
        LOG_INFO("[SESSION] Session acquired (for VT switching)");
    } else {
        LOG_WARN("[SESSION] No session available (VT switching disabled)");
    }

    server->renderer = wlr_renderer_autocreate(server->backend);
    if (!server->renderer) {
        LOG_ERROR("[RENDERER] Failed to create renderer (no GPU/DRM access)");
        LOG_ERROR("[RENDERER] Try setting WLR_RENDERER=pixman for software rendering");
        LOG_ERROR("[RENDERER] Or ensure you have proper GPU drivers installed");
        wlr_backend_destroy(server->backend);
        wl_display_destroy(server->display);
        havel_cpp_server_destroy(server->cpp_server);
        free(server);
        return NULL;
    }

    wlr_renderer_init_wl_display(server->renderer, server->display);

    // Initialize text input manager (IME support)
    // This requires wl_display to be created
    havel_cpp_server_init_text_input(server->cpp_server, server->display);

    // Initialize screen capture (PipeWire/screencopy)
    // This requires wl_display and output_layout to be created
    // Will be initialized after output_layout creation

    server->allocator = wlr_allocator_autocreate(server->backend, server->renderer);
    if (!server->allocator) {
        LOG_ERROR("[ALLOCATOR] Failed to create allocator");
        LOG_ERROR("[ALLOCATOR] This usually means no DRM/GPU access is available");
        LOG_ERROR("[ALLOCATOR] Try:");
        LOG_ERROR("[ALLOCATOR]   - Running on a real GPU with proper drivers");
        LOG_ERROR("[ALLOCATOR]   - Setting WLR_RENDERER=pixman for software rendering");
        LOG_ERROR("[ALLOCATOR]   - Using WLR_BACKENDS=x11 or wayland for nested mode");
        wlr_backend_destroy(server->backend);
        wl_display_destroy(server->display);
        havel_cpp_server_destroy(server->cpp_server);
        free(server);
        return NULL;
    }
    LOG_INFO("[ALLOCATOR] Allocator created successfully");
    server->compositor = wlr_compositor_create(server->display, 5, server->renderer);
    struct wlr_subcompositor *sub = wlr_subcompositor_create(server->display);
    fprintf(stderr, "subcompositor_create: %p\n", (void *)sub);
    // wl_shm is auto-created by wlroots 0.20 with renderer - don't create twice
    wlr_data_device_manager_create(server->display);

    server->output_layout = wlr_output_layout_create(server->display);
    server->scene = wlr_scene_create();

    // Initialize screen capture (PipeWire/screencopy support)
    // This enables screen sharing in browsers and recording apps
    // screencapture::initialize(server->display, server->output_layout);

    // Initialize xdg-desktop-portal integration
    // This enables browser screen sharing (Firefox, Chrome) via D-Bus
    // portal::initialize(server->display);

    // Background color is now handled by wallpaper plugin via output_frame handler
    // No static background rect needed - plugin draws per-output backgrounds

    // Create global workspace trees (shared across all outputs)
    for (uint32_t i = 0; i < HAVEL_WORKSPACE_COUNT; ++i) {
        server->workspaces[i] = wlr_scene_tree_create(&server->scene->tree);
        // Enable only active workspace initially
        wlr_scene_node_set_enabled(&server->workspaces[i]->node, (i == server->active_workspace));
        LOG_INFO("[WORKSPACE] Global workspace %u tree created, enabled=%d", i, (i == server->active_workspace));
    }

    // Create overlay layer (raised to top for Alt-Tab, Overview, etc.)
    server->overlay_layer = wlr_scene_tree_create(&server->scene->tree);
    wlr_scene_node_raise_to_top(&server->overlay_layer->node);
    wlr_scene_node_set_enabled(&server->overlay_layer->node, false);  // Hidden by default
    LOG_INFO("[Overlay] Overlay layer created (raised to top)");

    // Pass overlay layer to C++ server for plugin rendering
    havel_cpp_server_set_overlay_layer(server->cpp_server, server->overlay_layer);

    // Create XDG output manager (required by waybar and other clients)
    wlr_xdg_output_manager_v1_create(server->display, server->output_layout);
    LOG_INFO("[XDG Output] xdg_output_manager_v1 created");

    server->xdg_shell = wlr_xdg_shell_create(server->display, 6);
    server->new_xdg_toplevel.notify = server_new_xdg_toplevel;
    wl_signal_add(&server->xdg_shell->events.new_toplevel, &server->new_xdg_toplevel);
    server->new_xdg_surface.notify = server_new_xdg_surface;
    wl_signal_add(&server->xdg_shell->events.new_surface, &server->new_xdg_surface);

    // Initialize layer-shell v1 (for waybar, notifications, etc.)
    server->layer_shell = wlr_layer_shell_v1_create(server->display, 1);
    if (!server->layer_shell) {
        LOG_WARN("[LayerShell] Failed to create layer_shell_v1");
    } else {
        LOG_INFO("[LayerShell] layer_shell_v1 initialized");
        server->new_layer_surface.notify = server_new_layer_surface;
        wl_signal_add(&server->layer_shell->events.new_surface, &server->new_layer_surface);
    }

    // Create XDG decoration manager (for server-side window decorations)
    wlr_server_decoration_manager_create(server->display);
    LOG_INFO("[Decoration] server_decoration_manager created");

    // Create XDG activation manager (for window activation/urgency hints)
    wlr_xdg_activation_v1_create(server->display);
    LOG_INFO("[Activation] xdg_activation_v1 created");

    // Create primary selection device manager (for clipboard)
    wlr_primary_selection_v1_device_manager_create(server->display);
    LOG_INFO("[Primary Selection] primary_selection_v1_device_manager created");

    // Initialize gamma control v1 manager
    server->gamma_control_manager = wlr_gamma_control_manager_v1_create(server->display);
    if (!server->gamma_control_manager) {
        LOG_WARN("[Gamma] Failed to create gamma_control_manager_v1");
    } else {
        LOG_INFO("[Gamma] gamma_control_manager_v1 initialized");
    }

    server->xwayland = wlr_xwayland_create(server->display, server->compositor, true);
    if (server->xwayland) {
        server->new_xwayland_surface.notify = server_new_xwayland_surface;
        wl_signal_add(&server->xwayland->events.new_surface, &server->new_xwayland_surface);
        setenv("DISPLAY", server->xwayland->display_name, true);
    }

    server->new_output.notify = server_new_output;
    wl_signal_add(&server->backend->events.new_output, &server->new_output);

    server->seat = wlr_seat_create(server->display, "seat0");

    server->cursor = wlr_cursor_create();
    wlr_cursor_attach_output_layout(server->cursor, server->output_layout);
    server->cursor_mgr = wlr_xcursor_manager_create(NULL, 24);

    server->cursor_motion.notify = server_cursor_motion;
    wl_signal_add(&server->cursor->events.motion, &server->cursor_motion);

    server->cursor_motion_absolute.notify = server_cursor_motion_absolute;
    wl_signal_add(&server->cursor->events.motion_absolute, &server->cursor_motion_absolute);

    server->cursor_button.notify = server_cursor_button;
    wl_signal_add(&server->cursor->events.button, &server->cursor_button);

    server->cursor_axis.notify = server_cursor_axis;
    wl_signal_add(&server->cursor->events.axis, &server->cursor_axis);

    server->cursor_frame.notify = server_cursor_frame;
    wl_signal_add(&server->cursor->events.frame, &server->cursor_frame);

    server->new_input.notify = server_new_input;
    wl_signal_add(&server->backend->events.new_input, &server->new_input);

    return server;
}

void havel_wlr_destroy(havel_wlr_server_t *server) {
    if (!server) return;

    if (server->display) {
        wl_display_destroy_clients(server->display);
    }

    if (server->backend) {
        wlr_backend_destroy(server->backend);
    }

    if (server->display) {
        wl_display_destroy(server->display);
    }

    havel_cpp_server_destroy(server->cpp_server);
    free(server);
}

int havel_wlr_run(havel_wlr_server_t *server) {
    if (!server) return 1;

    // Set global pointer for quit functionality
    g_running_server = server;

    const char *socket = wl_display_add_socket_auto(server->display);
    if (!socket) {
        LOG_ERROR("Failed to create wayland socket");
        return 1;
    }

    if (!wlr_backend_start(server->backend)) {
        LOG_ERROR("Failed to start backend");
        return 1;
    }

    setenv("WAYLAND_DISPLAY", socket, true);
    LOG_INFO("=== Havel Compositor running on %s ===", socket);
    printf("Havel Compositor running on %s\n", socket);

    wl_display_run(server->display);
    
    // Clear global pointer
    g_running_server = NULL;
    
    LOG_INFO("Havel Compositor shutting down");
    return 0;
}

// ============================================================================
// Gamma/Temperature/Brightness Control (COMBINED LUT)
// ============================================================================

/**
 * Convert color temperature (Kelvin) to RGB multipliers.
 * Uses simplified Planckian locus approximation.
 */
static void kelvin_to_rgb_mult(int kelvin, float *r_mult, float *g_mult, float *b_mult) {
    float temp = kelvin / 100.0f;
    float r, g, b;

    // Red channel
    if (temp <= 66.0f) {
        r = 1.0f;
    } else {
        r = 1.294f * powf(temp - 60.0f, -0.133f);
    }

    // Green channel
    if (temp <= 66.0f) {
        g = 0.994f * powf(temp, 0.170f);
    } else {
        g = 1.129f * powf(temp - 60.0f, -0.082f);
    }

    // Blue channel
    if (temp >= 66.0f) {
        b = 1.0f;
    } else if (temp <= 19.0f) {
        b = 0.0f;
    } else {
        b = 0.543f * powf(temp - 10.0f, 0.443f);
    }

    // Clamp multipliers to valid range
    *r_mult = fmaxf(0.0f, fminf(1.0f, r));
    *g_mult = fmaxf(0.0f, fminf(1.0f, g));
    *b_mult = fmaxf(0.0f, fminf(1.0f, b));
}

/**
 * Generate and apply combined gamma LUT.
 * Combines: gamma_curve × brightness × temperature_rgb
 * 
 * Safety:
 * - Allocates LUT once per output (not per-frame)
 * - Clamps values to [0, 1] before 16-bit cast to prevent overflow
 * - Uses dirty flag to avoid redundant regeneration
 * 
 * wlroots 0.20 API: Uses wlr_output_state to apply gamma LUT
 */
static void havel_output_apply_gamma(struct havel_output *output) {
    if (!output || !output->output) return;

    size_t gamma_size = wlr_output_get_gamma_size(output->output);
    if (gamma_size == 0 || gamma_size != output->gamma_ramp_size) {
        return;  // Output doesn't support gamma or size mismatch
    }

    if (!output->gamma_ramp_red || !output->gamma_ramp_green || !output->gamma_ramp_blue) {
        return;  // LUT buffers not allocated
    }

    // Calculate RGB multipliers from temperature
    float r_mult, g_mult, b_mult;
    kelvin_to_rgb_mult(output->temperature, &r_mult, &g_mult, &b_mult);

    // Generate combined LUT: gamma_curve(value) × brightness × temperature_rgb
    float inv_gamma = 1.0f / output->gamma;

    for (size_t i = 0; i < gamma_size; i++) {
        // Normalize to [0, 1]
        float value = (float)i / (float)(gamma_size - 1);

        // Apply gamma correction
        value = powf(value, inv_gamma);

        // Apply brightness
        value *= output->brightness;

        // CRITICAL: Clamp BEFORE converting to 16-bit to prevent overflow
        // This handles cases where brightness > 1.0 or gamma < 1.0
        value = fmaxf(0.0f, fminf(1.0f, value));

        // Apply temperature multipliers and clamp again
        float r_value = fmaxf(0.0f, fminf(1.0f, value * r_mult));
        float g_value = fmaxf(0.0f, fminf(1.0f, value * g_mult));
        float b_value = fmaxf(0.0f, fminf(1.0f, value * b_mult));

        // Convert to 16-bit (safe now after clamping)
        output->gamma_ramp_red[i] = (uint16_t)(r_value * 0xFFFF);
        output->gamma_ramp_green[i] = (uint16_t)(g_value * 0xFFFF);
        output->gamma_ramp_blue[i] = (uint16_t)(b_value * 0xFFFF);
    }

    output->gamma_ramp_dirty = false;

    // Apply LUT via wlroots 0.20 gamma_control_v1 API
    if (!output->server->gamma_control_manager) {
        LOG_WARN("[OUTPUT] %s gamma_control_manager not available", output->output->name);
        return;
    }

    struct wlr_gamma_control_v1 *gamma_control = wlr_gamma_control_manager_v1_get_control(
        output->server->gamma_control_manager, output->output);

    if (gamma_control) {
        // wlroots 0.20 requires using wlr_output_state to apply gamma
        struct wlr_output_state state;
        wlr_output_state_init(&state);
        
        // Set the gamma LUT via the gamma control
        if (wlr_gamma_control_v1_apply(gamma_control, &state)) {
            // Apply the output state to commit the gamma change
            wlr_output_commit_state(output->output, &state);
            LOG_DEBUG("[OUTPUT] %s gamma LUT applied (gamma=%.2f, temp=%dK, brightness=%.2f)",
                      output->output->name, output->gamma, output->temperature, output->brightness);
        } else {
            LOG_WARN("[OUTPUT] %s failed to apply gamma LUT", output->output->name);
        }
        
        wlr_output_state_finish(&state);
    } else {
        // No gamma control available, output may not support it
        LOG_DEBUG("[OUTPUT] %s no gamma control available, LUT generated but not applied",
                  output->output->name);
    }
}

void havel_wlr_set_gamma(havel_wlr_server_t *server, float gamma) {
    if (!server) return;

    struct havel_output *output;
    wl_list_for_each(output, &server->outputs, link) {
        output->gamma = gamma;
        havel_output_apply_gamma(output);
    }

    LOG_INFO("[Gamma] Set to %.2f on all outputs", gamma);
}

void havel_wlr_set_temperature(havel_wlr_server_t *server, int kelvin) {
    if (!server) return;

    struct havel_output *output;
    wl_list_for_each(output, &server->outputs, link) {
        output->temperature = kelvin;
        havel_output_apply_gamma(output);
    }

    LOG_INFO("[Temperature] Set to %dK on all outputs", kelvin);
}

void havel_wlr_set_brightness(havel_wlr_server_t *server, float brightness) {
    if (!server) return;

    struct havel_output *output;
    wl_list_for_each(output, &server->outputs, link) {
        output->brightness = brightness;
        havel_output_apply_gamma(output);
    }

    LOG_INFO("[Brightness] Set to %.2f on all outputs", brightness);
}

// ============================================================================
// Texture Access for Alt-Tab Thumbnails
// ============================================================================

#include <wlr/render/gles2.h>

uint32_t havel_get_view_texture_id(void* c_view) {
    struct havel_xdg_view* view = (struct havel_xdg_view*)c_view;
    if (!view) return 0;
    
    struct wlr_surface* surface = view->xdg_surface->surface;
    if (!surface || !wlr_surface_has_buffer(surface)) return 0;
    
    struct wlr_texture* texture = wlr_surface_get_texture(surface);
    if (!texture) return 0;
    
    // Get GL texture ID from wlr_gles2_texture
    struct wlr_gles2_texture_attribs attribs;
    wlr_gles2_texture_get_attribs(texture, &attribs);
    return attribs.tex;
}

int havel_get_view_texture_width(void* c_view) {
    struct havel_xdg_view* view = (struct havel_xdg_view*)c_view;
    if (!view) return 0;
    return view->xdg_surface->surface->current.width;
}

int havel_get_view_texture_height(void* c_view) {
    struct havel_xdg_view* view = (struct havel_xdg_view*)c_view;
    if (!view) return 0;
    return view->xdg_surface->surface->current.height;
}
