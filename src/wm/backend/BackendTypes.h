// Backend Types - Common structures for wlroots backend
// Pure C - no C++ dependencies

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <wayland-server-core.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_input_device.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/types/wlr_layer_shell_v1.h>

#ifdef __cplusplus
extern "C" {
#endif

// Forward declarations
struct havel_wlr_server;
struct havel_output;
struct havel_keyboard;
struct havel_pointer;
struct havel_xdg_view;
struct havel_layer_surface;

// ============================================================================
// Server Handle - Opaque pointer to wlroots server state
// ============================================================================

typedef struct havel_wlr_server {
    struct wl_display *display;
    struct wlr_backend *backend;
    struct wlr_scene *scene;
    struct wlr_allocator *allocator;
    struct wlr_renderer *renderer;
    
    struct wlr_output_layout *output_layout;
    struct wl_list outputs;  // struct havel_output.link
    
    struct wlr_cursor *cursor;
    struct wlr_xcursor_manager *cursor_mgr;
    struct wlr_seat *seat;
    struct wl_list keyboards;  // struct havel_keyboard.link
    struct wl_list pointers;   // struct havel_pointer.link
    
    struct wlr_xdg_shell *xdg_shell;
    struct wl_listener new_xdg_toplevel;
    struct wl_listener new_xdg_surface;
    
    struct wlr_layer_shell_v1 *layer_shell;
    struct wl_listener new_layer_surface;
    
    struct wl_listener new_input;
    struct wl_listener request_cursor;
    struct wl_listener request_set_selection;
    
    // Workspace management
    uint32_t active_workspace;
    struct wlr_scene_tree *workspaces[10];  // 10 workspaces
    struct wlr_scene_tree *overlay_layer;   // For Alt-Tab, notifications, etc.
    
    // C++ server bridge
    void *cpp_server;  // havel::Server*
    
    // Session (for VT switching)
    struct wlr_session *session;
    
    // Performance metrics
    uint64_t frame_count;      // Total frames rendered
    float current_fps;         // Current FPS (updated every second)
    uint64_t startup_time;     // Monotonic time at startup (for uptime calculation)
} havel_wlr_server_t;

// ============================================================================
// Output Handle - Manages a single display output
// ============================================================================

typedef struct havel_output {
    struct havel_wlr_server *server;
    struct wlr_output *output;
    struct wlr_scene_output *scene_output;
    
    struct wl_listener frame;
    struct wl_listener destroy;
    
    struct wl_list link;  // havel_wlr_server_t.outputs
    
    // Display properties
    bool is_primary;
    
    // Gamma/temperature/brightness (per-output control)
    float gamma;
    int temperature;
    float brightness;
    float zoom;
    float zoom_center_x;
    float zoom_center_y;
    float prev_zoom;
    
    // Gamma LUT
    size_t gamma_ramp_size;
    uint16_t *gamma_ramp_red;
    uint16_t *gamma_ramp_green;
    uint16_t *gamma_ramp_blue;
    bool gamma_ramp_dirty;
} havel_output_t;

// ============================================================================
// Input Device Handles
// ============================================================================

typedef struct havel_keyboard {
    struct havel_wlr_server *server;
    struct wlr_keyboard *keyboard;
    
    struct wl_listener modifiers;
    struct wl_listener key;
    struct wl_listener destroy;
    
    struct wl_list link;  // havel_wlr_server_t.keyboards
    
    // XKB state
    struct xkb_state *xkb_state;
    struct xkb_keymap *keymap;
} havel_keyboard_t;

typedef struct havel_pointer {
    struct havel_wlr_server *server;
    struct wlr_pointer *pointer;
    
    struct wl_listener motion;
    struct wl_listener motion_absolute;
    struct wl_listener button;
    struct wl_listener axis;
    struct wl_listener frame;
    struct wl_listener destroy;
    
    struct wl_list link;  // havel_wlr_server_t.pointers
} havel_pointer_t;

// ============================================================================
// View Handles - Window representations
// ============================================================================

typedef struct havel_xdg_view {
    struct wlr_xdg_surface *xdg_surface;
    struct wlr_scene_tree *scene_tree;
    struct havel_wlr_server *server;
    
    void *cpp_view;         // havel::View*
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
    
    struct wl_list link;  // For view lists
} havel_xdg_view_t;

typedef struct havel_layer_surface {
    struct wlr_layer_surface_v1 *layer_surface;
    struct wlr_scene_tree *scene_tree;
    struct havel_wlr_server *server;
    
    struct wl_listener destroy;
    struct wl_listener map;
    struct wl_listener unmap;
    struct wl_listener surface_commit;
    
    struct wl_list link;  // For layer surface lists
} havel_layer_surface_t;

// ============================================================================
// Interactive Grab State
// ============================================================================

typedef enum {
    GRAB_MODE_NONE = 0,
    GRAB_MODE_MOVE,
    GRAB_MODE_RESIZE,
} grab_mode_t;

typedef struct havel_grab {
    grab_mode_t mode;
    void *view;  // havel_xdg_view_t*
    double start_x;
    double start_y;
    int view_start_x;
    int view_start_y;
    int view_start_w;
    int view_start_h;
    uint32_t resize_edges;
} havel_grab_t;

// ============================================================================
// Function Declarations - Output Management
// ============================================================================

void havel_output_init(havel_wlr_server_t *server);
havel_output_t* havel_output_create(havel_wlr_server_t *server, struct wlr_output *wlr_output);
void havel_output_destroy(havel_output_t *output);

// ============================================================================
// Function Declarations - Input Handling
// ============================================================================

void havel_input_init(havel_wlr_server_t *server);
havel_keyboard_t* havel_keyboard_create(havel_wlr_server_t *server, struct wlr_input_device *device);
havel_pointer_t* havel_pointer_create(havel_wlr_server_t *server, struct wlr_input_device *device);
void havel_keyboard_destroy(havel_keyboard_t *keyboard);
void havel_pointer_destroy(havel_pointer_t *pointer);

// ============================================================================
// Function Declarations - XDG Shell
// ============================================================================

void havel_xdg_shell_init(havel_wlr_server_t *server);
havel_xdg_view_t* havel_xdg_view_create(havel_wlr_server_t *server, struct wlr_xdg_toplevel *toplevel);
void havel_xdg_view_destroy(havel_xdg_view_t *view);

// ============================================================================
// Function Declarations - Layer Shell
// ============================================================================

void havel_layer_shell_init(havel_wlr_server_t *server);
havel_layer_surface_t* havel_layer_surface_create(havel_wlr_server_t *server, struct wlr_layer_surface_v1 *surface);
void havel_layer_surface_destroy(havel_layer_surface_t *lsurface);

// ============================================================================
// Function Declarations - Cursor & Seat
// ============================================================================

void havel_cursor_init(havel_wlr_server_t *server);
void havel_seat_init(havel_wlr_server_t *server);
void havel_cursor_destroy(havel_wlr_server_t *server);
void havel_seat_destroy(havel_wlr_server_t *server);

// ============================================================================
// Function Declarations - Server Lifecycle
// ============================================================================

havel_wlr_server_t* havel_server_create(struct wl_display *display);
void havel_server_destroy(havel_wlr_server_t *server);
void havel_server_start(havel_wlr_server_t *server);

#ifdef __cplusplus
}
#endif
