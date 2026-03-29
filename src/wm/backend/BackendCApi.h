// Backend C API - Clean C interface for C++ consumption
// NO wlroots headers - pure C types and opaque pointers

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// Opaque Handles - C++ never sees wlroots internals
// ============================================================================

typedef struct havel_backend havel_backend_t;
typedef struct havel_output havel_output_t;
typedef struct havel_keyboard havel_keyboard_t;
typedef struct havel_pointer havel_pointer_t;
typedef struct havel_xdg_view havel_xdg_view_t;
typedef struct havel_layer_surface havel_layer_surface_t;

// ============================================================================
// Backend Lifecycle
// ============================================================================

// Create backend (call from C++ main)
havel_backend_t* havel_backend_create(void* display);  // wl_display*
void havel_backend_destroy(havel_backend_t* backend);
void havel_backend_start(havel_backend_t* backend);
void havel_backend_stop(havel_backend_t* backend);
bool havel_backend_is_running(havel_backend_t* backend);

// ============================================================================
// Output API
// ============================================================================

typedef struct {
    const char* name;
    int width;
    int height;
    float refresh;
    float scale;
    bool is_primary;
    bool enabled;
} havel_output_info_t;

// Get output count
size_t havel_backend_get_output_count(havel_backend_t* backend);

// Get output info by index
bool havel_backend_get_output_info(havel_backend_t* backend, size_t index, havel_output_info_t* info);

// Output control
void havel_output_set_gamma(havel_output_t* output, float gamma);
void havel_output_set_temperature(havel_output_t* output, int kelvin);
void havel_output_set_brightness(havel_output_t* output, float brightness);
void havel_output_set_zoom(havel_output_t* output, float zoom);

// ============================================================================
// Input API
// ============================================================================

typedef struct {
    const char* name;
    uint32_t vendor;
    uint32_t product;
    bool is_keyboard;
    bool is_pointer;
} havel_input_info_t;

// Get keyboard count
size_t havel_backend_get_keyboard_count(havel_backend_t* backend);

// Get keyboard info
bool havel_backend_get_keyboard_info(havel_backend_t* backend, size_t index, havel_input_info_t* info);

// Get keyboard modifiers
uint32_t havel_keyboard_get_modifiers(havel_keyboard_t* keyboard);

// Get pointer count
size_t havel_backend_get_pointer_count(havel_backend_t* backend);

// ============================================================================
// View API
// ============================================================================

typedef struct {
    const char* app_id;
    const char* title;
    int x;
    int y;
    int width;
    int height;
    bool mapped;
    bool maximized;
    bool fullscreen;
    bool minimized;
} havel_view_info_t;

// Get XDG view count
size_t havel_backend_get_xdg_view_count(havel_backend_t* backend);

// Get view info by index
bool havel_backend_get_xdg_view_info(havel_backend_t* backend, size_t index, havel_view_info_t* info);

// View control
void havel_xdg_view_set_position(havel_xdg_view_t* view, int x, int y);
void havel_xdg_view_set_size(havel_xdg_view_t* view, int width, int height);
void havel_xdg_view_set_activated(havel_xdg_view_t* view, bool activated);
void havel_xdg_view_set_maximized(havel_xdg_view_t* view, bool maximized);
void havel_xdg_view_set_fullscreen(havel_xdg_view_t* view, bool fullscreen);
void havel_xdg_view_set_minimized(havel_xdg_view_t* view, bool minimized);
void havel_xdg_view_raise_to_top(havel_xdg_view_t* view);

// ============================================================================
// Workspace API
// ============================================================================

uint32_t havel_backend_get_active_workspace(havel_backend_t* backend);
void havel_backend_set_active_workspace(havel_backend_t* backend, uint32_t workspace);

// ============================================================================
// Cursor API
// ============================================================================

typedef struct {
    double x;
    double y;
} havel_cursor_pos_t;

havel_cursor_pos_t havel_backend_get_cursor_position(havel_backend_t* backend);
void havel_backend_warp_cursor(havel_backend_t* backend, double x, double y);
void havel_backend_set_cursor_theme(havel_backend_t* backend, const char* theme, int size);

// ============================================================================
// Scene Graph Access (for rendering)
// ============================================================================

// Get scene graph root (for wlroots rendering)
void* havel_backend_get_scene(havel_backend_t* backend);  // wlr_scene*

// Get output scene (for committing)
void* havel_backend_get_output_scene(havel_backend_t* backend, size_t index);  // wlr_scene_output*

// Get output layout
void* havel_backend_get_output_layout(havel_backend_t* backend);  // wlr_output_layout*

// ============================================================================
// C++ Bridge (opaque server pointer)
// ============================================================================

void havel_backend_set_cpp_server(havel_backend_t* backend, void* server);
void* havel_backend_get_cpp_server(havel_backend_t* backend);

// ============================================================================
// Event Callbacks (C++ can register handlers)
// ============================================================================

typedef void (*havel_output_add_callback)(void* user_data, havel_output_t* output);
typedef void (*havel_output_remove_callback)(void* user_data, havel_output_t* output);
typedef void (*havel_keyboard_add_callback)(void* user_data, havel_keyboard_t* keyboard);
typedef void (*havel_pointer_add_callback)(void* user_data, havel_pointer_t* pointer);
typedef void (*havel_view_add_callback)(void* user_data, havel_xdg_view_t* view);
typedef void (*havel_view_remove_callback)(void* user_data, havel_xdg_view_t* view);

void havel_backend_set_output_add_callback(havel_backend_t* backend, havel_output_add_callback cb, void* user_data);
void havel_backend_set_output_remove_callback(havel_backend_t* backend, havel_output_remove_callback cb, void* user_data);
void havel_backend_set_keyboard_add_callback(havel_backend_t* backend, havel_keyboard_add_callback cb, void* user_data);
void havel_backend_set_pointer_add_callback(havel_backend_t* backend, havel_pointer_add_callback cb, void* user_data);
void havel_backend_set_view_add_callback(havel_backend_t* backend, havel_view_add_callback cb, void* user_data);
void havel_backend_set_view_remove_callback(havel_backend_t* backend, havel_view_remove_callback cb, void* user_data);

#ifdef __cplusplus
}
#endif
