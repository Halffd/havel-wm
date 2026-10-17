// Enable unstable wlroots features (layer-shell, etc.)
// Note: WLR_USE_UNSTABLE is defined in CMakeLists.txt

#include <wm/wlr_bridge.h>
#include <Logger.h>
#include <wm/render_c.h>
#include <core/LoadingScreen.h>

#include <stdbool.h>
#include <stdint.h>
#include <drm_fourcc.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>

// Helper: Get monotonic time in milliseconds
static uint64_t get_monotonic_time_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000ULL + (uint64_t)ts.tv_nsec / 1000000ULL;
}
#include <sys/vt.h>
#include <sys/ioctl.h>
#include <fcntl.h>

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
#include <wlr/types/wlr_damage_ring.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_pointer.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_subcompositor.h>
#include <wlr/types/wlr_xcursor_manager.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/types/wlr_layer_shell_v1.h>
#include <wlr/types/wlr_xdg_output_v1.h>
#include <wlr/types/wlr_output_management_v1.h>
#include <wlr/types/wlr_server_decoration.h>
#include <wlr/types/wlr_xdg_activation_v1.h>
#include <wlr/types/wlr_primary_selection_v1.h>
#include <wlr/types/wlr_screencopy_v1.h>
#include <wlr/types/wlr_foreign_toplevel_management_v1.h>
#include <wlr/types/wlr_pointer_constraints_v1.h>
#include <wlr/types/wlr_relative_pointer_v1.h>
#include <wlr/types/wlr_idle_inhibit_v1.h>
#include <wlr/types/wlr_idle_notify_v1.h>
#include <wlr/types/wlr_virtual_pointer_v1.h>
#include <wlr/types/wlr_virtual_keyboard_v1.h>
#include <wlr/types/wlr_data_control_v1.h>
#include <wlr/types/wlr_input_method_v2.h>
#include <wlr/types/wlr_text_input_v3.h>
#include <wlr/types/wlr_cursor_shape_v1.h>
#include <wlr/types/wlr_linux_dmabuf_v1.h>
#include <wlr/types/wlr_presentation_time.h>
#include <wlr/types/wlr_tablet_v2.h>
#include <wlr/types/wlr_fractional_scale_v1.h>
#include <wlr/types/wlr_export_dmabuf_v1.h>
#include <wlr/types/wlr_color_management_v1.h>
#include <wlr/types/wlr_ext_foreign_toplevel_list_v1.h>
#include <wlr/util/log.h>
#include <wlr/xwayland.h>

#include <xkbcommon/xkbcommon.h>
#include <xkbcommon/xkbcommon-keysyms.h>

// Gamma control v1 protocol
#include <wlr/types/wlr_gamma_control_v1.h>

// Session/VT switching
#include <wlr/backend/session.h>

#include <wm/scene/SceneGraph.hpp>

#define HAVEL_WORKSPACE_COUNT 10

// ============================================================================
// C++ → C Callback Storage
// ============================================================================

// Global running server pointer (for quit functionality)
struct havel_wlr_server *g_running_server = NULL;

// C → C++ Callback Storage (extern for C++ access)
cpp_view_set_position_fn g_view_set_position = NULL;
cpp_view_set_size_fn g_view_set_size = NULL;
cpp_view_focus_fn g_view_focus = NULL;
cpp_view_raise_fn g_view_raise = NULL;
cpp_view_get_geometry_fn g_view_get_geometry = NULL;
cpp_view_close_fn g_view_close = NULL;
cpp_view_set_fullscreen_fn g_view_set_fullscreen = NULL;
cpp_view_minimize_fn g_view_minimize = NULL;
cpp_workspace_arrange_fn g_workspace_arrange = NULL;
cpp_workspace_set_active_fn g_workspace_set_active = NULL;
cpp_server_quit_fn g_server_quit = NULL;
cpp_server_spawn_fn g_server_spawn = NULL;

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

void havel_wlr_quit(void) {
    if (g_server_quit) {
        g_server_quit();
    }
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
    // Full-output background rect: wlroots 0.20 has no scene background
    // color API (that arrived in 0.21), so the wallpaper plugin's color
    // is rendered as the scene's bottom-most rect, created here BEFORE
    // the workspace trees so it stays first child.
    struct wlr_scene_rect *bg_rect;
    // Wallpaper image: a tree created BETWEEN bg_rect and the workspace
    // trees, so the image renders above the solid background colour and
    // below every window. Updated via havel_wlr_set_wallpaper.
    struct wlr_scene_tree *wallpaper_tree;
    struct wlr_scene_buffer *wallpaper_buffer;
    uint32_t *wallpaper_pixels;   // last-set image, ARGB8888 (owned copy)
    int wallpaper_w, wallpaper_h;

    struct wlr_xdg_shell *xdg_shell;
    struct wl_listener new_xdg_toplevel;
    struct wl_listener new_xdg_surface;

    // Overlay scene layer (for Alt-Tab, Overview, etc.)
    struct wlr_scene_tree *overlay_layer;

    // Positions scene outputs from the output layout (set at scene
    // creation; each scene output is registered with it below).
    struct wlr_scene_output_layout *scene_output_layout;

    struct wlr_xwayland *xwayland;
    struct wl_listener new_xwayland_surface;

    // Layer-shell v1 (for waybar, notifications, etc.)
    struct wlr_layer_shell_v1 *layer_shell;
    struct wl_listener new_layer_surface;

    // Foreign toplevel management v1 (for taskbars, window switchers)
    struct wlr_foreign_toplevel_manager_v1 *foreign_toplevel_manager;

    // Pointer constraints v1 (for games)
    struct wlr_pointer_constraints_v1 *pointer_constraints;
    struct wl_listener pointer_constraint;

    // Relative pointer v1 (for games)
    struct wlr_relative_pointer_manager_v1 *relative_pointer_manager;

    // Idle inhibit v1 (prevent screensaver)
    struct wlr_idle_inhibit_manager_v1 *idle_inhibit_manager;

    // Idle notify v1 (screensaver support)
    struct wlr_idle_notifier_v1 *idle_notifier;

    // Virtual pointer v1 (remote desktop)
    struct wlr_virtual_pointer_manager_v1 *virtual_pointer_manager;

    // Virtual keyboard v1 (input injection for tests/remote control)
    struct wlr_virtual_keyboard_manager_v1 *virtual_keyboard_manager;

    // Data control v1 (clipboard management)
    struct wlr_data_control_manager_v1 *data_control_manager;

    // Input method v2 (IME)
    struct wlr_input_method_manager_v2 *input_method_manager;

    // Text input v3 (text input)
    struct wlr_text_input_manager_v3 *text_input_manager;

    // Cursor shape v1 (cursor customization)
    struct wlr_cursor_shape_manager_v1 *cursor_shape_manager;

    // Linux DMA-BUF v1 (buffer sharing)
    struct wlr_linux_dmabuf_v1 *linux_dmabuf;

    // Presentation time (frame timing)
    struct wlr_presentation *presentation;

    struct wlr_tablet_manager_v2 *tablet_manager;

    // Fractional scale v1 (high DPI)
    struct wlr_fractional_scale_manager_v1 *fractional_scale_manager;

    // Export DMA-BUF v1 (screen capture)
    struct wlr_export_dmabuf_manager_v1 *export_dmabuf_manager;

    // Color management v1 (color calibration)
    struct wlr_color_manager_v1 *color_manager;

    // Extended foreign toplevel list v1
    struct wlr_ext_foreign_toplevel_list_v1 *ext_foreign_toplevel_list;

    struct wlr_seat *seat;
    struct wlr_cursor *cursor;
    struct wlr_xcursor_manager *cursor_mgr;

    struct wl_listener new_output;
    struct wl_listener session_active;
    struct wl_listener new_input;
    struct wl_listener new_virtual_keyboard;

    struct wl_listener cursor_motion;
    struct wl_listener cursor_motion_absolute;
    struct wl_listener cursor_button;
    struct wl_listener cursor_axis;
    struct wl_listener cursor_frame;
    // Cursor image sources: the seat tells us a client wants a cursor
    // surface, the shape manager gives named shapes.
    struct wl_listener request_set_cursor;
    struct wl_listener request_set_shape;
    // Force software cursors on every output (wlr_output_lock_software_cursors).
    // Balanced: lock once when turning on, unlock once when off.
    bool software_cursor_forced;

    uint32_t active_workspace;
    struct wl_list outputs; // havel_output::link
    struct wl_list layer_surfaces; // havel_layer_surface::link (for usable-area recompute)

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
    
    // Performance metrics
    uint64_t frame_count;      // Total frames rendered
    float current_fps;         // Current FPS (updated every second)
    uint64_t startup_time;     // Monotonic time at startup (for uptime calculation)
};

struct havel_output {
    struct wlr_output *output;
    struct wlr_scene_output *scene_output;
    struct wl_listener frame;
    struct wl_listener needs_frame;
    struct wl_listener destroy;
    struct wl_listener damage;   // software-cursor / backend damage

    struct havel_wlr_server *server;
    bool is_primary;
    bool cursor_mode_logged;   // one-shot hardware/software cursor report
    bool cursor_software_locked;
    struct wl_list link;

    // True once an enable commit succeeded. Native DRM boots with the
    // session not yet VT-active: atomic modeset commits fail EPERM
    // (the X server on the previous VT still holds DRM master) until
    // seatd activates the session and switches VT - outputs are retried
    // on the session's active signal (see session_active_cb).
    bool enable_ok;
    
    // Per-output workspace tracking
    uint32_t active_workspace;  // This output's active workspace

    // Gamma control v1 manager
    struct wlr_gamma_control_manager_v1 *gamma_control_manager;

    // Gamma/temperature/brightness/zoom state
    float gamma;
    int temperature;
    float brightness;
    float zoom;  // Per-monitor zoom level
    float zoom_center_x;  // Cursor X for cursor-centered zoom (output-local)
    float zoom_center_y;  // Cursor Y for cursor-centered zoom (output-local)
    float prev_zoom;      // Previous zoom level for calculating offset

    // Gamma LUT buffers (allocated once per output)
    uint16_t *gamma_ramp_red;
    uint16_t *gamma_ramp_green;
    uint16_t *gamma_ramp_blue;
    size_t gamma_ramp_size;
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

// View-kind tag: MUST be the first member of every C view wrapper struct.
// The cpp_impl_view_* callbacks receive an untyped nativeHandle and read
// this tag before casting, so XWayland views stop being reinterpreted as
// XDG views (wild-pointer deref on focus/close/fullscreen/minimize).
enum havel_view_kind {
    HAVEL_VIEW_XDG = 0x58444756,      // "XDGV"
    HAVEL_VIEW_XWAYLAND = 0x58574C56, // "XWLV"
};

struct havel_xdg_view {
    enum havel_view_kind kind;  // must stay first: tag read before cast
    struct wlr_xdg_surface *xdg_surface;
    struct wlr_scene_tree *scene_tree;
    struct havel_wlr_server *server;

    // NO workspace_id - C++ owns this
    // NO mapped flag - C++ owns this
    // NO geometry - C++ owns this

    // Only wlroots handles and C++ opaque pointer
    void *cpp_view;  // Opaque pointer to C++ View object
    void *scene_graph_view;  // Scene graph View (SceneView*)
    bool destroyed;  // Guard: cleanup runs exactly once (toplevel destroy
                     // fires before xdg_surface destroy on client teardown)

    struct wl_listener map;
    struct wl_listener unmap;
    struct wl_listener destroy;          // xdg_surface->events.destroy
    struct wl_listener toplevel_destroy; // toplevel->events.destroy (fires first)
    struct wl_listener surface_commit;  // Waits for first commit before sending configure
    struct wl_listener set_app_id;      // Called when app_id is set
    struct wl_listener set_title;       // Called when title is set
    struct wl_listener request_move;    // Window move request
    struct wl_listener request_resize;  // Window resize request
    struct wl_listener request_minimize; // Minimize request
    struct wl_listener request_maximize; // Maximize request
    struct wl_listener request_fullscreen; // Fullscreen request
};

struct havel_xwayland_view {
    enum havel_view_kind kind;  // must stay first: tag read before cast
    struct wlr_xwayland_surface *xsurface;
    struct wlr_scene_tree *scene_tree;
    struct havel_wlr_server *server;

    // NO workspace_id - C++ owns this
    // NO geometry - C++ owns this

    void *cpp_view;  // Opaque pointer to C++ View object
    void *scene_graph_view;  // Scene graph View (SceneView*)

    struct wl_listener destroy;
    // An X window's wl_surface is NOT valid at new-surface time - it
    // arrives with the associate event and leaves with dissociate
    // ("struct wlr_surface is valid once the associate event is
    // emitted" - wlr/xwayland.h). Creating the scene surface before
    // association passes NULL and segfaults the compositor (observed
    // live: SIGSEGV in wlr_scene_surface_create from
    // server_new_xwayland_surface the moment an X client mapped).
    struct wl_listener associate;
    struct wl_listener dissociate;
    bool scene_surface_created;

