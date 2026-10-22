#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// Callback function types
typedef void (*cpp_view_set_position_fn)(void* view, int x, int y);
typedef void (*cpp_view_set_size_fn)(void* view, int w, int h);
typedef void (*cpp_view_focus_fn)(void* view);
typedef void (*cpp_view_raise_fn)(void* view);
typedef void (*cpp_view_get_geometry_fn)(void* view, int* x, int* y, int* w, int* h);
typedef void (*cpp_view_close_fn)(void* view);
typedef void (*cpp_view_set_fullscreen_fn)(void* view, bool fullscreen);
typedef void (*cpp_view_minimize_fn)(void* view);
typedef void (*cpp_workspace_arrange_fn)(uint32_t workspace_id);
typedef void (*cpp_workspace_set_active_fn)(uint32_t workspace_id);
typedef void (*cpp_server_quit_fn)(void);
typedef void (*cpp_server_spawn_fn)(const char* command);

// Opaque handle to C++ Server
struct havel_cpp_server;

// Opaque handle to C server (wlroots)
struct havel_wlr_server;
typedef struct havel_wlr_server havel_wlr_server_t;

// Opaque handle to output
struct havel_output;

// Create/destroy C++ server
struct havel_cpp_server* havel_cpp_server_create(void);
void havel_cpp_server_destroy(struct havel_cpp_server* server);

// Store the C server pointer in the C++ wrapper
void havel_cpp_server_set_native_handle(struct havel_cpp_server* server, void* handle);

// Set overlay layer for plugin rendering
void havel_cpp_server_set_overlay_layer(struct havel_cpp_server* server, void* overlay_layer);

// Initialize text input manager (IME)
struct wl_display;
void havel_cpp_server_init_text_input(struct havel_cpp_server* server, struct wl_display* display);

// View lifecycle events (called from C bridge)
// Returns opaque View pointer for C to store
void* havel_cpp_on_xdg_surface_new(struct havel_cpp_server* server, void* c_view, uint32_t workspace_id, const char* appId, const char* title);
void havel_cpp_on_view_mapped(struct havel_cpp_server* server, void* c_view);
void havel_cpp_on_view_unmapped(struct havel_cpp_server* server, void* c_view);
// XDG title change: propagates the new toplevel title into the C++ layer
// (View + core Window) and re-broadcasts to IPC subscribers.
void havel_cpp_on_view_title_changed(struct havel_cpp_server* server, void* c_view, const char* title);
void havel_cpp_on_view_destroyed(struct havel_cpp_server* server, void* c_view);

// Input events - returns true if consumed by compositor
bool havel_cpp_on_key(struct havel_cpp_server* server, uint32_t keycode, bool pressed, uint32_t modifiers, uint32_t keysym, char key_char, const char* utf8);
void havel_cpp_on_pointer_button(struct havel_cpp_server* server, uint32_t button, bool pressed, double x, double y);
void havel_cpp_on_pointer_motion(struct havel_cpp_server* server, double x, double y);
// Scroll ticks (wlr axis deltas). Positive dy = scroll down.
void havel_cpp_on_pointer_axis(struct havel_cpp_server* server, double dx, double dy);
void havel_cpp_on_pointer_decoration_motion(struct havel_cpp_server* server, int x, int y);
void havel_cpp_on_pointer_decoration_button(struct havel_cpp_server* server, uint32_t button, bool pressed, int x, int y);

// Output/workspace events
void havel_cpp_set_output_geometry(struct havel_cpp_server* server, uint32_t workspace_id, int x, int y, int w, int h);

// Animation updates (called from C frame handler)
void havel_cpp_update_animations(struct havel_cpp_server* server);

// Background color (called from C for clear color)
void havel_cpp_get_background_color(struct havel_cpp_server* server, float* r, float* g, float* b);

// Overlay rendering
// Which output the plugin overlay is being drawn for. The scene is global,
// so overlays that need to sit on one monitor (Alt-Tab's monitor setting)
// resolve against this: (x,y,w,h) is the output's box in scene
// coordinates, index is its position in the output list, primary marks the
// first output, under_cursor marks the output holding the pointer.
struct havel_overlay_target {
    int x, y, w, h;
    int index;
    int primary;
    int under_cursor;
};

void havel_cpp_draw_overlays(struct havel_cpp_server* server, int width, int height,
                             const struct havel_overlay_target* target);

