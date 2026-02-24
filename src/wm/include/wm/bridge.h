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
typedef void (*cpp_workspace_arrange_fn)(uint32_t workspace_id);
typedef void (*cpp_workspace_set_active_fn)(uint32_t workspace_id);
typedef void (*cpp_server_quit_fn)(void);
typedef void (*cpp_server_spawn_fn)(const char* command);

// Opaque handle to C++ Server
struct havel_cpp_server;

// Create/destroy C++ server
struct havel_cpp_server* havel_cpp_server_create(void);
void havel_cpp_server_destroy(struct havel_cpp_server* server);

// Get native handle for storing in C struct
void* havel_cpp_server_get_native_handle(struct havel_cpp_server* server);

// View lifecycle events (called from C bridge)
void havel_cpp_on_xdg_surface_new(struct havel_cpp_server* server, void* xdg_surface);
void havel_cpp_on_view_mapped(struct havel_cpp_server* server, void* view);
void havel_cpp_on_view_unmapped(struct havel_cpp_server* server, void* view);
void havel_cpp_on_view_destroyed(struct havel_cpp_server* server, void* view);

// Input events
void havel_cpp_on_key(struct havel_cpp_server* server, uint32_t keycode, bool pressed, uint32_t modifiers);
void havel_cpp_on_pointer_button(struct havel_cpp_server* server, uint32_t button, bool pressed, double x, double y);
void havel_cpp_on_pointer_motion(struct havel_cpp_server* server, double x, double y);

// Output/workspace events
void havel_cpp_set_output_geometry(struct havel_cpp_server* server, uint32_t workspace_id, int x, int y, int w, int h);
void havel_cpp_set_active_workspace(struct havel_cpp_server* server, uint32_t workspace_id);

// Animation updates (called from C frame handler)
void havel_cpp_update_animations(struct havel_cpp_server* server);

// Callback registration (called from C during initialization)
void havel_cpp_register_view_callbacks(
    cpp_view_set_position_fn set_position,
    cpp_view_set_size_fn set_size,
    cpp_view_focus_fn focus,
    cpp_view_raise_fn raise,
    cpp_view_get_geometry_fn get_geometry,
    cpp_view_close_fn close,
    cpp_view_set_fullscreen_fn set_fullscreen
);

void havel_cpp_register_workspace_callbacks(
    cpp_workspace_arrange_fn arrange,
    cpp_workspace_set_active_fn set_active
);

void havel_cpp_register_server_callbacks(
    cpp_server_quit_fn quit,
    cpp_server_spawn_fn spawn
);

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
extern WorkspaceArrangeFn g_workspace_arrange;
extern WorkspaceSetActiveFn g_workspace_set_active;
extern ServerQuitFn g_server_quit;
extern ServerSpawnFn g_server_spawn;
#endif