    // Cascade spawn: X clients (xred) silently request (40,40) with
    // their OWN configure event, which wlroots honors by updating
    // wlr_xwayland_surface->position at map commit. Without a late
    // override hook, the compositor's cascade position gets clobbered
    // by every such configure (observed live: all five windows landed
    // at (40,40) despite the clue in the log showing stacking spawn
    // #0..4 at (40,40),(80,80),(120,120),(160,160),(200,200)).
    // The cascade offset is stored here; the configure handler re-
    // applies it after honoring the client-side resize.
    int pending_cascade_x;  // -1 while unset (no cascade)
    int pending_cascade_y;
    struct wl_listener request_configure;  // xsurface->events.request_configure
};

// Forward function declarations
static void xdg_view_set_position(struct havel_xdg_view *view, int x, int y);
static void xwayland_view_set_position(struct havel_xwayland_view *view, int x, int y);

// ============================================================================
// Callback Implementations (C++ → C)
// ============================================================================

static void cpp_impl_view_set_position(void* view, int x, int y) {
    if (!view) return;

    // Dispatch on the view-kind tag: scene_tree sits at the same offset in
    // both structs, but position logic differs (xdg_view_set_position is
    // XDG-specific; XWayland positions via xsurface).
    enum havel_view_kind kind = *(enum havel_view_kind*)view;
    if (kind == HAVEL_VIEW_XDG) {
        struct havel_xdg_view *xdg_view = (struct havel_xdg_view*)view;
        if (xdg_view->scene_tree) {
            xdg_view_set_position(xdg_view, x, y);
        }
    } else if (kind == HAVEL_VIEW_XWAYLAND) {
        struct havel_xwayland_view *xwl = (struct havel_xwayland_view*)view;
        // Cascade spawn means this position wins over any client
        // configure that lands later (see xwayland_view_handle_configure).
        xwl->pending_cascade_x = x;
        xwl->pending_cascade_y = y;
        wlr_xwayland_surface_configure(xwl->xsurface, x, y,
                                       xwl->xsurface->width, xwl->xsurface->height);
        if (xwl->scene_tree) {
            wlr_scene_node_set_position(&xwl->scene_tree->node, x, y);
        }
    }
}

static void cpp_impl_view_set_size(void* view, int w, int h) {
    if (!view) return;

    enum havel_view_kind kind = *(enum havel_view_kind*)view;
    if (kind == HAVEL_VIEW_XDG) {
        struct havel_xdg_view *xdg_view = (struct havel_xdg_view*)view;
        if (xdg_view->xdg_surface && xdg_view->xdg_surface->toplevel) {
            wlr_xdg_toplevel_set_size(xdg_view->xdg_surface->toplevel, w, h);
        }
    } else if (kind == HAVEL_VIEW_XWAYLAND) {
        struct havel_xwayland_view *xwl = (struct havel_xwayland_view*)view;
        if (xwl->xsurface) {
            int x = xwl->pending_cascade_x >= 0 ? xwl->pending_cascade_x : xwl->xsurface->x;
            int y = xwl->pending_cascade_y >= 0 ? xwl->pending_cascade_y : xwl->xsurface->y;
            wlr_xwayland_surface_configure(xwl->xsurface, x, y, w, h);
        }
    }
}

static void cpp_impl_view_focus(void* view) {
    LOG_INFO("[C] cpp_impl_view_focus: %p", view);
    if (!view) return;

    // View-kind tag is the first member of BOTH wrapper structs (see
    // havel_view_kind), so it can be read before knowing which struct
    // this is. XWayland views used to be reinterpreted as XDG views
    // here — wild pointer deref on focus of any X11 window.
    enum havel_view_kind kind = *(enum havel_view_kind*)view;
    struct wlr_surface *surface = NULL;
    struct havel_wlr_server *view_server = NULL;

    if (kind == HAVEL_VIEW_XDG) {
        struct havel_xdg_view *xdg_view = (struct havel_xdg_view*)view;
        if (xdg_view->xdg_surface) {
            surface = xdg_view->xdg_surface->surface;
            view_server = xdg_view->server;
        }
    } else if (kind == HAVEL_VIEW_XWAYLAND) {
        struct havel_xwayland_view *xwl = (struct havel_xwayland_view*)view;
        if (xwl->xsurface) {
            surface = xwl->xsurface->surface;
            view_server = xwl->server;
        }
    }

    if (surface && view_server) {
        struct wlr_seat *seat = view_server->seat;
        struct wlr_keyboard *keyboard = wlr_seat_get_keyboard(seat);
        LOG_INFO("[C] Calling wlr_seat_keyboard_notify_enter");
        if (keyboard) {
            wlr_seat_keyboard_notify_enter(seat, surface,
                keyboard->keycodes, keyboard->num_keycodes, &keyboard->modifiers);
            LOG_INFO("[C] wlr_seat_keyboard_notify_enter complete");
        } else {
            LOG_WARN("[C] No keyboard available");
        }
    } else {
        LOG_WARN("[C] Could not resolve surface for focus");
    }
}

static void cpp_impl_view_raise(void* view) {
    LOG_INFO("[C] cpp_impl_view_raise: %p", view);
    if (!view) return;

    // scene_tree sits at the same offset in both wrapper structs (after the
    // kind tag + surface pointer), but resolve through the right struct for
    // clarity and future-proofing.
    enum havel_view_kind kind = *(enum havel_view_kind*)view;
    struct wlr_scene_tree *scene_tree = NULL;

    if (kind == HAVEL_VIEW_XDG) {
        scene_tree = ((struct havel_xdg_view*)view)->scene_tree;
    } else if (kind == HAVEL_VIEW_XWAYLAND) {
        scene_tree = ((struct havel_xwayland_view*)view)->scene_tree;
    }

    if (!scene_tree) {
        LOG_WARN("[C] Invalid view or scene_tree");
        return;
    }
    LOG_INFO("[C] Calling wlr_scene_node_raise_to_top");
    wlr_scene_node_raise_to_top(&scene_tree->node);
    LOG_INFO("[C] wlr_scene_node_raise_to_top complete");
}

static void cpp_impl_view_get_geometry(void* view, int* x, int* y, int* w, int* h) {
    if (!view) return;

    enum havel_view_kind kind = *(enum havel_view_kind*)view;
    if (x) *x = 0;
    if (y) *y = 0;
    if (w) *w = 0;
    if (h) *h = 0;

    struct wlr_scene_tree *scene_tree = NULL;

    if (kind == HAVEL_VIEW_XDG) {
        struct havel_xdg_view *xdg_view = (struct havel_xdg_view*)view;

        // Get position from scene node (single source of truth)
        scene_tree = xdg_view->scene_tree;

        // Get size from XDG surface geometry
        if (xdg_view->xdg_surface && xdg_view->xdg_surface->surface) {
            struct wlr_box geo = xdg_view->xdg_surface->current.geometry;
            if (w) *w = geo.width;
            if (h) *h = geo.height;
        }
    } else if (kind == HAVEL_VIEW_XWAYLAND) {
        struct havel_xwayland_view *xwl = (struct havel_xwayland_view*)view;

        scene_tree = xwl->scene_tree;

        // XWayland surface has no xdg geometry; use the surface's current size
        if (xwl->xsurface) {
            if (w) *w = xwl->xsurface->width;
            if (h) *h = xwl->xsurface->height;
        }
    }

    if (scene_tree) {
        if (x) *x = scene_tree->node.x;
        if (y) *y = scene_tree->node.y;
    }
}

static void cpp_impl_view_close(void* view) {
    if (!view) return;

    enum havel_view_kind kind = *(enum havel_view_kind*)view;
    if (kind == HAVEL_VIEW_XDG) {
        struct havel_xdg_view *xdg_view = (struct havel_xdg_view*)view;
        if (xdg_view->xdg_surface && xdg_view->xdg_surface->toplevel) {
            wlr_xdg_toplevel_send_close(xdg_view->xdg_surface->toplevel);
        }
    } else if (kind == HAVEL_VIEW_XWAYLAND) {
        struct havel_xwayland_view *xwl = (struct havel_xwayland_view*)view;
        if (xwl->xsurface) {
            wlr_xwayland_surface_close(xwl->xsurface);
        }
    }
}

static void cpp_impl_view_set_fullscreen(void* view, bool fullscreen) {
    if (!view) return;

    enum havel_view_kind kind = *(enum havel_view_kind*)view;
    if (kind == HAVEL_VIEW_XDG) {
        struct havel_xdg_view *xdg_view = (struct havel_xdg_view*)view;
        if (xdg_view->xdg_surface && xdg_view->xdg_surface->toplevel) {
            wlr_xdg_toplevel_set_fullscreen(xdg_view->xdg_surface->toplevel, fullscreen);
        }
    } else if (kind == HAVEL_VIEW_XWAYLAND) {
        struct havel_xwayland_view *xwl = (struct havel_xwayland_view*)view;
        if (xwl->xsurface) {
            wlr_xwayland_surface_set_fullscreen(xwl->xsurface, fullscreen);
        }
    }
}

static void cpp_impl_view_minimize(void* view) {
    if (!view) return;

    // Hide the scene node to minimize (doesn't unmap, just hides).
    // scene_tree is at the same offset in both wrapper structs.
    enum havel_view_kind kind = *(enum havel_view_kind*)view;
    struct wlr_scene_tree *scene_tree = NULL;

    if (kind == HAVEL_VIEW_XDG) {
        scene_tree = ((struct havel_xdg_view*)view)->scene_tree;
    } else if (kind == HAVEL_VIEW_XWAYLAND) {
        scene_tree = ((struct havel_xwayland_view*)view)->scene_tree;
    }

    if (scene_tree) {
        wlr_scene_node_set_enabled(&scene_tree->node, false);
    }
}

static void cpp_impl_workspace_arrange(uint32_t workspace_id) {
    // Triggered from C++ when layout needs to be recalculated
    // The actual arrangement logic is now in C++
    (void)workspace_id;
}

// Workspace animation state
static struct {
    uint32_t from_workspace;
    uint32_t to_workspace;
    uint64_t start_time;
    uint32_t duration_ms;
    bool animating;
} g_workspace_anim = {0};

// Switch workspace on ALL outputs (global/workspace switching)
static void workspace_set_active_global(uint32_t workspace_id) {
    if (!g_running_server || workspace_id >= HAVEL_WORKSPACE_COUNT) return;
    
    uint32_t old_workspace = g_running_server->active_workspace;
    
    // Start workspace switch animation
    g_workspace_anim.from_workspace = old_workspace;
    g_workspace_anim.to_workspace = workspace_id;
    g_workspace_anim.start_time = get_monotonic_time_ms();
    g_workspace_anim.duration_ms = 250;  // 250ms crossfade
    g_workspace_anim.animating = (old_workspace != workspace_id);
    
    // Enable both workspaces during animation
    if (g_workspace_anim.animating) {
        wlr_scene_node_set_enabled(&g_running_server->workspaces[old_workspace]->node, true);
        wlr_scene_node_set_enabled(&g_running_server->workspaces[workspace_id]->node, true);
        
        // Set old workspace to fade out (will be updated in frame handler)
        LOG_INFO("[WORKSPACE] Starting workspace switch animation %u -> %u", 
                 old_workspace, workspace_id);
    } else {
        // No animation needed
        for (uint32_t i = 0; i < HAVEL_WORKSPACE_COUNT; i++) {
            wlr_scene_node_set_enabled(&g_running_server->workspaces[i]->node, false);
        }
        wlr_scene_node_set_enabled(&g_running_server->workspaces[workspace_id]->node, true);
    }
    
    g_running_server->active_workspace = workspace_id;
    
    // Update all outputs to match global workspace
    struct havel_output *output;
    wl_list_for_each(output, &g_running_server->outputs, link) {
        output->active_workspace = workspace_id;
    }
    
    LOG_INFO("[WORKSPACE] Global workspace switched to %u", workspace_id);
}

// Switch workspace on a SPECIFIC output (per-monitor workspace switching)
static void workspace_set_active_output(struct havel_output *havel_out, uint32_t workspace_id) {
    if (!havel_out || !g_running_server || workspace_id >= HAVEL_WORKSPACE_COUNT) return;
    
    // Update this output's active workspace
    havel_out->active_workspace = workspace_id;
    
    // For per-monitor workspaces, we would need per-output workspace trees
    // For now, update global workspace if this is the primary output
    if (havel_out->is_primary) {
        workspace_set_active_global(workspace_id);
    }
    
    LOG_INFO("[WORKSPACE] Output %s switched to workspace %u", 
             havel_out->output->name, workspace_id);
}

static void cpp_impl_workspace_set_active(uint32_t workspace_id) {
    // Update C layer workspace state (global switching)
    workspace_set_active_global(workspace_id);
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
    if (!command) return;
    
    // Parse command into argv (simple space-splitting, no shell)
    // Note: This is a simple parser; for complex commands with quotes/escaping,
    // the caller should use the IPC JSON argv array format instead.
    char* cmd_copy = strdup(command);
    if (!cmd_copy) {
        LOG_ERROR("[Spawn] strdup failed");
        return;
    }
    
    char* argv[64];
    int argc = 0;
    char* token = strtok(cmd_copy, " \t\n");
    while (token && argc < 63) {
        argv[argc++] = token;
        token = strtok(NULL, " \t\n");
    }
    argv[argc] = NULL;
    
    if (argc == 0) {
        free(cmd_copy);
        return;
    }
    
    pid_t pid = fork();
    if (pid < 0) {
        LOG_ERROR("[Spawn] Fork failed for command: %s", command);
        free(cmd_copy);
        return;
    }
    if (pid == 0) {
        // Child process - execute command directly, no shell
        execvp(argv[0], argv);
        _exit(127);  // exec failed
    }
    // Parent process
    LOG_INFO("[Spawn] Launched: %s (PID: %d)", command, pid);
    free(cmd_copy);
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
    if (!view || !view->scene_tree || !view->xsurface) return;
    // Client-initiated placements: xwayland honors the (x,y) request ->
    // compositor forwards it to the client (waiting for the configure
    // event handler to ack the final position).
    wlr_xwayland_surface_configure(view->xsurface, x, y,
                                   view->xsurface->width, view->xsurface->height);
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

    // Hide loading screen on first window map
    loading_screen_hide();

    // Notify C++ layer - C++ owns mapped state
    havel_cpp_on_view_mapped(view->server->cpp_server, view->cpp_view);

    // Apply a maximize request that arrived before initialization:
    // clients (firefox) set toplevel state, then commit, so the request
    // signal fired while scheduling a configure would have asserted
    // (see xdg_handle_request_maximize). At map the surface is
    // initialized and the configure is legal. Same for fullscreen
    // (browsers with --kiosk / --start-fullscreen).
    if (view->xdg_surface->toplevel) {
        struct wlr_xdg_toplevel *toplevel = view->xdg_surface->toplevel;
        if (toplevel->requested.maximized) {
            LOG_INFO("[XDG] Applying deferred maximize for %p", (void*)view);
            wlr_xdg_toplevel_set_maximized(toplevel, true);
            if (!wl_list_empty(&view->server->outputs)) {
                struct havel_output *output =
                    wl_container_of(view->server->outputs.next, output, link);
                if (output && output->output) {
                    wlr_xdg_toplevel_set_size(toplevel,
                                              output->output->width, output->output->height);
                }
            }
        }
        if (toplevel->requested.fullscreen) {
            LOG_INFO("[XDG] Applying deferred fullscreen for %p", (void*)view);
            wlr_xdg_toplevel_set_fullscreen(toplevel, true);
        }
    }

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

    // Propagate into the C++ layer (View + core Window + IPC broadcast)
    if (title && view->server && view->server->cpp_server) {
        havel_cpp_on_view_title_changed(view->server->cpp_server, view->cpp_view, title);
    }
}

// NEW: Window move request handler - THIS IS WHY WINDOWS CAN NOW MOVE!
static void xdg_handle_request_move(struct wl_listener *listener, void *data) {
    struct havel_xdg_view *view = wl_container_of(listener, view, request_move);
    struct havel_wlr_server *server = view->server;
    
    if (!server->seat || !server->cursor) return;
    
    LOG_INFO("[XDG] Move request for view %p", (void*)view);
    
    // Start interactive move using the same grab system as Meta+click
    server->grab.view = view;
    server->grab.mode = INTERACTIVE_MOVE;
    server->grab.start_x = server->cursor->x;
    server->grab.start_y = server->cursor->y;
    server->grab.view_start_x = view->scene_tree->node.x;
    server->grab.view_start_y = view->scene_tree->node.y;
}

// NEW: Window resize request handler
static void xdg_handle_request_resize(struct wl_listener *listener, void *data) {
    struct havel_xdg_view *view = wl_container_of(listener, view, request_resize);
    struct havel_wlr_server *server = view->server;
    struct wlr_xdg_toplevel_resize_event *event = data;
    
    if (!server->seat || !server->cursor) return;
    
    LOG_INFO("[XDG] Resize request for view %p (edges: %d)", (void*)view, event->edges);
    
    // Start interactive resize using the same grab system as Meta+click
    server->grab.view = view;
    server->grab.mode = INTERACTIVE_RESIZE;
    server->grab.start_x = server->cursor->x;
    server->grab.start_y = server->cursor->y;
    // Get current size from xdg surface geometry
    struct wlr_box geo = view->xdg_surface->current.geometry;
    server->grab.view_start_w = geo.width > 0 ? geo.width : 800;
    server->grab.view_start_h = geo.height > 0 ? geo.height : 600;
    
    // wlroots 0.20: Interactive resize handled by our custom grab system
    // wlr_cursor_start_interactive() doesn't exist in wlroots 0.20
}

// NEW: Maximize request handler
static void xdg_handle_request_maximize(struct wl_listener *listener, void *data) {
    struct havel_xdg_view *view = wl_container_of(listener, view, request_maximize);

    if (!view || !view->xdg_surface->toplevel) return;

    // Maximize requests can arrive before the surface's first commit -
    // clients set toplevel state, THEN commit (firefox does exactly
    // this). Scheduling a configure on an uninitialized surface
    // asserts in wlroots and kills the compositor (observed live).
    // wlroots records the request in toplevel->requested; the map
    // handler applies it once the surface is initialized.
    if (!view->xdg_surface->initialized) {
        LOG_INFO("[XDG] Maximize request for %p before initialization - "
                 "will apply on map", (void*)view);
        return;
    }

    LOG_INFO("[XDG] Maximize request for view %p", (void*)view);

    // Toggle maximized state and resize window
    struct wlr_xdg_toplevel *toplevel = view->xdg_surface->toplevel;
    bool maximized = toplevel->current.maximized;

    if (!maximized) {
        // Maximize: set to output size
        wlr_xdg_toplevel_set_maximized(toplevel, true);
        // Get output size
        struct havel_output *output = NULL;
        if (!wl_list_empty(&view->server->outputs)) {
            struct wl_list *next = view->server->outputs.next;
            output = wl_container_of(next, output, link);
        }
        if (output && output->output) {
            wlr_xdg_toplevel_set_size(toplevel, output->output->width, output->output->height);
        }
    } else {
        // Restore: clear maximized state, size will be restored by client
        wlr_xdg_toplevel_set_maximized(toplevel, false);
    }
}

// NEW: Minimize request handler
static void xdg_handle_request_minimize(struct wl_listener *listener, void *data) {
    struct havel_xdg_view *view = wl_container_of(listener, view, request_minimize);

    if (!view || !view->xdg_surface->toplevel) return;

    LOG_INFO("[XDG] Minimize request for view %p", (void*)view);

    // Hide the scene node to minimize
    if (view->scene_tree) {
        wlr_scene_node_set_enabled(&view->scene_tree->node, false);
    }
    
    // Notify C++ layer
    if (view->cpp_view) {
        havel_cpp_on_view_unmapped(view->server->cpp_server, view->cpp_view);
    }
}

// NEW: Fullscreen request handler
static void xdg_handle_request_fullscreen(struct wl_listener *listener, void *data) {
    struct havel_xdg_view *view = wl_container_of(listener, view, request_fullscreen);

    if (!view || !view->xdg_surface->toplevel) return;

    // Same pre-initialization hazard as request_maximize: clients can
    // request fullscreen before their first commit (browsers with
    // --kiosk / --start-fullscreen do exactly this) and
    // wlr_xdg_toplevel_set_fullscreen schedules a configure that
    // asserts on an uninitialized surface. wlroots records the request
    // in toplevel->requested; the map handler applies it.
    if (!view->xdg_surface->initialized) {
        LOG_INFO("[XDG] Fullscreen request for %p before initialization - "
                 "will apply on map", (void*)view);
        return;
    }

    LOG_INFO("[XDG] Fullscreen request for view %p", (void*)view);

    // Toggle fullscreen state
    struct wlr_xdg_toplevel *toplevel = view->xdg_surface->toplevel;
    bool fullscreen = toplevel->current.fullscreen;
    wlr_xdg_toplevel_set_fullscreen(toplevel, !fullscreen);
}

static void xdg_view_cleanup(struct havel_xdg_view *view) {
    if (view->destroyed) return;
    view->destroyed = true;

    LOG_INFO("[XDG] DESTROY: %p (cpp_view=%p)", (void*)view, view->cpp_view);

    // CRITICAL: Save server pointer BEFORE any cleanup (needed for C++ notification)
    struct havel_wlr_server *server = view->server;
    void *cpp_view = view->cpp_view;

    // CRITICAL: Clear cpp_view pointer FIRST to prevent C++ from accessing freed memory
    view->cpp_view = NULL;

    // Remove toplevel-event listeners. On client teardown the toplevel
    // destroy signal fires BEFORE xdg_surface destroy (and wlroots
    // asserts in destroy_xdg_toplevel that request_* listeners are
    // gone) - removing them here covers both orderings via the
    // destroyed-flag guard.
    wl_list_remove(&view->set_app_id.link);
    wl_list_remove(&view->set_title.link);
    wl_list_remove(&view->request_move.link);
    wl_list_remove(&view->request_resize.link);
    wl_list_remove(&view->request_minimize.link);
    wl_list_remove(&view->request_maximize.link);
    wl_list_remove(&view->request_fullscreen.link);
    wl_list_remove(&view->toplevel_destroy.link);

    // Remove remaining listeners
    wl_list_remove(&view->map.link);
    wl_list_remove(&view->unmap.link);
    wl_list_remove(&view->destroy.link);

    // surface_commit may have already been removed by the commit handler
    if (view->surface_commit.notify != NULL) {
        wl_list_remove(&view->surface_commit.link);
    }

    // Notify C++ layer - C++ destroys its View object
    // DO NOT access view-> members after this point!
    if (cpp_view && server) {
        havel_cpp_on_view_destroyed(server->cpp_server, cpp_view);
    }

    // Destroy scene graph view (C owns this, not C++)
    if (view->scene_graph_view) {
        scene_view_destroy((SceneView*)view->scene_graph_view);
        view->scene_graph_view = NULL;
        LOG_INFO("[Scene] Scene graph view destroyed");
    }

    // Scene node is destroyed automatically by wlr_scene_xdg_surface_create
    // Do NOT manually destroy it

    LOG_INFO("[XDG] View freed");
    free(view);
}

// Toplevel destroy: fires first on client teardown, before wlroots
// asserts that request_* listeners are detached.
static void xdg_view_handle_toplevel_destroy(struct wl_listener *listener, void *data) {
    struct havel_xdg_view *view = wl_container_of(listener, view, toplevel_destroy);
    (void)data;
    xdg_view_cleanup(view);
}

// xdg_surface destroy: fires on other teardown paths (e.g. wl_surface
// role change). Idempotent via the destroyed flag.
static void xdg_view_handle_destroy(struct wl_listener *listener, void *data) {
    struct havel_xdg_view *view = wl_container_of(listener, view, destroy);
    (void)data;
    xdg_view_cleanup(view);
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
    view->kind = HAVEL_VIEW_XDG;
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

    LOG_INFO("[XDG] Adding toplevel destroy listener (fires before surface destroy)");
    view->toplevel_destroy.notify = xdg_view_handle_toplevel_destroy;
    wl_signal_add(&xdg_surface->toplevel->events.destroy, &view->toplevel_destroy);

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
    
    // Add move/resize request listeners - THIS IS WHY WINDOWS CAN'T MOVE!
    LOG_INFO("[XDG] Adding move request listener");
    view->request_move.notify = xdg_handle_request_move;
    wl_signal_add(&toplevel->events.request_move, &view->request_move);
    
    LOG_INFO("[XDG] Adding resize request listener");
    view->request_resize.notify = xdg_handle_request_resize;
    wl_signal_add(&toplevel->events.request_resize, &view->request_resize);
    
    LOG_INFO("[XDG] Adding minimize request listener");
    view->request_minimize.notify = xdg_handle_request_minimize;
    wl_signal_add(&toplevel->events.request_minimize, &view->request_minimize);
    
    LOG_INFO("[XDG] Adding maximize request listener");
    view->request_maximize.notify = xdg_handle_request_maximize;
    wl_signal_add(&toplevel->events.request_maximize, &view->request_maximize);
    
    LOG_INFO("[XDG] Adding fullscreen request listener");
    view->request_fullscreen.notify = xdg_handle_request_fullscreen;
    wl_signal_add(&toplevel->events.request_fullscreen, &view->request_fullscreen);

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

    // Create scene graph view
    view->scene_graph_view = NULL;
    if (server->cpp_server) {
        Scene* scene_graph = (Scene*)havel_cpp_get_scene_graph((struct havel_cpp_server*)server->cpp_server);
        if (scene_graph) {
            // Get active workspace from scene graph
            SceneOutput* output = scene_output_get_primary(scene_graph);
            if (output && server->active_workspace < SCENE_MAX_WORKSPACES) {
                SceneWorkspace* ws = scene_workspace_get(output, server->active_workspace);
                if (ws) {
                    // Create scene graph view
                    view->scene_graph_view = scene_view_create(ws, xdg_surface);
                    LOG_INFO("[Scene] Created scene graph view %p for XDG surface %p",
                             view->scene_graph_view, (void*)xdg_surface);
                }
            }
        }
    }

    LOG_INFO("[XDG] View setup complete for %p (cpp_view=%p, scene_graph_view=%p, parent=%p, appId=%s, title=%s)",
             (void*)view, view->cpp_view, view->scene_graph_view, (void*)parent, appId ? appId : "unknown", title ? title : "unknown");
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

    // CRITICAL: Save pointers BEFORE clearing
    struct havel_wlr_server *server = view->server;
    void *cpp_view = view->cpp_view;
    
    // CRITICAL: Clear cpp_view pointer FIRST to prevent C++ from accessing freed memory
    view->cpp_view = NULL;
    
    wl_list_remove(&view->destroy.link);
    wl_list_remove(&view->associate.link);
    wl_list_remove(&view->dissociate.link);
    wl_list_remove(&view->request_configure.link);

    // Notify C++ layer - C++ destroys View object
    // DO NOT access view-> members after this point!
    if (cpp_view && server) {
        havel_cpp_on_view_destroyed(server->cpp_server, cpp_view);
    }

    // Destroy scene graph view (C owns this, not C++)
    if (view->scene_graph_view) {
        scene_view_destroy((SceneView*)view->scene_graph_view);
        view->scene_graph_view = NULL;
        LOG_INFO("[Scene] Scene graph view destroyed");
    }

    free(view);
}

static void xwayland_view_handle_associate(struct wl_listener *listener, void *data) {
    struct havel_xwayland_view *view = wl_container_of(listener, view, associate);
    (void)data;

    // The listeners stay registered for the window's whole life: an X
    // window can dissociate and re-associate repeatedly.
    if (view->scene_surface_created) {
        return;
    }
    LOG_INFO("[XWayland] surface associated: binding scene surface");
    if (view->scene_tree && view->xsurface && view->xsurface->surface) {
        wlr_scene_surface_create(view->scene_tree, view->xsurface->surface);
        view->scene_surface_created = true;

        // There is no xwayland events.map: surfaces are always mapped
        // while they exist; the C++ layer is told now (spawn offset,
        // focus, thumbnail).
        if (view->cpp_view) {
            havel_cpp_on_view_mapped(view->server->cpp_server, view->cpp_view);
        }
    } else {
        LOG_WARN("[XWayland] associate fired without a usable surface");
    }
}

static void xwayland_view_handle_dissociate(struct wl_listener *listener, void *data) {
    struct havel_xwayland_view *view = wl_container_of(listener, view, dissociate);
    (void)data;
    LOG_INFO("[XWayland] surface dissociated (scene surface dies with it)");
    // The wlr_scene_surface created on the old wl_surface is destroyed
    // with it by wlroots. associate will fire again if the window
    // re-binds (the associate listener stays registered).
    view->scene_surface_created = false;
}

static void xwayland_view_handle_configure(struct wl_listener *listener, void *data) {
    struct havel_xwayland_view *view = wl_container_of(listener, view, request_configure);
    struct wlr_xwayland_surface_configure_event *event = data;

    // Cascade override: clients that set their own position (x,
    // otherwise 0) cannot fight the spawn cascade after map. The
    // compositor's stacked spawn offset wins for these xwayland
    // windows; the client-configured size (w,h) still applies.
    if (view->pending_cascade_x >= 0) {
        int cx = view->pending_cascade_x;
        int cy = view->pending_cascade_y;
        wlr_scene_node_set_position(&view->scene_tree->node, cx, cy);
        wlr_xwayland_surface_configure(view->xsurface, cx, cy,
                                       view->xsurface->width, view->xsurface->height);
        LOG_INFO("[XWayland] re-applying cascade %d,%d over client (%d,%d)",
                 cx, cy, event->x, event->y);
        return;
    }
    wlr_xwayland_surface_configure(view->xsurface, event->x, event->y,
                                   event->width, event->height);
}

static void server_new_xwayland_surface(struct wl_listener *listener, void *data) {
    struct havel_wlr_server *server = wl_container_of(listener, server, new_xwayland_surface);
    struct wlr_xwayland_surface *xsurface = data;

    if (xsurface->override_redirect) {
        return;
    }

    struct havel_xwayland_view *view = calloc(1, sizeof(*view));
    view->kind = HAVEL_VIEW_XWAYLAND;
    view->server = server;
    view->xsurface = xsurface;
    xsurface->data = view;
    // NO workspace_id in C - C++ owns this
    
    // Create scene tree first. The wl_surface is not valid yet on every
    // path - X windows bind theirs with the associate event (see the
    // associate/dissociate listeners above). The scene surface is
    // created either now (already associated) or on associate.
    view->scene_tree = wlr_scene_tree_create(&server->scene->tree);
    if (xsurface->surface) {
        wlr_scene_surface_create(view->scene_tree, xsurface->surface);
        view->scene_surface_created = true;
    }
    // Both lifecycle listeners register unconditionally: the associate
    // handler is guarded by scene_surface_created (no double-create),
    // and always linking keeps the destroy path's wl_list_remove safe.
    view->associate.notify = xwayland_view_handle_associate;
    wl_signal_add(&xsurface->events.associate, &view->associate);
    view->dissociate.notify = xwayland_view_handle_dissociate;
    wl_signal_add(&xsurface->events.dissociate, &view->dissociate);
    view->request_configure.notify = xwayland_view_handle_configure;
    wl_signal_add(&xsurface->events.request_configure, &view->request_configure);
    view->pending_cascade_x = -1;
    view->pending_cascade_y = -1;

    // Apply geometry: only when the X client requested a specific
    // position (not 0,0 and not the -1 sentinel). The cascade spawn
    // positions the scene node later; a client that asks for (40,40)
    // must not overwrite it on every map (observed live: five xred
    // windows all rendered at (40,40) - the fixed stacking cascade
    // offsets were all clobbered by this unconditional call).
    bool has_position = (xsurface->x != 0 || xsurface->y != 0) &&
                        (xsurface->x != -1 && xsurface->y != -1);
    bool position_valid = false;
    if (has_position) {
        struct wlr_output *wlr_out = wlr_output_layout_output_at(server->output_layout,
                                                               xsurface->x, xsurface->y);
        position_valid = (wlr_out != NULL);
        if (position_valid) {
            xwayland_view_set_position(view, xsurface->x, xsurface->y);
        }
    }

    // Get window metadata from XWayland surface
    const char* appId = xsurface->class ? xsurface->class : "";
    const char* title = xsurface->title ? xsurface->title : "";

    // Notify C++ layer - it creates the View object and owns all state
    view->cpp_view = havel_cpp_on_xdg_surface_new(server->cpp_server, view, server->active_workspace, appId, title);

    // Create scene graph view
    view->scene_graph_view = NULL;
    if (server->cpp_server) {
        Scene* scene_graph = (Scene*)havel_cpp_get_scene_graph((struct havel_cpp_server*)server->cpp_server);
        if (scene_graph) {
            SceneOutput* output = scene_output_get_primary(scene_graph);
            if (output && server->active_workspace < SCENE_MAX_WORKSPACES) {
                SceneWorkspace* ws = scene_workspace_get(output, server->active_workspace);
                if (ws) {
                    view->scene_graph_view = scene_view_create_xwayland(ws, xsurface);
                    LOG_INFO("[Scene] Created scene graph view %p for XWayland surface %p",
                             view->scene_graph_view, (void*)xsurface);
                }
            }
        }
    }

    view->destroy.notify = xwayland_view_handle_destroy;
    wl_signal_add(&xsurface->events.destroy, &view->destroy);

    LOG_INFO("[XWayland] View setup complete for %p (cpp_view=%p, scene_graph_view=%p, appId=%s, title=%s)",
             (void*)view, view->cpp_view, view->scene_graph_view, appId, title);
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
    // First-commit hook: the initial configure goes out on the layer
    // surface's first commit (see layer_surface_handle_commit) - at
    // new-surface time wlroots asserts surface->initialized is still
    // false, and map only fires after the client got its configure.
    struct wl_listener commit;
    bool configured;
    struct wl_list link;  // server->layer_surfaces (usable-area recompute)
};

// First commit: the layer surface has its initial state (anchors,
// desired size, exclusive zone) and wlroots has it `initialized` - the
// earliest legal moment for wlr_layer_surface_v1_configure. Waybar
// blocks on this configure before attaching its buffer (observed:
// "Timed out waiting for initial .configure" - surface torn down, no
// bar) and configuring any earlier trips the initialized assert (that
// crash was observed live too).
// Recompute the usable area (output minus every mapped layer surface's
// exclusive zone) and push it to the C++ workspaces via
// havel_cpp_set_output_geometry - that export existed but was never
// called, so C++ placement/maximize ran on an empty rect. Called on
// output enable and on layer map/unmap/destroy; waybar's top bar
// (exclusive zone 30) moves the usable area origin below the bar.
static void update_usable_area(struct havel_wlr_server *server) {
    struct havel_output *output = NULL;
    if (!wl_list_empty(&server->outputs)) {
        output = wl_container_of(server->outputs.next, output, link);
    }
    if (!output || !output->output || !output->enable_ok) return;

    struct wlr_box full_area = {0, 0, output->output->width, output->output->height};
    struct wlr_box usable_area = full_area;

    // Layer order matters: bottom layers consume usable area first
    // (exactly what wlr_scene_layer_surface_v1_configure computes).
    struct havel_layer_surface *lsurface;
    wl_list_for_each(lsurface, &server->layer_surfaces, link) {
        if (!lsurface->output || !lsurface->scene_layer_surface) continue;
        if (!lsurface->scene_layer_surface->layer_surface->surface->mapped) continue;
        wlr_scene_layer_surface_v1_configure(
            lsurface->scene_layer_surface, &full_area, &usable_area);
    }

    for (int ws = 0; ws < HAVEL_WORKSPACE_COUNT; ws++) {
        havel_cpp_set_output_geometry(server->cpp_server, ws,
                                      usable_area.x, usable_area.y,
                                      usable_area.width, usable_area.height);
    }
    LOG_INFO("[LayerShell] usable area now %dx%d+%d+%d",
             usable_area.width, usable_area.height,
             usable_area.x, usable_area.y);
}

static void layer_surface_handle_commit(struct wl_listener *listener, void *data) {
    struct havel_layer_surface *lsurface = wl_container_of(listener, lsurface, commit);
    (void)data;
    if (lsurface->configured || !lsurface->output) return;
    lsurface->configured = true;
    wl_list_remove(&lsurface->commit.link);

    struct wlr_output *output = lsurface->output;
    struct wlr_box full_area = {0, 0, output->width, output->height};
    struct wlr_box usable_area = {0, 0, output->width, output->height};
    wlr_scene_layer_surface_v1_configure(
        lsurface->scene_layer_surface, &full_area, &usable_area);
    LOG_INFO("[LayerShell] initial configure sent on first commit (%dx%d)",
             full_area.width, full_area.height);
}

static void layer_surface_handle_destroy(struct wl_listener *listener, void *data) {
    struct havel_layer_surface *lsurface = wl_container_of(listener, lsurface, destroy);

    LOG_INFO("[LayerShell] Surface destroyed");

    wl_list_remove(&lsurface->destroy.link);
    wl_list_remove(&lsurface->map.link);
    wl_list_remove(&lsurface->unmap.link);
    if (!lsurface->configured) {
        wl_list_remove(&lsurface->commit.link);
    }
    wl_list_remove(&lsurface->link);

    if (lsurface->scene_tree) {
        wlr_scene_node_destroy(&lsurface->scene_tree->node);
    }

    update_usable_area(lsurface->server);
    free(lsurface);
}

static void layer_surface_handle_map(struct wl_listener *listener, void *data) {
    struct havel_layer_surface *lsurface = wl_container_of(listener, lsurface, map);
    struct wlr_output *output = lsurface->output;

    LOG_INFO("[LayerShell] Surface mapped: namespace=%s, layer=%d",
             lsurface->scene_layer_surface->layer_surface->namespace,
             lsurface->scene_layer_surface->layer_surface->current.layer);

    // Recompute the usable area with this surface's exclusive zone -
    // also configures the surface within the fresh arrangement.
    update_usable_area(lsurface->server);

    // Raise to top when mapped
    wlr_scene_node_raise_to_top(&lsurface->scene_tree->node);
}

static void layer_surface_handle_unmap(struct wl_listener *listener, void *data) {
    struct havel_layer_surface *lsurface = wl_container_of(listener, lsurface, unmap);

    LOG_INFO("[LayerShell] Surface unmapped");
    // Its exclusive zone no longer applies.
    update_usable_area(lsurface->server);
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

    // Initial configure on the surface's FIRST COMMIT - not here: at
    // new-surface time wlroots asserts the surface is not yet
    // `initialized` (the assert crash was observed live).
    lsurface->commit.notify = layer_surface_handle_commit;
    wl_signal_add(&layer_surface->surface->events.commit, &lsurface->commit);
    wl_list_insert(&server->layer_surfaces, &lsurface->link);

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
// Pointer Constraint Handlers (for games)
// ============================================================================

static void handle_pointer_constraint(struct wl_listener *listener, void *data) {
    struct wlr_pointer_constraint_v1 *constraint = data;
    LOG_INFO("[PointerConstraint] New constraint: type=%d", constraint->type);
    
    // wlroots handles the constraint automatically
    // We just need to create it, wlroots does the rest
}

// ============================================================================
// Output Handlers
// ============================================================================

static void output_frame(struct wl_listener *listener, void *data) {
    struct havel_output *output = wl_container_of(listener, output, frame);
    struct havel_wlr_server *server = output->server;

    // Keep the overlay layer on top of everything. wlroots appends new
    // scene nodes (client surfaces, layer-shell surfaces) at the top,
    // so an overlay that was raised once at init ends up underneath
    // every window mapped afterwards - observed live: the Alt-Tab grid
    // was drawn but clients painted over it.
    if (server->overlay_layer) {
        wlr_scene_node_raise_to_top(&server->overlay_layer->node);
    }
    
    // Update workspace switch animation
    if (g_workspace_anim.animating) {
        uint64_t elapsed = get_monotonic_time_ms() - g_workspace_anim.start_time;
        float progress = (float)elapsed / (float)g_workspace_anim.duration_ms;
        
        if (progress >= 1.0f) {
            // Animation complete
            g_workspace_anim.animating = false;
            wlr_scene_node_set_enabled(&server->workspaces[g_workspace_anim.from_workspace]->node, false);
            LOG_INFO("[WORKSPACE] Workspace switch animation complete");
        } else {
            // Update opacity based on progress (ease-out cubic)
            float ease = 1.0f - (1.0f - progress) * (1.0f - progress) * (1.0f - progress);
            
            // New workspace fades in, old workspace fades out
            // Note: wlroots scene graph doesn't support per-node opacity directly
            // For now, just switch at the end of animation
            // Full implementation would require custom rendering
        }
    }
    
    // Frame timing for performance metrics
    static uint64_t lastFrameTime = 0;
    static uint32_t frameCount = 0;
    static float fps = 0.0f;
    
    uint64_t now = get_monotonic_time_ms();
    if (lastFrameTime > 0) {
        frameCount++;
        if (now - lastFrameTime >= 1000) {  // Update FPS every second
            fps = (float)frameCount * 1000.0f / (float)(now - lastFrameTime);
            frameCount = 0;
            lastFrameTime = now;
        }
    } else {
        lastFrameTime = now;
    }
    
    // Store FPS for stats API
    server->current_fps = fps;
    server->frame_count++;

    // Frame heartbeat: first frame and every 600th after (~10s at 60fps)
    static int frame_count = 0;
    frame_count++;
    if (frame_count == 1 || frame_count % 600 == 0) {
        LOG_INFO("[FRAME] Frame #%d on %s (enabled=%d, scene_output=%p, FPS=%.1f)",
                 frame_count, output->output->name,
                 output->output->enabled, output->scene_output, fps);
    }

    // Per-frame logging disabled to prevent log spam (60fps = 3600 lines/min)
    // Enable temporarily for debugging specific issues
    // LOG_INFO("[FRAME] %s: >>> START", output->output->name);

    // Update animations before rendering
    havel_cpp_update_animations(server->cpp_server);
    
    // Process IPC events for external tool communication
    havel_cpp_process_ipc_events(server->cpp_server);

    // Dispatch frame event to plugins (via C++ server) with actual output dimensions
    havel_cpp_dispatch_output_frame(server->cpp_server, output->output, output->scene_output,
                                     output->output->width, output->output->height, output->output->refresh);

    // Get background color from wallpaper plugin
    float bgR, bgG, bgB;
    havel_cpp_get_background_color(server->cpp_server, &bgR, &bgG, &bgB);
    // wlroots 0.20 has no scene background-color API (that arrived in
    // 0.21). The scene's first-child rect (created at server init)
    // renders the wallpaper color behind everything. The old TODO here
    // dropped the color entirely - the desktop rendered black.
    if (server->bg_rect) {
        float bg[4] = { bgR, bgG, bgB, 1.0f };
        bool color_changed = memcmp(server->bg_rect->color, bg, sizeof bg) != 0;
        bool size_changed = server->bg_rect->width != output->output->width ||
                            server->bg_rect->height != output->output->height;
        if (color_changed) {
            memcpy(server->bg_rect->color, bg, sizeof bg);
        }
        if (size_changed) {
            wlr_scene_rect_set_size(server->bg_rect,
                                    output->output->width, output->output->height);
        }
        if (color_changed || size_changed) {
            // Scene commits only redraw damaged regions: toggling the
            // node damages the rect's area so the new color lands.
            wlr_scene_node_set_enabled(&server->bg_rect->node, false);
            wlr_scene_node_set_enabled(&server->bg_rect->node, true);
        }
    }

    // Commit scene output using wlroots 0.20 API with cursor-centered zoom
    struct wlr_output_state state;
    wlr_output_state_init(&state);

    // Apply zoom as output scale transform
    if (output->zoom != 1.0f && output->zoom > 0.0f) {
        // Set scale for zoom (1.0 = normal, 2.0 = 2x zoom)
        wlr_output_state_set_scale(&state, output->zoom);
        
        // Apply cursor-centered zoom translation
        // When zooming, content under cursor should stay in place
        // Translation offset = cursor_pos * (1 - prev_zoom / new_zoom)
        if (output->zoom_center_x >= 0 && output->zoom_center_y >= 0 && 
            output->prev_zoom > 0) {
            float zoom_ratio = output->prev_zoom / output->zoom;
            float offset_x = output->zoom_center_x * (1.0f - zoom_ratio);
            float offset_y = output->zoom_center_y * (1.0f - zoom_ratio);
            
            // Apply offset to scene output position
            wlr_scene_output_set_position(output->scene_output, 
                                          (int)offset_x, (int)offset_y);
            
            LOG_DEBUG("[FRAME] %s: cursor-centered zoom offset (%.0f,%.0f)", 
                      output->output->name, offset_x, offset_y);
        }
    }

    const struct wlr_scene_output_state_options options = {
        .timer = NULL,
    };

    if (!output->scene_output) {
        LOG_ERROR("[FRAME] %s: scene_output is NULL!", output->output->name);
        wlr_output_state_finish(&state);
        return;
    }

    // Overlays render before the scene commit so they composite into the
    // frame. The target describes which output this is (scene box, index,
    // primary, whether the pointer is on it) - plugin overlays that target
    // one monitor need it.
    struct havel_overlay_target overlay_target = {0, 0, output->output->width,
                                                  output->output->height, 0, 1, 1};
    {
        // The output's own layout entry, not wlr_output_layout_get_box:
        // get_box only takes a layout-wide or output box and returned the
        // WHOLE layout rectangle for the primary (observed live: x=2560 on
        // a 2560-wide dual-head layout), which laid the overlay out
        // off-screen. The layout entry's x/y plus the output's mode size
        // is the output's box.
        struct wlr_output_layout_output *lo =
            wlr_output_layout_get(server->output_layout, output->output);
        if (lo) {
            overlay_target.x = lo->x;
            overlay_target.y = lo->y;
            overlay_target.w = output->output->width;
            overlay_target.h = output->output->height;
        }
        overlay_target.index = 0;
        struct havel_output *o;
        wl_list_for_each(o, &server->outputs, link) {
            if (o == output) break;
            overlay_target.index++;
        }
        overlay_target.primary = output->is_primary ? 1 : 0;
        // With no cursor yet, fall back to the primary output: "cursor"
        // must not mean "nowhere" before the pointer has moved.
        if (server->cursor) {
            struct wlr_output *cur_out = wlr_output_layout_output_at(
                server->output_layout, server->cursor->x, server->cursor->y);
            if (cur_out) {
                overlay_target.under_cursor = (cur_out == output->output) ? 1 : 0;
            } else {
                overlay_target.under_cursor = overlay_target.primary;
            }
        }
    }
    havel_cpp_draw_overlays(server->cpp_server, output->output->width, output->output->height,
                            &overlay_target);

    // Note: wlroots handles all actual screen rendering via wlr_scene_output_commit()
    // Our custom buffer import (SHM/DMA-BUF) is available for plugins that need
    // direct texture access (Alt-Tab thumbnails, screen capture, etc.)
    // To use custom rendering, implement wlr_renderer interface and replace
    // wlr_renderer_autocreate() in havel_wlr_create()

    // Commit with zoom transform applied via output state.
    // The scene MUST be built into the state before committing —
    // wlr_output_commit_state alone attaches no buffer, which blanks
    // the output. Previously the zoom path did exactly that.
    if (output->zoom != 1.0f && output->zoom > 0.0f) {
        LOG_DEBUG("[FRAME] %s: committing with zoom %.2f", output->output->name, output->zoom);
        if (!wlr_scene_output_build_state(output->scene_output, &state, &options)) {
            LOG_ERROR("[FRAME] %s: scene_output_build_state failed", output->output->name);
        } else if (!wlr_output_commit_state(output->output, &state)) {
            LOG_ERROR("[FRAME] %s: output_commit_state failed (zoom=%.2f)",
                      output->output->name, output->zoom);
        }
    } else {
        wlr_scene_output_commit(output->scene_output, &options);
    }
    wlr_output_state_finish(&state);

    // Honour a forced software cursor for outputs created after the switch.
    if (output->server->software_cursor_forced &&
        !output->cursor_software_locked) {
        wlr_output_lock_software_cursors(output->output, true);
        output->cursor_software_locked = true;
    }

    // Report once whether this output is driving a hardware cursor plane
    // or wlroots' software cursor (set during the commit above).
    if (!output->cursor_mode_logged) {
        output->cursor_mode_logged = true;
        LOG_INFO("[Cursor] %s: %s", output->output->name,
                 output->cursor_software_locked
                     ? "software cursor (forced)"
                     : (output->output->hardware_cursor
                            ? "hardware cursor plane"
                            : "software cursor (no plane or fallback)"));
    }

    // Drive the next frame. DRM outputs chain via page-flip; the
    // headless backend does not - without an explicit schedule the
    // frame loop runs exactly once and stalls (observed: Frame #1 then
    // silence). No-op when a frame is already pending.
    wlr_output_schedule_frame(output->output);
}

// The scene graph (or backend logic) damaged the output and wants a new
// frame committed. DRM chains frames via page-flip after a present, but
// the headless backend never self-schedules - without this hook its
// frame loop runs once and stalls.
// Software cursors (and some backend logic) damage the output OUTSIDE the
// scene graph: wlroots tells us about it here and expects us to repaint
// that region. Without this the cursor is composited at each new position
// but the previous one is never cleared - the pointer leaves a trail of
// stale sprites across the screen (observed on DRM: repeated left_ptr
// arrows in a capture after moving the pointer).
static void output_damage(struct wl_listener *listener, void *data) {
    struct havel_output *output = wl_container_of(listener, output, damage);
    struct wlr_output_event_damage *event = data;
    if (!output->scene_output || !event->damage) {
        return;
    }
    // Buffer-local coordinates, same space the scene's damage ring uses.
    wlr_damage_ring_add(&output->scene_output->damage_ring, event->damage);
    wlr_output_schedule_frame(output->output);
}

static void output_needs_frame(struct wl_listener *listener, void *data) {
    struct havel_output *output = wl_container_of(listener, output, needs_frame);
    (void)data;
    wlr_output_schedule_frame(output->output);
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
    wl_list_remove(&output->needs_frame.link);
    wl_list_remove(&output->destroy.link);
    wl_list_remove(&output->link);
    free(output);
}

// Forward: the enable logic (defined after server_new_output; also
// called by session_active_cb for the post-VT-switch retry).
static void output_try_enable(struct havel_output *output);
static void wallpaper_fit_output(struct havel_wlr_server *server);

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
    
    // Initialize per-output workspace to global active workspace
    output->active_workspace = server->active_workspace;

    // Initialize gamma/temperature/brightness/zoom state to defaults
    output->gamma = 1.0f;
    output->temperature = 6500;
    output->brightness = 1.0f;
    output->zoom = 1.0f;
    output->zoom_center_x = -1.0f;  // -1 = not set
    output->zoom_center_y = -1.0f;
    output->prev_zoom = 1.0f;

    // Allocate gamma LUT buffers once per output
    size_t gamma_size = wlr_output_get_gamma_size(wlr_output);
    if (gamma_size > 0) {
        output->gamma_ramp_size = gamma_size;
        output->gamma_ramp_red = calloc(gamma_size, sizeof(uint16_t));
        output->gamma_ramp_green = calloc(gamma_size, sizeof(uint16_t));
        output->gamma_ramp_blue = calloc(gamma_size, sizeof(uint16_t));
        LOG_INFO("[OUTPUT] Gamma LUT allocated for %s (size=%zu)", wlr_output->name, gamma_size);
    } else {
        output->gamma_ramp_size = 0;
        output->gamma_ramp_red = NULL;
        output->gamma_ramp_green = NULL;
        output->gamma_ramp_blue = NULL;
        LOG_WARN("[OUTPUT] %s does not support gamma control", wlr_output->name);
    }

    wl_list_insert(server->outputs.prev, &output->link);  // Append to list (not prepend)
    output->is_primary = wl_list_empty(&server->outputs) || (server->outputs.next == &output->link);

    LOG_DEBUG("[OUTPUT] %s is %s", wlr_output->name, output->is_primary ? "primary" : "secondary");


    output->frame.notify = output_frame;
    wl_signal_add(&wlr_output->events.frame, &output->frame);
    LOG_INFO("[OUTPUT] Frame handler installed for %s", wlr_output->name);

    output->needs_frame.notify = output_needs_frame;
    wl_signal_add(&wlr_output->events.needs_frame, &output->needs_frame);

    output->damage.notify = output_damage;
    wl_signal_add(&wlr_output->events.damage, &output->damage);

    output->destroy.notify = output_destroy;
    wl_signal_add(&wlr_output->events.destroy, &output->destroy);

    // A backend can announce the same output more than once (an enable or
    // mode change re-emits new_output on some backends). Adding it to the
    // layout again with add_auto parks it after everything already there,
    // which is how a two-output headless boot ended up with the outputs at
    // x=1280 and x=2560 and NOTHING at 0..1280 - every overlay drawn at the
    // scene origin fell outside the layout and no plugin output was ever
    // captured. Position each output once.
    if (wlr_output_layout_get(server->output_layout, wlr_output)) {
        LOG_DEBUG("[OUTPUT] %s already in the layout, keeping its position",
                  wlr_output->name);
    } else if (wl_list_empty(&server->outputs)) {
        // First monitor - position explicitly at (0,0). add_auto would
        // let wlroots re-position the output whenever the layout changes:
        // when the second output is manually placed at (1280,0), the
        // auto-placed first output gets moved to the right of everything
        // (observed live: HEADLESS-2 ended up at (2560,0) on a two-output
        // headless boot, leaving 0..1280 empty and every layout-relative
        // coordinate off by 1280).
        wlr_output_layout_add(server->output_layout, wlr_output, 0, 0);
    } else {
        // Second (or subsequent) monitor - position to the right of the
        // previous one.
        //
        // The new output is already in server->outputs by now (inserted
        // above), so reading the list tail measured the output against
        // ITSELF: wlr_output_layout_get_box on an output that is not in
        // the layout yet returns an empty box, every monitor after the
        // first was placed at (0,0) and they all overlapped (observed
        // live: "Positioned HEADLESS-1 at (0,0) to the right of
        // HEADLESS-1"). Walk the list for the output before this one.
        struct havel_output *prev = NULL;
        struct havel_output *o;
        wl_list_for_each(o, &server->outputs, link) {
            if (o == output) break;
            prev = o;
        }
        if (!prev) {
            wlr_output_layout_add(server->output_layout, wlr_output, 0, 0);
        } else {
            struct wlr_box prev_box;
            wlr_output_layout_get_box(server->output_layout, prev->output, &prev_box);
            int x = prev_box.x + prev_box.width;
            int y = 0;
            wlr_output_layout_add(server->output_layout, wlr_output, x, y);

            LOG_INFO("[OUTPUT] Positioned %s at (%d,%d) to the right of %s",
                     wlr_output->name, x, y, prev->output->name);
        }
    }

    // Register this scene output with the scene output layout so the
    // output's position in the layout drives the scene output position.
    // wlr_scene_attach_output_layout alone was not enough: it manages the
    // scene outputs IT creates, while this compositor creates its own
    // (wlr_scene_output_create above). Without the registration the scene
    // output stayed at (0,0) - probe evidence on a two-output boot:
    // scene_output->x,y=(0,0) for outputs the layout placed at 1280 and
    // 2560 - so everything addressed in layout coordinates rendered on
    // the same spot on every monitor, and overlays fell off-screen.
    if (server->scene_output_layout) {
        struct wlr_output_layout_output *lo =
            wlr_output_layout_get(server->output_layout, wlr_output);
        if (lo) {
            wlr_scene_output_layout_add_output(server->scene_output_layout, lo,
                                               output->scene_output);
            LOG_INFO("[OUTPUT] %s registered with the scene output layout (pos %d,%d)",
                     wlr_output->name, lo->x, lo->y);
        }
    }

    // Verify output state and scene graph
    LOG_INFO("[OUTPUT] %s setup COMPLETE (enabled=%d)",
             wlr_output->name, wlr_output->enabled);
    LOG_INFO("[SCENE] Scene root: %p, enabled=%d",
             (void*)&server->scene->tree, server->scene->tree.node.enabled);
    LOG_INFO("[SCENE_OUTPUT] %s: scene_output=%p",
             wlr_output->name, (void*)output->scene_output);

    // Enable the output and commit a mode (tinywl pattern). Without
    // this, outputs stay disabled and the frame loop never runs - the
    // headless boot showed exactly that (enabled=0, zero frames).
    // Physical DRM outputs get their preferred mode; mode-less outputs
    // (headless) get a custom 1280x720.
    output_try_enable(output);
}

static void output_try_enable(struct havel_output *output) {
    struct wlr_output *wlr_output = output->output;
    struct wlr_output_state state;
    wlr_output_state_init(&state);
    wlr_output_state_set_enabled(&state, true);

    struct wlr_output_mode *mode = wlr_output_preferred_mode(wlr_output);
    bool committed = false;
    if (mode != NULL) {
        wlr_output_state_set_mode(&state, mode);
        committed = wlr_output_commit_state(wlr_output, &state);
    } else {
        LOG_INFO("[OUTPUT] %s has no modes", wlr_output->name);
        // Mode-less outputs split two ways:
        // - headless NEEDS a custom mode (no size otherwise, and 0
        //   refresh means no frame timer at all)
        // - x11 already has a window size; an enable-only commit
        //   keeps it and succeeds where a resize commit fails
        //   (observed live: custom-mode commit failed on X11-1 and
        //   the output stayed disabled - zero frames, black window)
        committed = wlr_output_commit_state(wlr_output, &state);
        if (!committed) {
            LOG_INFO("[OUTPUT] %s: enable-only commit failed, trying custom 1280x720@60",
                     wlr_output->name);
            wlr_output_state_set_custom_mode(&state, 1280, 720, 60000);
            committed = wlr_output_commit_state(wlr_output, &state);
        }
    }

    if (!committed) {
        LOG_ERROR("[OUTPUT] %s: failed to commit enabled state", wlr_output->name);
    } else {
        output->enable_ok = true;
        LOG_INFO("[OUTPUT] %s enabled (mode committed)", wlr_output->name);

        // A software-cursor request can arrive before this output exists
        // (config.hv runs at startup), so honour it here as well as on
        // demand.
        if (output->server->software_cursor_forced && !output->cursor_software_locked) {
            wlr_output_lock_software_cursors(output->output, true);
            output->cursor_software_locked = true;
            LOG_INFO("[Cursor] %s: software cursor forced (requested before output existed)",
                     wlr_output->name);
        }
        // The splash was created before any output existed, so it ran on
        // hardcoded 1920x1080 geometry with every box parked at (0,0).
        // Now that a real mode is committed, give it the real size.
        if (loading_screen_is_visible()) {
            loading_screen_layout(wlr_output->width, wlr_output->height);
        }
        wallpaper_fit_output(output->server);

        // Publish the output geometry to the C++ workspaces (the export
        // existed but was never called - placement ran on an empty
        // rect). Layer surfaces shrink this later via their exclusive
        // zones (see update_usable_area).
        update_usable_area(output->server);
        // Kick the frame loop: DRM chains frames via page-flip, but
        // headless outputs need an explicit first present or
        // events.frame never fires. Commit the (empty) scene once to
        // present a buffer, then schedule the next frame.
        struct wlr_scene_output_state_options opts = {0};
        struct wlr_output_state scene_state;
        wlr_output_state_init(&scene_state);
        if (wlr_scene_output_build_state(output->scene_output, &scene_state, &opts) &&
            wlr_output_commit_state(wlr_output, &scene_state)) {
            LOG_INFO("[OUTPUT] %s: first frame presented", wlr_output->name);
            wlr_output_schedule_frame(wlr_output);
        } else {
            LOG_WARN("[OUTPUT] %s: first frame present failed", wlr_output->name);
        }
        wlr_output_state_finish(&scene_state);
    }
    wlr_output_state_finish(&state);
}

// Session VT activation: retry outputs whose enable commit failed during
// boot. With native DRM the first attempt can race the session
// activation - the X server on the previous VT still holds DRM master,
// so atomic modesets return EPERM (observed live: "Atomic commit failed:
// Permission denied" until the VT switch, then never retried - black
// console). wlroots signals activation here; retry while active.
static void session_active_cb(struct wl_listener *listener, void *data) {
    struct havel_wlr_server *server =
        wl_container_of(listener, server, session_active);
    bool active = server->session && server->session->active;
    LOG_INFO("[SESSION] session %s", active ? "active (VT acquired)" : "inactive");
    if (!active) return;
    struct havel_output *output;
    wl_list_for_each(output, &server->outputs, link) {
        if (!output->enable_ok && !output->output->enabled) {
            LOG_INFO("[OUTPUT] %s: retrying enable after session activation",
                     output->output->name);
            output_try_enable(output);
        }
    }
}

// ============================================================================
// Overlay Rendering (Scene Graph Based)
// ============================================================================
// (Alt-Tab overlay removed: AltTabPlugin in the C++ layer owns window
// switching end-to-end. See keyboard_handle_key for the dispatch note.)

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
        // xkb keymaps are indexed by evdev+8 (the X11 keycode offset);
        // wlroots hands us raw evdev codes here, so the offset must be
        // applied for the keysym/modifier lookup (observed live: Esc
        // arrived as keycode 1 and resolved to keysym 0 without it).
        xkb_state_update_key(keyboard->xkb_state, event->keycode + 8,
                            event->state == WL_KEYBOARD_KEY_STATE_PRESSED ? XKB_KEY_DOWN : XKB_KEY_UP);
    }

    if (event->state == WL_KEYBOARD_KEY_STATE_PRESSED) {
        const uint32_t keycode = event->keycode + 8;  // xkb evdev+8 index
        // Read modifiers from OUR xkb_state (updated above). wlroots's
        // keyboard->modifiers only refreshes on wlr_keyboard_notify_key,
        // which virtual keyboards never receive - so wlroots's copy stays
        // zero for injected keys (observed live: Meta held, mods=0).
        uint32_t modifiers = 0;
        if (keyboard->xkb_state) {
            xkb_mod_mask_t depressed = xkb_state_serialize_mods(
                keyboard->xkb_state, XKB_STATE_MODS_DEPRESSED);
            // xkb modifier bits: Shift=0 CapsLock=1 Ctrl=2 Alt=3 Mod2=4
            // Mod3=5 Mod4(Meta)=6 Mod5=7 - same layout the C++ layer uses
            modifiers = (uint32_t)depressed;
        } else {
            modifiers = keyboard->keyboard->modifiers.depressed;
        }

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

        // Check for Ctrl+Alt modifiers for VT switching
        xkb_mod_index_t ctrl_idx = xkb_keymap_mod_get_index(keyboard->keymap, XKB_MOD_NAME_CTRL);
        xkb_mod_index_t alt_idx = xkb_keymap_mod_get_index(keyboard->keymap, XKB_MOD_NAME_ALT);
        bool ctrl_pressed = (ctrl_idx != XKB_MOD_INVALID) &&
                           (xkb_state_mod_index_is_active(keyboard->xkb_state, ctrl_idx, XKB_STATE_MODS_DEPRESSED) > 0);
        bool alt_pressed = (alt_idx != XKB_MOD_INVALID) &&
                          (xkb_state_mod_index_is_active(keyboard->xkb_state, alt_idx, XKB_STATE_MODS_DEPRESSED) > 0);

        // VT Switching (Ctrl+Alt+F1..F12) - Direct ioctl call (no shell)
        if (ctrl_pressed && alt_pressed && keysym >= XKB_KEY_F1 && keysym <= XKB_KEY_F12) {
            unsigned int vt = keysym - XKB_KEY_F1 + 1;
            LOG_INFO("[VT] Switching to VT%u", vt);
            
            // Method 1: Try ioctl VT_ACTIVATE directly
            int console_fd = open("/dev/console", O_WRONLY);
            int ret = -1;
            if (console_fd >= 0) {
                ret = ioctl(console_fd, VT_ACTIVATE, vt);
                close(console_fd);
                if (ret == 0) {
                    LOG_INFO("[VT] Switched via ioctl");
                }
            }
            
            if (ret != 0 && server->session) {
                // Method 2: Fallback to wlroots session VT switch
                wlr_session_change_vt(server->session, vt);
            }
            
            if (ret != 0) {
                LOG_WARN("[VT] All VT switch methods failed for VT%u", vt);
            }
            
            return;  // Consume the event, don't forward
        }

        // Alt+Tab window switching is owned by AltTabPlugin in the C++
        // layer (thumbnails, alt-release select). Previously this C-side
        // 3-rect overlay intercepted Alt+Tab before the C++ dispatch and
        // shadowed the plugin completely; it was removed so the plugin
        // receives the keys through havel_cpp_on_key below.

        // Forward to C++ layer for keybindings/plugins
        LOG_INFO("[KEY] Pressed: keycode=%u keysym=0x%x mods=0x%x char='%c'", keycode, keysym, modifiers, key_char ? key_char : ' ');
        bool consumed = havel_cpp_on_key(server->cpp_server, keycode, true, modifiers, keysym, key_char, utf8_buffer);
        LOG_INFO("[KEY] Consumed=%d", consumed);

        // Desktop manager (logout binding, show-desktop) sees keys the
        // keybindings/plugins did not consume, keysym-based like the
        // C++ layer (it previously never received keys at all).
        if (!consumed) {
            havel_cpp_process_desktop_key(server->cpp_server, keysym, modifiers);
        }

        // Only forward to seat if not consumed by compositor
        if (!consumed) {
            wlr_seat_keyboard_notify_key(server->seat, event->time_msec, event->keycode, event->state);
        }
        return;
    }

    // Forward release to C++ layer too (push-to-zoom end, alt-tab
    // select-on-alt-release depend on it). Releases are never "consumed"
    // for seat purposes: the matching press was already swallowed, so the
    // client never saw a press and must not see an orphan release.
    const uint32_t keycode = event->keycode + 8;  // xkb evdev+8 index
    xkb_keysym_t keysym = 0;
    char key_char = 0;
    char utf8_buffer[8] = {0};
    if (keyboard->xkb_state) {
        keysym = xkb_state_key_get_one_sym(keyboard->xkb_state, keycode);
        int len = xkb_keysym_to_utf8(keysym, utf8_buffer, sizeof(utf8_buffer));
        if (len == 1 && utf8_buffer[0] >= 32 && utf8_buffer[0] <= 126) {
            key_char = utf8_buffer[0];
        }
    }
    // Same modifier source as the press path (our xkb_state - wlroots's
    // copy never updates for injected keys)
    uint32_t rel_modifiers = 0;
    if (keyboard->xkb_state) {
        rel_modifiers = (uint32_t)xkb_state_serialize_mods(
            keyboard->xkb_state, XKB_STATE_MODS_DEPRESSED);
    }
    (void)havel_cpp_on_key(server->cpp_server, keycode, false,
                            rel_modifiers,
                            keysym, key_char, utf8_buffer);

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

