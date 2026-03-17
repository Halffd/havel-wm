#pragma once

#include <stdint.h>
#include <stdbool.h>

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

// Create/destroy C++ server
struct havel_cpp_server* havel_cpp_server_create(void);
void havel_cpp_server_destroy(struct havel_cpp_server* server);

// Get native handle for storing in C struct
void* havel_cpp_server_get_native_handle(struct havel_cpp_server* server);
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
void havel_cpp_on_view_destroyed(struct havel_cpp_server* server, void* c_view);

// Input events - returns true if consumed by compositor
bool havel_cpp_on_key(struct havel_cpp_server* server, uint32_t keycode, bool pressed, uint32_t modifiers, uint32_t keysym, char key_char, const char* utf8);
void havel_cpp_on_pointer_button(struct havel_cpp_server* server, uint32_t button, bool pressed, double x, double y);
void havel_cpp_on_pointer_motion(struct havel_cpp_server* server, double x, double y);
void havel_cpp_on_pointer_decoration_motion(struct havel_cpp_server* server, int x, int y);
void havel_cpp_on_pointer_decoration_button(struct havel_cpp_server* server, uint32_t button, bool pressed, int x, int y);

// Output/workspace events
void havel_cpp_set_output_geometry(struct havel_cpp_server* server, uint32_t workspace_id, int x, int y, int w, int h);
void havel_cpp_set_active_workspace(struct havel_cpp_server* server, uint32_t workspace_id);

// Animation updates (called from C frame handler)
void havel_cpp_update_animations(struct havel_cpp_server* server);

// Plugin event dispatch (called from C)
void havel_cpp_dispatch_output_frame(struct havel_cpp_server* server, void* output, void* sceneOutput);

// Background color (called from C for clear color)
void havel_cpp_get_background_color(struct havel_cpp_server* server, float* r, float* g, float* b);

// Gamma/temperature control
void havel_cpp_set_gamma(struct havel_cpp_server* server, float gamma);
void havel_cpp_set_temperature(struct havel_cpp_server* server, int kelvin);
void havel_cpp_set_brightness(struct havel_cpp_server* server, float brightness);

// Overlay rendering
void havel_cpp_draw_overlays(struct havel_cpp_server* server, int width, int height);
void* havel_cpp_get_plugin_manager(struct havel_cpp_server* server);

// Alt-Tab window selection
void havel_cpp_alt_tab_select(struct havel_cpp_server* server, int index);

// Application spawning (for App Launcher)
void havel_cpp_server_spawn(struct havel_cpp_server* server, const char* command);

// Gesture recognition
void havel_cpp_init_gestures(struct havel_cpp_server* server);
void havel_cpp_process_gesture_motion(struct havel_cpp_server* server, double x, double y, uint64_t timestamp);
void havel_cpp_process_gesture_button(struct havel_cpp_server* server, int button, bool pressed, double x, double y, uint64_t timestamp);

// Combo system
void havel_cpp_process_combo_key(struct havel_cpp_server* server, uint32_t keycode, bool pressed, uint32_t modifiers);

// IPC server events
void havel_cpp_process_ipc_events(struct havel_cpp_server* server);

// Desktop management
void havel_cpp_init_desktop(struct havel_cpp_server* server);
void* havel_cpp_get_desktop_manager(struct havel_cpp_server* server);
void havel_cpp_process_desktop_mouse(struct havel_cpp_server* server, int button, bool pressed, int x, int y);
void havel_cpp_process_desktop_motion(struct havel_cpp_server* server, int x, int y);
void havel_cpp_process_desktop_key(struct havel_cpp_server* server, uint32_t keycode, uint32_t modifiers);

// Window group management
void havel_cpp_init_window_groups(struct havel_cpp_server* server);
void* havel_cpp_get_window_group_manager(struct havel_cpp_server* server);

// C layer gamma application (global - all monitors)
void havel_wlr_set_gamma(havel_wlr_server_t* server, float gamma);
void havel_wlr_set_temperature(havel_wlr_server_t* server, int kelvin);
void havel_wlr_set_brightness(havel_wlr_server_t* server, float brightness);

// Per-monitor gamma/brightness control
void havel_wlr_set_gamma_for_output(havel_wlr_server_t* server, int output_index, float gamma);
void havel_wlr_set_temperature_for_output(havel_wlr_server_t* server, int output_index, int kelvin);
void havel_wlr_set_brightness_for_output(havel_wlr_server_t* server, int output_index, float brightness);

// Texture access for Alt-Tab thumbnails (called from C++ PluginManager)
uint32_t havel_get_view_texture_id(void* c_view);
int havel_get_view_texture_width(void* c_view);
int havel_get_view_texture_height(void* c_view);

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

// Per-monitor control functions
void havel_cpp_set_gamma_for_output(struct havel_cpp_server* server, int output_index, float gamma);
void havel_cpp_set_temperature_for_output(struct havel_cpp_server* server, int output_index, int kelvin);
void havel_cpp_set_brightness_for_output(struct havel_cpp_server* server, int output_index, float brightness);

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

// C++ declarations for callback pointers (defined in bridge.cpp)
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
#endif