// Zoom magnifier capture handshake (C renders the scene, C++ wants a
// region as CPU pixels for lens/strip/custom zoom modes).
// take_request: atomically fetch + clear the region the C++ side asked for
// during the overlay pass; returns false when there is no pending request.
bool havel_cpp_zoom_take_request(struct havel_cpp_server* server, int output_index,
                                  float* cx, float* cy, int* w, int* h);
// feed: deliver the captured region (output-local source coordinates).
// Copies the pixels; caller frees.
void havel_cpp_zoom_feed(struct havel_cpp_server* server, int output_index,
                          const uint32_t* pixels, int w, int h, float src_x, float src_y);

// Application spawning (for App Launcher): fork+exec, no shell
void havel_cpp_server_spawn(struct havel_cpp_server* server, const char* command);

// IPC server events
void havel_cpp_process_ipc_events(struct havel_cpp_server* server);

// Current IPC fds (listen first, then clients) for event-loop sources
int havel_cpp_ipc_fds(struct havel_cpp_server* server, int* out, int max);

// Defined in wlr_bridge.c. The IPC server calls this on accept
// (added=1) and disconnect (added=0) so the display event loop can
// watch the socket fds directly - fd sources are the reliable IPC
// wakeup (timer-based service lost wakeups under render load).
void havel_wlr_notify_ipc_fd(int fd, int added);

// Output frame dispatch (with actual dimensions from wlroots)
void havel_cpp_dispatch_output_frame(struct havel_cpp_server* server, void* output, void* sceneOutput, int width, int height, int refresh);

// Desktop management
void havel_cpp_init_desktop(struct havel_cpp_server* server);
void* havel_cpp_get_scene_graph(struct havel_cpp_server* server);
void havel_cpp_process_desktop_mouse(struct havel_cpp_server* server, int button, bool pressed, int x, int y);
void havel_cpp_process_desktop_motion(struct havel_cpp_server* server, int x, int y);
void havel_cpp_process_desktop_key(struct havel_cpp_server* server, uint32_t keysym, uint32_t modifiers);

// Window group management
void havel_cpp_init_window_groups(struct havel_cpp_server* server);

// C layer gamma application (global - all monitors)
void havel_wlr_set_gamma(havel_wlr_server_t* server, float gamma);
void havel_wlr_set_temperature(havel_wlr_server_t* server, int kelvin);
void havel_wlr_set_brightness(havel_wlr_server_t* server, float brightness);
void havel_wlr_quit(void);
void havel_wlr_close_view(void* c_view);
void havel_wlr_set_gamma_for_output(havel_wlr_server_t* server, int output_index, float gamma);
void havel_wlr_set_temperature_for_output(havel_wlr_server_t* server, int output_index, int kelvin);
void havel_wlr_set_brightness_for_output(havel_wlr_server_t* server, int output_index, float brightness);

// Callback registration (called from C during initialization)
void havel_cpp_register_view_callbacks(
    cpp_view_set_position_fn set_position,
    cpp_view_set_size_fn set_size,
    cpp_view_focus_fn focus,
    cpp_view_raise_fn raise,
    cpp_view_get_geometry_fn get_geometry,
    cpp_view_close_fn close,
    cpp_view_set_fullscreen_fn set_fullscreen,
    cpp_view_minimize_fn minimize
);

void havel_cpp_register_workspace_callbacks(
    cpp_workspace_arrange_fn arrange,
    cpp_workspace_set_active_fn set_active
);

void havel_cpp_register_server_callbacks(
    cpp_server_quit_fn quit,
    cpp_server_spawn_fn spawn
);

// View manipulation (called from C++ Server.cpp)
void havel_wlr_set_view_position(void* c_view, int x, int y);
void havel_wlr_set_view_size(void* c_view, int w, int h);
void havel_wlr_close_view(void* c_view);
void havel_wlr_set_view_fullscreen(void* c_view, bool fullscreen);
void havel_wlr_minimize_view(void* c_view);
void havel_wlr_focus_view(void* c_view);
void havel_wlr_raise_view(void* c_view);
void havel_wlr_close_view(void* c_view);

// View texture capture (for Alt-Tab thumbnails)
uint32_t havel_wlr_capture_view_texture(void* c_view);

// Script-driven key injection (io.injectKey): feeds a synthetic key
// event through the seat's current keyboard - updates xkb state and
// emits events.key, running the full compositor dispatch chain.
void havel_wlr_inject_key(struct havel_wlr_server *server, uint32_t keycode, bool pressed);