// A client created a virtual keyboard (zwp_virtual_keyboard_v1). These
// do NOT arrive through backend->events.new_input (that path is for
// session devices) - the manager emits its own signal. Route through
// the same server_new_keyboard path so injected keys drive the normal
// dispatch chain.
static void server_new_virtual_keyboard(struct wl_listener *listener, void *data) {
    struct havel_wlr_server *server =
        wl_container_of(listener, server, new_virtual_keyboard);
    struct wlr_virtual_keyboard_v1 *vk = data;

    LOG_INFO("[Input] New virtual keyboard (client-injected)");
    struct wlr_input_device *device = &vk->keyboard.base;

    // The client keymap arrives via the protocol AFTER creation (the
    // zwp_virtual_keyboard_v1.keymap request); by the time keys flow,
    // the keyboard->events.key signal fires regardless. Attach through
    // the same path as physical keyboards.
    server_new_keyboard(server, device);
}

static void server_new_input(struct wl_listener *listener, void *data) {
    struct havel_wlr_server *server = wl_container_of(listener, server, new_input);
    struct wlr_input_device *device = data;

    LOG_INFO("[Input] New input device: %s (type=%d)",
             device->name ? device->name : "(unnamed)", (int)device->type);

    switch (device->type) {
    case WLR_INPUT_DEVICE_KEYBOARD:
        server_new_keyboard(server, device);
        break;
    case WLR_INPUT_DEVICE_POINTER:
        server_new_pointer(server, device);
        break;
    case WLR_INPUT_DEVICE_TOUCH:
        // Touchscreen support
        LOG_INFO("[Input] New touchscreen: %s", device->name);
        break;
    case WLR_INPUT_DEVICE_TABLET_PAD:
        // Tablet pad (buttons/dials)
        LOG_INFO("[Input] New tablet pad: %s", device->name);
        break;
    case WLR_INPUT_DEVICE_SWITCH:
        // Laptop lid switch
        LOG_INFO("[Input] New switch: %s", device->name);
        break;
    default:
        // Check if it's a gamepad/joystick via device name
        if (device->name) {
            const char* name = device->name;
            if (strstr(name, "gamepad") || strstr(name, "controller") ||
                strstr(name, "joystick") || strstr(name, "Xbox") ||
                strstr(name, "PlayStation") || strstr(name, "DualShock") ||
                strstr(name, "DualSense") || strstr(name, "Steam")) {
                // Accepted by the seat as a plain input device; gamepad-
                // specific handling would need a real consumer first.
                LOG_INFO("[Input] New gamepad/controller: %s", device->name);
            }
        }
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

    // Process desktop events
    havel_cpp_process_desktop_motion(server->cpp_server,
                                      (int)server->cursor->x, (int)server->cursor->y);

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

    // Process desktop events
    havel_cpp_process_desktop_mouse(server->cpp_server, event->button,
                                     event->state == WL_POINTER_BUTTON_STATE_PRESSED,
                                     (int)server->cursor->x, (int)server->cursor->y);

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

    // Forward scroll to plugins too: the seat notification above only
    // reaches the focused client. Plugins (Alt-Tab grid, hot corners)
    // consume wheel as scroll ticks - positive vertical delta = down.
    if (event->orientation == WL_POINTER_AXIS_VERTICAL_SCROLL) {
        havel_cpp_on_pointer_axis(server->cpp_server, 0.0, event->delta);
    } else if (event->orientation == WL_POINTER_AXIS_HORIZONTAL_SCROLL) {
        havel_cpp_on_pointer_axis(server->cpp_server, event->delta, 0.0);
    }
}

static void server_cursor_frame(struct wl_listener *listener, void *data) {
    struct havel_wlr_server *server = wl_container_of(listener, server, cursor_frame);
    wlr_seat_pointer_notify_frame(server->seat);
}

// A client asked for a cursor surface (wl_pointer.set_cursor). Any client
// can send this, so only honour the one that currently has pointer focus.
// wlr_cursor then shows it on whichever output the pointer is on -
// hardware cursor plane where the backend has one, wlroots' software
// cursor otherwise.
static void seat_request_set_cursor(struct wl_listener *listener, void *data) {
    struct havel_wlr_server *server = wl_container_of(listener, server, request_set_cursor);
    struct wlr_seat_pointer_request_set_cursor_event *event = data;
    if (event->seat_client == server->seat->pointer_state.focused_client) {
        wlr_cursor_set_surface(server->cursor, event->surface,
                               event->hotspot_x, event->hotspot_y);
    }
}

// A client asked for a named cursor shape (cursor-shape-v1): "text",
// "ew-resize", ... - same focus check, then the theme provides the image.
static void cursor_shape_request_set_shape(struct wl_listener *listener, void *data) {
    struct havel_wlr_server *server = wl_container_of(listener, server, request_set_shape);
    struct wlr_cursor_shape_manager_v1_request_set_shape_event *event = data;
    if (event->seat_client == server->seat->pointer_state.focused_client) {
        wlr_cursor_set_xcursor(server->cursor, server->cursor_mgr,
                               wlr_cursor_shape_v1_name(event->shape));
    }
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
    wl_list_init(&server->layer_surfaces);
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
    
    // Initialize performance metrics
    server->frame_count = 0;
    server->current_fps = 0.0f;
    server->startup_time = get_monotonic_time_ms();

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
        // Outputs often first appear while the session is NOT yet
        // VT-active (native DRM: the previous VT's X server still holds
        // DRM master); their enable commits fail EPERM. Retry them when
        // the session activates. See session_active_cb.
        server->session_active.notify = session_active_cb;
        wl_signal_add(&server->session->events.active, &server->session_active);
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

    // Initialize desktop manager
    havel_cpp_init_desktop(server->cpp_server);

    // Initialize window group manager
    havel_cpp_init_window_groups(server->cpp_server);

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
    
    // (No loading-screen update here: the loading screen is only
    // created further down, after the overlay layer exists - status
    // calls this early were dead code, and their labels described work
    // that had already finished: "Creating allocator" ran after the
    // allocator was created.)
    server->compositor = wlr_compositor_create(server->display, 5, server->renderer);
    struct wlr_subcompositor *sub = wlr_subcompositor_create(server->display);
    fprintf(stderr, "subcompositor_create: %p\n", (void *)sub);
    // wl_shm is auto-created by wlroots 0.20 with renderer - don't create twice
    wlr_data_device_manager_create(server->display);

    server->output_layout = wlr_output_layout_create(server->display);
    server->scene = wlr_scene_create();

    // Make the scene follow the output layout. Without this the scene has
    // no idea where each output sits: every scene_output renders from
    // (0,0), so on a multi-monitor setup anything positioned in layout
    // coordinates - windows AND overlays - is drawn off-screen (observed
    // live on a two-output headless boot: the layout is 2560 wide, windows
    // were positioned at x=2801, and nothing from the second monitor
    // reached the capture). Single-output boots never noticed because
    // (0,0) is correct there. wlroots keeps scene_output positions in sync
    // from here on, including output moves.
    server->scene_output_layout =
        wlr_scene_attach_output_layout(server->scene, server->output_layout);

    // Background rect: created as the scene's FIRST child (bottom z),
    // before any workspace tree, so the wallpaper plugin's color
    // renders behind everything. Sized per-output in output_frame.
    {
        float black[4] = {0.0f, 0.0f, 0.0f, 1.0f};
        server->bg_rect = wlr_scene_rect_create(
            &server->scene->tree, 1, 1, black);
    }

    // Wallpaper tree: second child of the scene (after bg_rect, before
    // the workspace trees), so an image wallpaper lands behind windows.
    server->wallpaper_tree = wlr_scene_tree_create(&server->scene->tree);

    // Screencopy protocol: one line, unlocks grim / wf-recorder /
    // xdg-desktop-portal-wlr screen sharing. (The old C++ ScreenCapture /
    // PipeWireStream / DesktopPortal stack never ran - its bridge calls
    // were commented out and HAVE_PIPEWIRE was never defined by the
    // build - and was removed 2026-09. This is the real, supported path.)
    if (wlr_screencopy_manager_v1_create(server->display)) {
        LOG_INFO("[ScreenCapture] wlr-screencopy-v1 protocol created");
    } else {
        LOG_WARN("[ScreenCapture] Failed to create wlr-screencopy-v1 protocol");
    }

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
    wlr_scene_node_set_enabled(&server->overlay_layer->node, true);  // ENABLED for testing
    LOG_INFO("[Overlay] Overlay layer created (ENABLED for testing)");

    // Initialize loading screen (before outputs are created)
    loading_screen_init(server->overlay_layer, wl_display_get_event_loop(server->display));
    
    // Show loading screen
    if (loading_screen_get_config()->enabled) {
        loading_screen_show();
        loading_screen_set_status("Initializing compositor...");
        loading_screen_set_progress(10);
    }

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

    // Create output management v1 (for dynamic output configuration via wlr-randr)
    wlr_output_manager_v1_create(server->display);
    LOG_INFO("[Output Management] output_manager_v1 created (wlr-randr support)");

    // Note: output_power_manager_v1 requires wlr-protocols >= 1.21
    // Power management available via gamma control in the meantime

    // Initialize gamma control v1 manager
    server->gamma_control_manager = wlr_gamma_control_manager_v1_create(server->display);
    if (!server->gamma_control_manager) {
        LOG_WARN("[Gamma] Failed to create gamma_control_manager_v1");
    } else {
        LOG_INFO("[Gamma] gamma_control_manager_v1 initialized");
    }

    // Create foreign toplevel manager v1 (for taskbars, window switchers like waybar)
    server->foreign_toplevel_manager = wlr_foreign_toplevel_manager_v1_create(server->display);
    if (server->foreign_toplevel_manager) {
        LOG_INFO("[Foreign Toplevel] foreign_toplevel_manager_v1 created (waybar taskbar support)");
    }

    // Create pointer constraints v1 (for games - confine cursor)
    server->pointer_constraints = wlr_pointer_constraints_v1_create(server->display);
    if (server->pointer_constraints) {
        LOG_INFO("[Pointer Constraints] pointer_constraints_v1 created (game cursor confinement)");
        server->pointer_constraint.notify = handle_pointer_constraint;
        wl_signal_add(&server->pointer_constraints->events.new_constraint, &server->pointer_constraint);
    }

    // Create relative pointer manager v1 (for games - relative mouse motion)
    server->relative_pointer_manager = wlr_relative_pointer_manager_v1_create(server->display);
    if (server->relative_pointer_manager) {
        LOG_INFO("[Relative Pointer] relative_pointer_manager_v1 created (game mouse support)");
    }

    // Create idle inhibit manager v1 (prevent screensaver during video playback)
    server->idle_inhibit_manager = wlr_idle_inhibit_v1_create(server->display);
    if (server->idle_inhibit_manager) {
        LOG_INFO("[Idle Inhibit] idle_inhibit_v1 created (video playback support)");
    }

    // Create idle notifier v1 (screensaver support)
    server->idle_notifier = wlr_idle_notifier_v1_create(server->display);
    if (server->idle_notifier) {
        LOG_INFO("[Idle Notify] idle_notifier_v1 created (screensaver support)");
    }

    // Create virtual pointer manager v1 (remote desktop control)
    server->virtual_pointer_manager = wlr_virtual_pointer_manager_v1_create(server->display);
    if (server->virtual_pointer_manager) {
        LOG_INFO("[Virtual Pointer] virtual_pointer_manager_v1 created (remote desktop)");
    }

    // Create virtual keyboard manager v1 (input injection - lets test
    // clients drive the full key dispatch chain; also the remote-desktop
    // path). Virtual keyboards surface via the manager's own signal,
    // not backend new_input.
    server->virtual_keyboard_manager = wlr_virtual_keyboard_manager_v1_create(server->display);
    if (server->virtual_keyboard_manager) {
        server->new_virtual_keyboard.notify = server_new_virtual_keyboard;
        wl_signal_add(&server->virtual_keyboard_manager->events.new_virtual_keyboard,
                      &server->new_virtual_keyboard);
        LOG_INFO("[Virtual Keyboard] virtual_keyboard_manager_v1 created (input injection)");
    }

    // Create data control manager v1 (clipboard management)
    server->data_control_manager = wlr_data_control_manager_v1_create(server->display);
    if (server->data_control_manager) {
        LOG_INFO("[Data Control] data_control_manager_v1 created (clipboard history)");
    }

    // Create input method manager v2 (IME support)
    server->input_method_manager = wlr_input_method_manager_v2_create(server->display);
    if (server->input_method_manager) {
        LOG_INFO("[Input Method] input_method_manager_v2 created (IME support)");
    }

    // Create text input manager v3 (text input for IME)
    server->text_input_manager = wlr_text_input_manager_v3_create(server->display);
    if (server->text_input_manager) {
        LOG_INFO("[Text Input] text_input_manager_v3 created (text input)");
    }

    // Create cursor shape manager v1 (cursor shape customization)
    server->cursor_shape_manager = wlr_cursor_shape_manager_v1_create(server->display, 1);
    if (server->cursor_shape_manager) {
        LOG_INFO("[Cursor Shape] cursor_shape_manager_v1 created (cursor themes)");
    }

    // Create Linux DMA-BUF v1 (hardware buffer sharing)
    // create_with_renderer: passing NULL feedback to _create makes
    // wlroots build default feedback from nothing and crashes under
    // software renderers (pixman) — the header mandates the renderer
    // variant for wlr_renderer compositors.
    server->linux_dmabuf = wlr_linux_dmabuf_v1_create_with_renderer(
        server->display, 1, server->renderer);
    if (server->linux_dmabuf) {
        LOG_INFO("[Linux DMA-BUF] linux_dmabuf_v1 created (hardware acceleration)");
    }

    // Create presentation time (frame timing for video sync)
    server->presentation = wlr_presentation_create(server->display, server->backend, 1);
    if (server->presentation) {
        LOG_INFO("[Presentation] presentation_time created (video sync)");
    }


    // Create fractional scale manager v1 (high DPI scaling)
    server->fractional_scale_manager = wlr_fractional_scale_manager_v1_create(server->display, 1);
    if (server->fractional_scale_manager) {
        LOG_INFO("[Fractional Scale] fractional_scale_manager_v1 created (high DPI)");
    }

    // Create export DMA-BUF manager v1 (screen capture)
    server->export_dmabuf_manager = wlr_export_dmabuf_manager_v1_create(server->display);
    if (server->export_dmabuf_manager) {
        LOG_INFO("[Export DMA-BUF] export_dmabuf_manager_v1 created (screen capture)");
    }

    // Create color manager v1 (color calibration)
    // Options must be populated (renderer-derived lists, like labwc);
    // passing NULL crashes inside wlroots when it reads the option
    // arrays, and this wlroots version asserts against unsupported
    // features (icc_v2_v4, set_primaries). Parametric-only, perceptual
    // intent - verified under headless+pixman boot.
    {
        size_t tf_len = 0, prim_len = 0;
        enum wp_color_manager_v1_transfer_function *tfs =
            wlr_color_manager_v1_transfer_function_list_from_renderer(
                server->renderer, &tf_len);
        enum wp_color_manager_v1_primaries *prims =
            wlr_color_manager_v1_primaries_list_from_renderer(
                server->renderer, &prim_len);

        // Perceptual is required (wlroots asserts has_perceptual_render_intent)
        static const enum wp_color_manager_v1_render_intent intents[] = {
            WP_COLOR_MANAGER_V1_RENDER_INTENT_PERCEPTUAL,
        };
        struct wlr_color_manager_v1_options opts = {0};
        opts.features.parametric = true;
        opts.render_intents = intents;
        opts.render_intents_len = sizeof(intents) / sizeof(intents[0]);
        opts.transfer_functions = tfs;
        opts.transfer_functions_len = tf_len;
        opts.primaries = prims;
        opts.primaries_len = prim_len;

        server->color_manager = wlr_color_manager_v1_create(
            server->display, 1, &opts);
        if (server->color_manager) {
            LOG_INFO("[Color Management] color_manager_v1 created (color calibration)");
        }

        free(tfs);
        free(prims);
    }

    // Create extended foreign toplevel list v1 (taskbar support)
    server->ext_foreign_toplevel_list = wlr_ext_foreign_toplevel_list_v1_create(server->display, 1);
    if (server->ext_foreign_toplevel_list) {
        LOG_INFO("[Foreign Toplevel List] ext_foreign_toplevel_list_v1 created (taskbar)");
    }

    // Eager xwayland (lazy=false): the lazy server spawns on first client
    // connect and proved unreliable here - it accepted the connection,
    // created the surface, then died ("Restarting Xwayland (lazy)" loop,
    // clients got "X connection broken" - observed live). An eager server
    // starts at compositor boot and stays up.
    server->xwayland = wlr_xwayland_create(server->display, server->compositor, false);
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
    setenv("XCURSOR_SIZE", "24", 1);

    // Show a cursor before any client asks for one. Without an image the
    // pointer was invisible: nothing in this compositor ever called
    // set_xcursor at startup, and the two events that carry cursor images
    // (seat request_set_cursor, shape manager request_set_shape) had no
    // listeners at all - so client-supplied cursors and named shapes were
    // dropped on the floor.
    wlr_cursor_set_xcursor(server->cursor, server->cursor_mgr, "left_ptr");

    server->request_set_cursor.notify = seat_request_set_cursor;
    wl_signal_add(&server->seat->events.request_set_cursor, &server->request_set_cursor);

    if (server->cursor_shape_manager) {
        server->request_set_shape.notify = cursor_shape_request_set_shape;
        wl_signal_add(&server->cursor_shape_manager->events.request_set_shape,
                      &server->request_set_shape);
    }

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

    // Update loading screen - almost ready
    if (loading_screen_is_visible()) {
        loading_screen_set_status("Finalizing initialization...");
        loading_screen_set_progress(90);
    }

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

// IPC fd sources on the display event loop. The timer-based service
// proved fragile on this system's timerfd-based libwayland: under render
// load the timer expired once, was never read, and never re-armed
// (fdinfo: it_value=(0,0), ticks=1; the callback stopped firing the
// moment a rendering client connected - live breakpoints). Level-triggered
// fd sources do not lose events, so the IPC sockets themselves drive
// processEvents; the timer and frame-path calls stay as extra sweeps.
#define IPC_FD_SOURCES_MAX 64
static struct wl_event_loop *g_ipc_loop = NULL;
static havel_wlr_server_t *g_ipc_ctx = NULL;
static struct wl_event_source *g_ipc_fd_src[IPC_FD_SOURCES_MAX];
static int g_ipc_fd_num[IPC_FD_SOURCES_MAX];

static int ipc_fd_event(int fd, uint32_t mask, void *data) {
    (void)mask; (void)data;
    struct havel_wlr_server *server = g_ipc_ctx;
    if (!server || !server->cpp_server) return 0;

    // Guard against fd-number collisions: evdev-style fd reuse means a
    // stale source (its close notify missed somewhere) can fire for a
    // fd that now belongs to someone else - observed live: this handler
    // received a wayland client's fd whose events it swallowed while
    // the real wl_client source starved (libwayland's epoll add failed
    // EEXIST against our stale registration). If the fd is no longer an
    // IPC fd, drop the stale source and stay silent - the current owner
    // gets its own events once our stale registration is gone.
    int ipc_fds[IPC_FD_SOURCES_MAX];
    int n = havel_cpp_ipc_fds(server->cpp_server, ipc_fds, IPC_FD_SOURCES_MAX);
    bool ours = false;
    for (int i = 0; i < n; i++) {
        if (ipc_fds[i] == fd) { ours = true; break; }
    }
    if (!ours) {
        LOG_WARN("[IPC] fd %d readable but not an IPC fd anymore (stale "
                 "source after fd reuse) - removing", fd);
        havel_wlr_notify_ipc_fd(fd, 0);
        return 0;
    }

    havel_cpp_process_ipc_events(server->cpp_server);
    return 0;
}

// Called by the C++ IPCServer (accept path adds, disconnect removes).
// No-op until the display loop exists (IPC starts before havel_wlr_run;
// havel_wlr_run registers the pre-existing fds itself).
void havel_wlr_notify_ipc_fd(int fd, int added) {
    if (fd < 0 || !g_ipc_loop) return;
    if (added) {
        // Dedup: an fd number must never hold two of our sources - the
        // second epoll_ctl would fail EEXIST and leave a source that
        // never fires while its fd's events go elsewhere (see the stale
        // guard in ipc_fd_event).
        for (int i = 0; i < IPC_FD_SOURCES_MAX; i++) {
            if (g_ipc_fd_src[i] && g_ipc_fd_num[i] == fd) {
                wl_event_source_remove(g_ipc_fd_src[i]);
                g_ipc_fd_src[i] = NULL;
                g_ipc_fd_num[i] = -1;
                break;
            }
        }
        for (int i = 0; i < IPC_FD_SOURCES_MAX; i++) {
            if (!g_ipc_fd_src[i]) {
                g_ipc_fd_num[i] = fd;
                g_ipc_fd_src[i] = wl_event_loop_add_fd(
                    g_ipc_loop, fd, WL_EVENT_READABLE, ipc_fd_event, NULL);
                break;
            }
        }
    } else {
        for (int i = 0; i < IPC_FD_SOURCES_MAX; i++) {
            if (g_ipc_fd_src[i] && g_ipc_fd_num[i] == fd) {
                wl_event_source_remove(g_ipc_fd_src[i]);
                g_ipc_fd_src[i] = NULL;
                g_ipc_fd_num[i] = -1;
                break;
            }
        }
    }
}

// Repeating event-loop timer: services IPC even when no frames are
// scheduled (idle compositor stops requesting frames once damage dries
// up - see the add_timer call site in havel_wlr_run).
static struct wl_event_source *g_ipc_timer = NULL;

static int ipc_timer_event(void *data) {
    havel_wlr_server_t *server = (havel_wlr_server_t *)data;
    if (server && server->cpp_server) {
        havel_cpp_process_ipc_events(server->cpp_server);
    }
    // Explicit re-arm. Returning the delay from the callback is the
    // documented re-arm mechanism, but this system's timerfd-based
    // libwayland fired the callback exactly ONCE and never again
    // (observed live: gdb breakpoint hit at +50ms, then silence for
    // 40s; epoll kept waiting with timeout=-1). The explicit
    // wl_event_source_timer_update() works on both implementations.
    if (g_ipc_timer) {
        wl_event_source_timer_update(g_ipc_timer, 50);
    }
    return 0;
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

    // Self-injection keyboard: persistent virtual keyboard over a
    // background connection to our own socket. The background roundtrip
    // completes once the event loop below starts dispatching.
    havel_self_kbd_start();

    // Hide loading screen once compositor is fully running
    if (loading_screen_is_visible()) {
        loading_screen_set_progress(100);
        loading_screen_set_status("Ready!");
        
        // Auto-hide after 1 second
        struct LoadingScreenConfig* config = loading_screen_get_config();
        config->timeout_ms = 1000;
        loading_screen_start_timer();
    }

    // IPC liveness: processEvents is also driven from output_frame and
    // from fd sources on the loop (see havel_wlr_notify_ipc_fd above -
    // the reliable path). The repeating timer stays as an extra sweep
    // for the fully-idle case; its callback-return re-arm does not work
    // on this timerfd-based libwayland (fires once then stops under
    // load - observed), hence the explicit timer_update re-arm.
    g_ipc_loop = wl_display_get_event_loop(server->display);
    g_ipc_ctx = server;
    {
        int fds[IPC_FD_SOURCES_MAX];
        int n = havel_cpp_ipc_fds(server->cpp_server, fds, IPC_FD_SOURCES_MAX);
        for (int i = 0; i < n; i++) {
            havel_wlr_notify_ipc_fd(fds[i], 1);
        }
    }
    g_ipc_timer = wl_event_loop_add_timer(g_ipc_loop, ipc_timer_event, server);
    if (g_ipc_timer) {
        wl_event_source_timer_update(g_ipc_timer, 50);
    } else {
        LOG_ERROR("Failed to add IPC timer - IPC only serviced on frame ticks");
    }

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

// Per-monitor gamma control
void havel_wlr_set_gamma_for_output(havel_wlr_server_t *server, int output_index, float gamma) {
    if (!server) return;

    int i = 0;
    struct havel_output *output;
    wl_list_for_each(output, &server->outputs, link) {
        if (i == output_index) {
            output->gamma = gamma;
            havel_output_apply_gamma(output);
            LOG_INFO("[Gamma] Output %d (%s) set to %.2f", output_index, output->output->name, gamma);
            return;
        }
        i++;
    }
    LOG_WARN("[Gamma] Output %d not found", output_index);
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

// Per-monitor temperature control
void havel_wlr_set_temperature_for_output(havel_wlr_server_t *server, int output_index, int kelvin) {
    if (!server) return;

    int i = 0;
    struct havel_output *output;
    wl_list_for_each(output, &server->outputs, link) {
        if (i == output_index) {
            output->temperature = kelvin;
            havel_output_apply_gamma(output);
            LOG_INFO("[Temperature] Output %d (%s) set to %dK", output_index, output->output->name, kelvin);
            return;
        }
        i++;
    }
    LOG_WARN("[Temperature] Output %d not found", output_index);
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

// Per-monitor brightness control
void havel_wlr_set_brightness_for_output(havel_wlr_server_t *server, int output_index, float brightness) {
    if (!server) return;

    int i = 0;
    struct havel_output *output;
    wl_list_for_each(output, &server->outputs, link) {
        if (i == output_index) {
            output->brightness = brightness;
            havel_output_apply_gamma(output);
            LOG_INFO("[Brightness] Output %d (%s) set to %.2f", output_index, output->output->name, brightness);
            return;
        }
        i++;
    }
    LOG_WARN("[Brightness] Output %d not found", output_index);
}

// Per-monitor zoom control with cursor-centered zoom
void havel_wlr_set_zoom_for_output(havel_wlr_server_t *server, int output_index, float zoom, 
                                    float cursor_x, float cursor_y) {
    if (!server) return;

    int i = 0;
    struct havel_output *output;
    wl_list_for_each(output, &server->outputs, link) {
        if (i == output_index) {
            // Store cursor position as zoom center (if valid)
            if (cursor_x >= 0 && cursor_y >= 0) {
                output->zoom_center_x = cursor_x;
                output->zoom_center_y = cursor_y;
            }
            
            // Store previous zoom for offset calculation
            output->prev_zoom = output->zoom;
            output->zoom = zoom;
            
            LOG_INFO("[Zoom] Output %d (%s) set to %.2f (center: %.0f,%.0f)", 
                     output_index, output->output->name, zoom,
                     output->zoom_center_x, output->zoom_center_y);
            return;
        }
        i++;
    }
    LOG_WARN("[Zoom] Output %d not found", output_index);
}

// Wrapper without cursor position (for backward compatibility)
void havel_wlr_set_zoom_for_output_simple(havel_wlr_server_t *server, int output_index, float zoom) {
    havel_wlr_set_zoom_for_output(server, output_index, zoom, -1.0f, -1.0f);
}

// ============================================================================
// Texture Access for Alt-Tab Thumbnails
// ============================================================================

// Resolve the wlr_surface from either wrapper struct via the kind tag.
static struct wlr_surface* view_any_surface(void* c_view) {
    if (!c_view) return NULL;
    enum havel_view_kind kind = *(enum havel_view_kind*)c_view;
    if (kind == HAVEL_VIEW_XDG) {
        struct havel_xdg_view* view = (struct havel_xdg_view*)c_view;
        return view->xdg_surface ? view->xdg_surface->surface : NULL;
    }
    if (kind == HAVEL_VIEW_XWAYLAND) {
        struct havel_xwayland_view* view = (struct havel_xwayland_view*)c_view;
        return view->xsurface ? view->xsurface->surface : NULL;
    }
    return NULL;
}

// ============================================================================
// Scene Graph Integration - View Creation
// ============================================================================

SceneView* scene_view_create(SceneWorkspace* ws, struct wlr_xdg_surface* xdg_surface) {
    if (!ws || !xdg_surface) return NULL;
    
    SceneView* view = (SceneView*)calloc(1, sizeof(SceneView));
    if (!view) return NULL;
    
    view->base.type = SCENE_NODE_VIEW;
    view->xdg_surface = xdg_surface;
    view->mapped = false;
    view->floating = true;  // Default to floating
    
    // Get app_id and title
    if (xdg_surface->toplevel) {
        const char* app_id = xdg_surface->toplevel->app_id ? xdg_surface->toplevel->app_id : "";
        const char* title = xdg_surface->toplevel->title ? xdg_surface->toplevel->title : "";
        strncpy(view->app_id, app_id, sizeof(view->app_id) - 1);
        strncpy(view->title, title, sizeof(view->title) - 1);
    }
    
    // Default floating geometry
    view->float_x = 100;
    view->float_y = 100;
    view->float_width = 800;
    view->float_height = 600;
    
    return view;
}

SceneView* scene_view_create_xwayland(SceneWorkspace* ws, struct wlr_xwayland_surface* xwayland_surface) {
    if (!ws || !xwayland_surface) return NULL;
    
    SceneView* view = (SceneView*)calloc(1, sizeof(SceneView));
    if (!view) return NULL;
    
    view->base.type = SCENE_NODE_VIEW;
    view->xwayland_surface = xwayland_surface;
    view->mapped = false;
    view->floating = true;  // XWayland always floating
    
    // Get class and title
    const char* class_name = xwayland_surface->class ? xwayland_surface->class : "xwayland";
    const char* title = xwayland_surface->title ? xwayland_surface->title : "";
    strncpy(view->app_id, class_name, sizeof(view->app_id) - 1);
    strncpy(view->title, title, sizeof(view->title) - 1);
    
    // Default floating geometry
    view->float_x = 150;
    view->float_y = 150;
    view->float_width = 640;
    view->float_height = 480;
    
    return view;
}

void scene_view_destroy(SceneView* view) {
    if (!view) return;
    free(view);
}

bool scene_view_set_floating(SceneView* view, bool floating) {
    if (!view) return false;
    
    if (floating && !view->floating) {
        // Save current geometry
        view->float_x = view->base.x;
        view->float_y = view->base.y;
        view->float_width = view->base.width;
        view->float_height = view->base.height;
    }
    
    view->floating = floating;
    view->base.dirty_flags |= SCENE_DIRTY_LAYOUT;
    return true;
}

bool scene_view_focus(SceneView* view) {
    if (!view || !view->workspace) return false;

    view->workspace->active_view = view;
    if (view->container) {
        view->workspace->active_container = view->container;
    }

    return true;
}

// ============================================================================
// View Manipulation (C++ → C bridge)
// ============================================================================

void havel_wlr_set_view_position(void* c_view, int x, int y) {
    if (!c_view) return;

    // xwayland views need their cascade explicitly marked so the
    // configure handler can re-apply it after the client requests
    // its own (0,0)/spawn position at map time - see
    // xwayland_view_handle_configure.
    enum havel_view_kind* tag = (enum havel_view_kind*)c_view;
    if (*tag == HAVEL_VIEW_XWAYLAND) {
        struct havel_xwayland_view* xw = (struct havel_xwayland_view*)c_view;
        xw->pending_cascade_x = x;
        xw->pending_cascade_y = y;
    }

    // Call the registered callback
    if (g_view_set_position) {
        g_view_set_position(c_view, x, y);
    }
}

void havel_wlr_set_view_size(void* c_view, int w, int h) {
    if (!c_view) return;
    
    // Call the registered callback
    if (g_view_set_size) {
        g_view_set_size(c_view, w, h);
    }
}

void havel_wlr_close_view(void* c_view) {
    if (!c_view) return;
    
    // Call the registered callback
    if (g_view_close) {
        g_view_close(c_view);
    }
}

void havel_wlr_set_view_fullscreen(void* c_view, bool fullscreen) {
    if (!c_view) return;
    
    // Call the registered callback
    if (g_view_set_fullscreen) {
        g_view_set_fullscreen(c_view, fullscreen);
    }
}

void havel_wlr_minimize_view(void* c_view) {
    if (!c_view) return;
    
    // Call the registered callback
    if (g_view_minimize) {
        g_view_minimize(c_view);
    }
}

void havel_wlr_focus_view(void* c_view) {
    if (!c_view) return;
    
    // Call the registered callback
    if (g_view_focus) {
        g_view_focus(c_view);
    }
}

void havel_wlr_raise_view(void* c_view) {
    if (!c_view) return;
    
    // Call the registered callback
    if (g_view_raise) {
        g_view_raise(c_view);
    }
}

// Capture view thumbnail texture for Alt-Tab, Overview
// Returns non-zero if texture capture succeeded (actual GL texture requires Vulkan-GLES interop)
uint32_t havel_wlr_capture_view_texture(void* c_view) {
    if (!c_view) return 0;

    // Resolve via the kind tag — the same nativeHandle carries either
    // wrapper struct, and the old unconditional havel_xdg_view* cast
    // crashed on XWayland views before reaching the stub return.
    enum havel_view_kind kind = *(enum havel_view_kind*)c_view;
    struct wlr_scene_tree *scene_tree = NULL;
    if (kind == HAVEL_VIEW_XDG) {
        struct havel_xdg_view *view = (struct havel_xdg_view*)c_view;
        scene_tree = view->scene_tree;
    } else if (kind == HAVEL_VIEW_XWAYLAND) {
        struct havel_xwayland_view *view = (struct havel_xwayland_view*)c_view;
        scene_tree = view->scene_tree;
    }
    if (!scene_tree) return 0;

    // Get the scene surface from the scene tree
    struct wlr_scene_surface *scene_surface = NULL;

    // Walk the scene tree to find the surface
    struct wlr_scene_node *node = &scene_tree->node;
    if (node->type == WLR_SCENE_NODE_BUFFER) {
        struct wlr_scene_buffer *scene_buffer = wlr_scene_buffer_from_node(node);
        scene_surface = wlr_scene_surface_try_from_buffer(scene_buffer);
    }

    if (!scene_surface) return 0;

    // Get the underlying wlr_surface
    struct wlr_surface *surface = scene_surface->surface;
    if (!surface || !surface->buffer) return 0;

    // Texture capture not fully implemented (needs renderer access from the
    // C++ layer); Alt-Tab uses fallback colored rectangles.
    return 0;
}

// Capture a view's CURRENT committed buffer as CPU pixels (ARGB8888
// little-endian, premultiplied - the wl_shm convention, and what the
// scene buffers expect). Works with the software renderer and plain shm
// clients, which is what Alt-Tab thumbnails need; DMA-BUF (GL clients)
// and transformed/scaled buffers fall back to the placeholder honestly
// instead of rendering something garbled.
//
// *out_pixels is malloc'd (free with havel_wlr_capture_pixels_free);
// returns 0 with *out_pixels = NULL when capture is not possible.
// Iterator callback: keep the first buffer node (the toplevel surface).
static void first_buffer_iter(struct wlr_scene_buffer *buf, int sx, int sy, void *data) {
    (void)sx; (void)sy;
    struct wlr_scene_buffer **out = (struct wlr_scene_buffer**)data;
    if (!*out) *out = buf;
}

bool havel_wlr_capture_view_pixels(void* c_view,
                                    uint32_t** out_pixels, int* out_w, int* out_h) {
    if (!c_view || !out_pixels || !out_w || !out_h) return false;
    *out_pixels = NULL; *out_w = 0; *out_h = 0;

    enum havel_view_kind kind = *(enum havel_view_kind*)c_view;
    struct wlr_scene_tree *scene_tree = NULL;
    if (kind == HAVEL_VIEW_XDG) {
        scene_tree = ((struct havel_xdg_view*)c_view)->scene_tree;
    } else if (kind == HAVEL_VIEW_XWAYLAND) {
        scene_tree = ((struct havel_xwayland_view*)c_view)->scene_tree;
    }
    if (!scene_tree) return false;

    // The view tree wraps the surface: find its buffer node. (The tree
    // node itself is a TREE, not a BUFFER - the original capture code
    // checked the tree node and always bailed.)
    struct wlr_scene_buffer *scene_buffer = NULL;
    wlr_scene_node_for_each_buffer(&scene_tree->node, first_buffer_iter, &scene_buffer);
    if (!scene_buffer) return false;
    struct wlr_scene_surface *scene_surface =
        wlr_scene_surface_try_from_buffer(scene_buffer);
    if (!scene_surface) return false;

    struct wlr_surface *surface = scene_surface->surface;
    if (!surface || !surface->buffer) return false;

    // Rotated/flipped or scaled buffers need transform-aware copying;
    // rather than risk a garbled thumbnail, no capture for those.
    if (surface->current.transform != WL_OUTPUT_TRANSFORM_NORMAL ||
        surface->current.scale != 1) {
        return false;
    }
    struct wlr_client_buffer *cb = surface->buffer;
    if (!cb) return false;

    void* data = NULL;
    uint32_t format = 0;
    size_t stride = 0;
    if (!wlr_buffer_begin_data_ptr_access(&cb->base, WLR_BUFFER_DATA_PTR_ACCESS_READ,
                                           &data, &format, &stride)) {
        return false;   // DMA-BUF etc. - no CPU access
    }
    bool ok = false;
    const int bw = surface->current.buffer_width;
    const int bh = surface->current.buffer_height;
    if ((format == DRM_FORMAT_ARGB8888 || format == DRM_FORMAT_XRGB8888) &&
        data && bw > 0 && bh > 0 && stride >= (size_t)bw * 4) {
        uint32_t* out = malloc((size_t)bw * bh * 4);
        if (out) {
            for (int y = 0; y < bh; y++) {
                memcpy(out + (size_t)y * bw, (uint8_t*)data + (size_t)y * stride,
                       (size_t)bw * 4);
            }
            *out_pixels = out;
            *out_w = bw;
            *out_h = bh;
            ok = true;
        }
    }
    wlr_buffer_end_data_ptr_access(&cb->base);
    return ok;
}

void havel_wlr_capture_pixels_free(uint32_t* pixels) {
    free(pixels);
}

// ============================================================================
// Input injection (script-driven; io.injectKey host function)
// ============================================================================

#include <wlr/interfaces/wlr_keyboard.h>
#include <wayland-client.h>
#include <pthread.h>
#include "virtual-keyboard-unstable-v1-client-protocol.h"

// Self-injection keyboard: a background wayland-client connection to the
// compositor's own socket, holding a persistent virtual keyboard. The
// transient-client pattern (the harness's inject_keys) dies with its
// client; this one lives for the compositor's lifetime, so io.injectKey
// works at any point after the event loop starts. This is the "WM owns
// the input system" vision from the archived input-backend plan, in
// Wayland terms.
struct havel_self_kbd {
    struct wl_display *display;
    struct wl_seat *seat;
    struct zwp_virtual_keyboard_manager_v1 *mgr;
    struct zwp_virtual_keyboard_v1 *kbd;
    pthread_t thread;
    pthread_mutex_t lock;
    volatile bool ready;
};
static struct havel_self_kbd self_kbd = {.lock = PTHREAD_MUTEX_INITIALIZER, .ready = false};

static const char *SELF_KBD_KEYMAP = \
    "xkb_keymap {\n"
    "xkb_keycodes { include \"evdev+aliases(qwerty)\" };\n"
    "xkb_types { include \"complete\" };\n"
    "xkb_compat { include \"complete\" };\n"
    "xkb_symbols { include \"pc+us+inet(evdev)\" };\n"
    "};\n";

static void self_kbd_reg_global(void *data, struct wl_registry *r,
                                 uint32_t name, const char *iface, uint32_t ver) {
    (void)data; (void)ver;
    if (strcmp(iface, "wl_seat") == 0) {
        self_kbd.seat = wl_registry_bind(r, name, &wl_seat_interface, 1);
    } else if (strcmp(iface, "zwp_virtual_keyboard_manager_v1") == 0) {
        self_kbd.mgr = wl_registry_bind(r, name,
                                         &zwp_virtual_keyboard_manager_v1_interface, 1);
    }
}
static void self_kbd_reg_remove(void *data, struct wl_registry *r, uint32_t name) {
    (void)data; (void)r; (void)name;
}

static void *self_kbd_thread(void *arg) {
    (void)arg;
    struct wl_display *d = wl_display_connect(NULL);  // WAYLAND_DISPLAY is set
    if (!d) {
        LOG_WARN("[Inject] Self-keyboard: failed to connect to own socket");
        return NULL;
    }
    self_kbd.display = d;

    struct wl_registry *registry = wl_display_get_registry(d);
    struct wl_registry_listener reg_listener = {
        .global = self_kbd_reg_global,
        .global_remove = self_kbd_reg_remove,
    };
    wl_registry_add_listener(registry, &reg_listener, NULL);
    wl_display_roundtrip(d);

    if (!self_kbd.seat || !self_kbd.mgr) {
        LOG_WARN("[Inject] Self-keyboard: missing globals (seat=%p mgr=%p)",
                 (void*)self_kbd.seat, (void*)self_kbd.mgr);
        return NULL;
    }

    self_kbd.kbd = zwp_virtual_keyboard_manager_v1_create_virtual_keyboard(
        self_kbd.mgr, self_kbd.seat);
    wl_display_roundtrip(d);
    if (!self_kbd.kbd) {
        LOG_WARN("[Inject] Self-keyboard: keyboard creation failed");
        return NULL;
    }

    // Keymap (protocol requirement before keys)
    size_t size = strlen(SELF_KBD_KEYMAP) + 1;
    char path[] = "/tmp/havel-self-kbd-XXXXXX";
    int fd = mkstemp(path);
    if (fd >= 0) {
        unlink(path);
        if (write(fd, SELF_KBD_KEYMAP, size) == (ssize_t)size) {
            lseek(fd, 0, SEEK_SET);
            zwp_virtual_keyboard_v1_keymap(self_kbd.kbd, 1 /* xkb_v1 */, fd, (uint32_t)size);
        }
        close(fd);
    }
    wl_display_roundtrip(d);

    self_kbd.ready = true;
    LOG_INFO("[Inject] Self-keyboard ready (persistent virtual keyboard)");

    // Dispatch loop: keeps the connection alive and processes the
    // keyboard's lifecycle events for the compositor's runtime
    while (wl_display_dispatch(d) >= 0) {
    }
    LOG_WARN("[Inject] Self-keyboard connection lost");
    return NULL;
}

// Feed a synthetic key event through the self-keyboard (persistent) or,
// before it is ready, the seat's current keyboard. The full compositor
// dispatch chain runs (bridge handler -> keybindings -> plugins) exactly
// as it would for a real keypress. The global-hotkey path the monorepo's
// io.send was written for (X11 evdev grabs) has no meaning under a
// compositor that owns the seat - this is its Wayland equivalent.
void havel_wlr_inject_key(struct havel_wlr_server *server, uint32_t keycode,
                          bool pressed) {
    if (!server) return;

    struct wlr_keyboard_key_event event = {0};
    event.keycode = keycode;  // raw evdev; the key handler applies the +8 xkb offset
    event.state = pressed ? WL_KEYBOARD_KEY_STATE_PRESSED
                          : WL_KEYBOARD_KEY_STATE_RELEASED;
    event.time_msec = (uint32_t)get_monotonic_time_ms();

    // Preferred: the persistent self-keyboard (survives client
    // connect/disconnect cycles)
    if (self_kbd.ready && self_kbd.kbd) {
        pthread_mutex_lock(&self_kbd.lock);
        zwp_virtual_keyboard_v1_key(self_kbd.kbd, event.time_msec, keycode, event.state);
        wl_display_flush(self_kbd.display);
        pthread_mutex_unlock(&self_kbd.lock);
        return;
    }

    // Fallback: the seat's current keyboard (a client's virtual keyboard
    // or a physical device)
    if (server->seat) {
        struct wlr_keyboard *keyboard = wlr_seat_get_keyboard(server->seat);
        if (keyboard) {
            wlr_keyboard_notify_key(keyboard, &event);
            return;
        }
    }

    LOG_WARN("[Inject] No keyboard available (self-keyboard not ready, no seat keyboard) - key 0x%x dropped",
             keycode);
}

// Cursor control. Moving wlr_cursor directly and running the same
// dispatch chain as the hardware pointer keeps client focus, interactive
// grabs and plugin overlays (Alt-Tab hover/buttons, hot corners) in sync.
void havel_wlr_warp_pointer(struct havel_wlr_server *server, double x, double y) {
    if (!server || !server->cursor) return;
    wlr_cursor_warp(server->cursor, NULL, x, y);
    havel_cpp_on_pointer_decoration_motion(server->cpp_server,
                                           (int)server->cursor->x,
                                           (int)server->cursor->y);
    havel_cpp_on_pointer_motion(server->cpp_server, server->cursor->x, server->cursor->y);
    process_cursor_motion(server, get_monotonic_time_ms());
    LOG_DEBUG("[Inject] Pointer warped to (%.0f,%.0f)", server->cursor->x, server->cursor->y);
}

void havel_wlr_click_pointer(struct havel_wlr_server *server, uint32_t button, bool pressed) {
    if (!server || !server->seat) return;
    uint32_t t = (uint32_t)get_monotonic_time_ms();
    wlr_seat_pointer_notify_button(server->seat, t, button,
        pressed ? WL_POINTER_BUTTON_STATE_PRESSED : WL_POINTER_BUTTON_STATE_RELEASED);
    havel_cpp_on_pointer_decoration_button(server->cpp_server, button, pressed,
        (int)server->cursor->x, (int)server->cursor->y);
    havel_cpp_on_pointer_button(server->cpp_server, button, pressed,
        server->cursor->x, server->cursor->y);
    LOG_DEBUG("[Inject] Pointer button 0x%x %s", button, pressed ? "down" : "up");
}

// Force software cursors on every output (or restore wlroots' choice).
// wlr_cursor shows a hardware cursor plane when the backend has one, which
// is what you want normally; on backends that advertise a plane but never
// composite it (headless does), or while capturing, the software cursor is
// the one that actually reaches the framebuffer.
void havel_wlr_set_software_cursor(struct havel_wlr_server *server, bool forced) {
    if (!server) return;
    if (forced == server->software_cursor_forced) return;
    server->software_cursor_forced = forced;
    struct havel_output *output;
    wl_list_for_each(output, &server->outputs, link) {
        wlr_output_lock_software_cursors(output->output, forced);
    }
    LOG_INFO("[Cursor] software cursor %s", forced ? "forced" : "released");
}

// Hide the pointer image until something sets one again (show_cursor or a
// client request). wlr_cursor_unset_image is the supported way.
void havel_wlr_hide_cursor(struct havel_wlr_server *server) {
    if (!server || !server->cursor) return;
    wlr_cursor_unset_image(server->cursor);
    LOG_INFO("[Cursor] hidden by request");
}

// Restore the default pointer image.
void havel_wlr_show_cursor(struct havel_wlr_server *server) {
    if (!server || !server->cursor || !server->cursor_mgr) return;
    wlr_cursor_set_xcursor(server->cursor, server->cursor_mgr, "left_ptr");
    LOG_INFO("[Cursor] shown by request");
}

// Swap the xcursor theme/size live: replace the manager and re-set the
// image so the new theme is visible immediately.
void havel_wlr_set_cursor_theme(struct havel_wlr_server *server,
                                const char *theme, int size) {
    if (!server || size < 16 || size > 128) return;
    if (server->cursor_mgr) {
        wlr_xcursor_manager_destroy(server->cursor_mgr);
    }
    server->cursor_mgr = wlr_xcursor_manager_create(
        (theme && *theme) ? theme : NULL, size);
    char sz[8];
    snprintf(sz, sizeof sz, "%d", size);
    setenv("XCURSOR_SIZE", sz, 1);
    if (server->cursor && server->cursor_mgr) {
        wlr_cursor_set_xcursor(server->cursor, server->cursor_mgr, "left_ptr");
    }
    LOG_INFO("[Cursor] theme set to '%s' size %d",
             (theme && *theme) ? theme : "default", size);
}

// Set a wallpaper image (ARGB8888 CPU pixels). The compositor keeps a copy
// so outputs enabled later can be covered too. Pass NULL to clear (back
// to the solid background colour). The buffer is sized to the whole
// output layout; the caller scales the image to fit.
void havel_wlr_set_wallpaper(struct havel_wlr_server *server,
                              const uint32_t *pixels, int w, int h) {
    if (!server || !server->wallpaper_tree) return;

    free(server->wallpaper_pixels);
    server->wallpaper_pixels = NULL;
    server->wallpaper_w = 0;
    server->wallpaper_h = 0;

    if (server->wallpaper_buffer) {
        wlr_scene_node_destroy(&server->wallpaper_buffer->node);
        server->wallpaper_buffer = NULL;
    }

    if (!pixels || w <= 0 || h <= 0) {
        LOG_INFO("[Wallpaper] cleared");
        return;
    }

    size_t n = (size_t)w * h;
    server->wallpaper_pixels = malloc(n * sizeof(uint32_t));
    if (!server->wallpaper_pixels) return;
    memcpy(server->wallpaper_pixels, pixels, n * sizeof(uint32_t));
    server->wallpaper_w = w;
    server->wallpaper_h = h;

    // Fit the layout box if it is known; otherwise the first output
    // enable re-creates the node (see output_try_enable).
    struct wlr_box box = {0};
    wlr_output_layout_get_box(server->output_layout, NULL, &box);
    if (box.width > 0 && box.height > 0) {
        server->wallpaper_buffer = havel_scene_pixels(
            server->wallpaper_tree, server->wallpaper_pixels, w, h,
            box.x, box.y, box.width, box.height);
    }
    LOG_INFO("[Wallpaper] set: %dx%d image", w, h);
}

// Cover a newly enabled output with the current wallpaper.
static void wallpaper_fit_output(struct havel_wlr_server *server) {
    if (!server || !server->wallpaper_tree || !server->wallpaper_pixels) return;
    struct wlr_box box = {0};
    wlr_output_layout_get_box(server->output_layout, NULL, &box);
    if (box.width <= 0 || box.height <= 0) return;
    if (server->wallpaper_buffer) {
        wlr_scene_node_destroy(&server->wallpaper_buffer->node);
        server->wallpaper_buffer = NULL;
    }
    server->wallpaper_buffer = havel_scene_pixels(
        server->wallpaper_tree, server->wallpaper_pixels,
        server->wallpaper_w, server->wallpaper_h,
        box.x, box.y, box.width, box.height);
}

void havel_wlr_get_pointer(struct havel_wlr_server *server, double *x, double *y) {
    if (!server || !server->cursor) { if (x) *x = -1; if (y) *y = -1; return; }
    if (x) *x = server->cursor->x;
    if (y) *y = server->cursor->y;
}

void havel_wlr_scroll_pointer(struct havel_wlr_server *server, double dx, double dy) {
    if (!server || !server->cpp_server) return;
    havel_cpp_on_pointer_axis(server->cpp_server, dx, dy);
}

// Start the self-keyboard thread. Called from havel_wlr_run once the
// socket exists; the background roundtrip completes when the main loop
// begins dispatching.
void havel_self_kbd_start(void) {
    if (pthread_create(&self_kbd.thread, NULL, self_kbd_thread, NULL) == 0) {
        LOG_INFO("[Inject] Self-keyboard thread started");
    } else {
        LOG_WARN("[Inject] Self-keyboard thread failed to start");
    }
}