// Cursor control (IPC warp_cursor / click_pointer, io.* helpers). Moves
// the real wlr_cursor and runs the same motion/button dispatch chain the
// hardware path uses, so plugins (Alt-Tab hover/buttons, hot corners)
// see it.
void havel_wlr_warp_pointer(struct havel_wlr_server *server, double x, double y);
void havel_wlr_click_pointer(struct havel_wlr_server *server, uint32_t button, bool pressed);
// Feed a scroll tick through the plugin dispatch path (same as a wheel).
void havel_wlr_scroll_pointer(struct havel_wlr_server *server, double dx, double dy);
void havel_wlr_get_pointer(struct havel_wlr_server *server, double *x, double *y);
// Cursor visibility and theme (IPC hide_cursor / show_cursor /
// set_cursor_theme).
void havel_wlr_hide_cursor(struct havel_wlr_server *server);
void havel_wlr_show_cursor(struct havel_wlr_server *server);
void havel_wlr_set_cursor_theme(struct havel_wlr_server *server,
                                const char *theme, int size);
// Force software cursors on all outputs (false = let wlroots use planes).
void havel_wlr_set_software_cursor(struct havel_wlr_server *server, bool forced);
// Wallpaper image (ARGB8888 CPU pixels), rendered behind all windows.
// Pass NULL to clear back to the solid background colour.
void havel_wlr_set_wallpaper(struct havel_wlr_server *server,
                             const uint32_t* pixels, int w, int h);

// Capture a view's current buffer as CPU ARGB8888 (premultiplied)
// pixels - window thumbnails for the Alt-Tab grid. false/NULL when the
// buffer is not CPU-readable (DMA-BUF) or transformed/scaled.
bool havel_wlr_capture_view_pixels(void* c_view,
                                   uint32_t** out_pixels, int* out_w, int* out_h);
void havel_wlr_capture_pixels_free(uint32_t* pixels);

// Zoom query (per-output; rendered in output_frame via scale + offset)
float havel_wlr_get_zoom(struct havel_wlr_server *server, int output_index);

// Zoom setter (per-output, cursor-centred scale + offset; rendered in
// output_frame). centre < 0 means "follow the live cursor".
void havel_wlr_set_zoom_for_output(struct havel_wlr_server *server, int output_index,
                                    float zoom, float center_x, float center_y);

// Output geometry query (layout position + mode size + name + primary)
bool havel_wlr_get_output_info(struct havel_wlr_server *server, int output_index,
                                int* x, int* y, int* w, int* h,
                                char* name, size_t name_size, int* primary);

// Start the persistent self-injection keyboard (background wayland-client
// connection to the compositor's own socket)
void havel_self_kbd_start(void);

#ifdef __cplusplus
}

// C++ declarations for callback function types
using ViewSetPositionFn = cpp_view_set_position_fn;
using ViewSetSizeFn = cpp_view_set_size_fn;
using ViewFocusFn = cpp_view_focus_fn;
using ViewRaiseFn = cpp_view_raise_fn;
using ViewGetGeometryFn = cpp_view_get_geometry_fn;
using ViewCloseFn = cpp_view_close_fn;
using ViewSetFullscreenFn = cpp_view_set_fullscreen_fn;
using ViewMinimizeFn = cpp_view_minimize_fn;
using WorkspaceArrangeFn = cpp_workspace_arrange_fn;
using WorkspaceSetActiveFn = cpp_workspace_set_active_fn;
using ServerQuitFn = cpp_server_quit_fn;
using ServerSpawnFn = cpp_server_spawn_fn;

// C++ declarations for callback pointers (defined in wlr_bridge.c with C linkage)
extern "C" {
    extern ViewSetPositionFn g_view_set_position;
    extern ViewSetSizeFn g_view_set_size;
    extern ViewFocusFn g_view_focus;
    extern ViewRaiseFn g_view_raise;
    extern ViewGetGeometryFn g_view_get_geometry;
    extern ViewCloseFn g_view_close;
    extern ViewSetFullscreenFn g_view_set_fullscreen;
    extern ViewMinimizeFn g_view_minimize;
    extern WorkspaceArrangeFn g_workspace_arrange;
    extern WorkspaceSetActiveFn g_workspace_set_active;
    extern ServerQuitFn g_server_quit;
    extern ServerSpawnFn g_server_spawn;
}
#endif
