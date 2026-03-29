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
typedef void (*havel_keyboard_remove_callback)(void* user_data, havel_keyboard_t* keyboard);
typedef void (*havel_pointer_add_callback)(void* user_data, havel_pointer_t* pointer);
typedef void (*havel_pointer_remove_callback)(void* user_data, havel_pointer_t* pointer);
typedef void (*havel_view_add_callback)(void* user_data, havel_xdg_view_t* view);
typedef void (*havel_view_remove_callback)(void* user_data, havel_xdg_view_t* view);
typedef void (*havel_layer_add_callback)(void* user_data, havel_layer_surface_t* layer);
typedef void (*havel_layer_remove_callback)(void* user_data, havel_layer_surface_t* layer);

void havel_backend_set_output_add_callback(havel_backend_t* backend, havel_output_add_callback cb, void* user_data);
void havel_backend_set_output_remove_callback(havel_backend_t* backend, havel_output_remove_callback cb, void* user_data);
void havel_backend_set_keyboard_add_callback(havel_backend_t* backend, havel_keyboard_add_callback cb, void* user_data);
void havel_backend_set_keyboard_remove_callback(havel_backend_t* backend, havel_keyboard_remove_callback cb, void* user_data);
void havel_backend_set_pointer_add_callback(havel_backend_t* backend, havel_pointer_add_callback cb, void* user_data);
void havel_backend_set_pointer_remove_callback(havel_backend_t* backend, havel_pointer_remove_callback cb, void* user_data);
void havel_backend_set_view_add_callback(havel_backend_t* backend, havel_view_add_callback cb, void* user_data);
void havel_backend_set_view_remove_callback(havel_backend_t* backend, havel_view_remove_callback cb, void* user_data);
void havel_backend_set_layer_add_callback(havel_backend_t* backend, havel_layer_add_callback cb, void* user_data);
void havel_backend_set_layer_remove_callback(havel_backend_t* backend, havel_layer_remove_callback cb, void* user_data);

// ============================================================================
// Output Configuration (Advanced)
// ============================================================================

typedef struct {
    int32_t width;
    int32_t height;
    int32_t refresh;  // mHz
    bool preferred;
} havel_output_mode_t;

size_t havel_output_get_mode_count(havel_output_t* output);
bool havel_output_get_mode(havel_output_t* output, size_t index, havel_output_mode_t* mode);
bool havel_output_set_mode(havel_output_t* output, size_t mode_index);
bool havel_output_set_custom_mode(havel_output_t* output, int32_t width, int32_t height, int32_t refresh);
bool havel_output_enable(havel_output_t* output, bool enabled);
bool havel_output_set_scale(havel_output_t* output, float scale);
bool havel_output_set_transform(havel_output_t* output, int32_t transform);  // wl_output_transform
const char* havel_output_get_make(havel_output_t* output);
const char* havel_output_get_model(havel_output_t* output);
const char* havel_output_get_serial(havel_output_t* output);

// ============================================================================
// Input Device Configuration
// ============================================================================

typedef struct {
    int32_t repeat_rate;     // key repeats per second
    int32_t repeat_delay;    // delay before repeat in ms
} havel_keyboard_config_t;

bool havel_keyboard_set_config(havel_keyboard_t* keyboard, const havel_keyboard_config_t* config);
bool havel_keyboard_get_config(havel_keyboard_t* keyboard, havel_keyboard_config_t* config);
const char* havel_keyboard_get_keymap_as_string(havel_keyboard_t* keyboard);  // Caller must free
bool havel_keyboard_led_update(havel_keyboard_t* keyboard, uint32_t leds);  // xkb_led_mask

// ============================================================================
// Layer Surface API
// ============================================================================

typedef struct {
    const char* ns;
    uint32_t layer;  // zwlr_layer_shell_v1_layer
    uint32_t anchor;
    int32_t x;
    int32_t y;
    int32_t width;
    int32_t height;
    bool exclusive_zone;
    bool mapped;
} havel_layer_info_t;

size_t havel_backend_get_layer_count(havel_backend_t* backend);
bool havel_backend_get_layer_info(havel_backend_t* backend, size_t index, havel_layer_info_t* info);
void havel_layer_set_exclusive_zone(havel_layer_surface_t* layer, int32_t zone);
void havel_layer_set_anchor(havel_layer_surface_t* layer, uint32_t anchor);
void havel_layer_set_margin(havel_layer_surface_t* layer, int32_t top, int32_t right, int32_t bottom, int32_t left);
void havel_layer_set_keyboard_interactivity(havel_layer_surface_t* layer, bool interactive);

// ============================================================================
// Damage Tracking (for efficient rendering)
// ============================================================================

typedef struct {
    int32_t x;
    int32_t y;
    int32_t width;
    int32_t height;
} havel_damage_rect_t;

typedef struct {
    havel_damage_rect_t* rects;
    size_t count;
    uint32_t age;  // Buffer age (0 = full redraw needed)
} havel_damage_info_t;

bool havel_output_get_damage(havel_output_t* output, havel_damage_info_t* damage);
void havel_output_damage_done(havel_output_t* output);
void havel_output_damage_add(havel_output_t* output, int32_t x, int32_t y, int32_t width, int32_t height);
void havel_output_damage_add_whole(havel_output_t* output);

// ============================================================================
// Clipboard and Data Device
// ============================================================================

typedef enum {
    HAVEL_SELECTION_NONE = 0,
    HAVEL_SELECTION_CLIPBOARD,
    HAVEL_SELECTION_PRIMARY,
    HAVEL_SELECTION_DND,
} havel_selection_type_t;

typedef struct {
    havel_selection_type_t type;
    const char* mime_type;
    const void* data;
    size_t size;
} havel_selection_data_t;

bool havel_backend_get_selection(havel_backend_t* backend, havel_selection_type_t type, havel_selection_data_t* data);
bool havel_backend_set_selection(havel_backend_t* backend, havel_selection_type_t type, const char* mime_type, const void* data, size_t size);
void havel_backend_clear_selection(havel_backend_t* backend, havel_selection_type_t type);

// ============================================================================
// Idle and Power Management
// ============================================================================

uint32_t havel_backend_get_idle_time(havel_backend_t* backend);  // ms since last input
void havel_backend_reset_idle_timer(havel_backend_t* backend);
bool havel_output_set_power_mode(havel_output_t* output, uint32_t mode);  // 0=off, 1=on
uint32_t havel_output_get_power_mode(havel_output_t* output);

// ============================================================================
// Screen Recording (Screencopy)
// ============================================================================

typedef struct {
    void* data;           // Buffer pointer (caller provides)
    size_t size;          // Buffer size
    uint32_t format;      // DRM_FORMAT_*
    uint32_t stride;      // Bytes per line
    int32_t width;
    int32_t height;
} havel_screencopy_buffer_t;

bool havel_screencopy_output(havel_backend_t* backend, havel_output_t* output, havel_screencopy_buffer_t* buffer);
bool havel_screencopy_region(havel_backend_t* backend, havel_output_t* output, 
                              int32_t x, int32_t y, int32_t width, int32_t height,
                              havel_screencopy_buffer_t* buffer);

// ============================================================================
// Texture Import (for thumbnails, screen capture, plugins)
// ============================================================================

typedef enum {
    HAVEL_TEXTURE_INVALID = 0,
    HAVEL_TEXTURE_SHM,      // Shared memory buffer
    HAVEL_TEXTURE_DMA_BUF,  // DMA-BUF file descriptor
    HAVEL_TEXTURE_GL,       // OpenGL texture ID
} havel_texture_type_t;

typedef struct {
    havel_texture_type_t type;
    union {
        struct {
            void* data;
            uint32_t format;  // WL_SHM_FORMAT_*
            uint32_t stride;
        } shm;
        struct {
            int fd;
            uint32_t width;
            uint32_t height;
            uint32_t stride;
            uint64_t modifier;
        } dma_buf;
        struct {
            uint32_t gl_texture_id;
            uint32_t width;
            uint32_t height;
        } gl;
    };
} havel_texture_import_info_t;

uint32_t havel_texture_import(havel_backend_t* backend, const havel_texture_import_info_t* info);
void havel_texture_destroy(havel_backend_t* backend, uint32_t texture_id);
bool havel_texture_bind(havel_backend_t* backend, uint32_t texture_id);

// ============================================================================
// Foreign Toplevel Management (window control from other apps)
// ============================================================================

typedef struct {
    const char* app_id;
    const char* title;
    uint32_t workspace;
    bool activated;
    bool maximized;
    bool minimized;
    bool fullscreen;
    int32_t x;
    int32_t y;
    int32_t width;
    int32_t height;
} havel_foreign_toplevel_info_t;

size_t havel_backend_get_foreign_toplevel_count(havel_backend_t* backend);
bool havel_backend_get_foreign_toplevel_info(havel_backend_t* backend, size_t index, havel_foreign_toplevel_info_t* info);
void havel_foreign_toplevel_activate(havel_backend_t* backend, size_t index);
void havel_foreign_toplevel_close(havel_backend_t* backend, size_t index);
void havel_foreign_toplevel_set_maximized(havel_backend_t* backend, size_t index, bool maximized);
void havel_foreign_toplevel_set_minimized(havel_backend_t* backend, size_t index, bool minimized);

// ============================================================================
// Virtual Input (for testing, automation, remote control)
// ============================================================================

havel_keyboard_t* havel_keyboard_create_virtual(havel_backend_t* backend);
havel_pointer_t* havel_pointer_create_virtual(havel_backend_t* backend);
void havel_keyboard_destroy_virtual(havel_keyboard_t* keyboard);
void havel_pointer_destroy_virtual(havel_pointer_t* pointer);

// Virtual keyboard events
void havel_keyboard_virtual_key(havel_keyboard_t* keyboard, uint32_t keycode, bool pressed);
void havel_keyboard_virtual_modifiers(havel_keyboard_t* keyboard, uint32_t modifiers);

// Virtual pointer events
void havel_pointer_virtual_motion(havel_pointer_t* pointer, double dx, double dy);
void havel_pointer_virtual_motion_absolute(havel_pointer_t* pointer, double x, double y);
void havel_pointer_virtual_button(havel_pointer_t* pointer, uint32_t button, bool pressed);
void havel_pointer_virtual_axis(havel_pointer_t* pointer, double dx, double dy);

// ============================================================================
// Renderer Access (for custom rendering, plugins)
// ============================================================================

void* havel_backend_get_renderer(havel_backend_t* backend);  // wlr_renderer*
void* havel_backend_get_allocator(havel_backend_t* backend);  // wlr_allocator*
bool havel_backend_renderer_begin(havel_backend_t* backend, uint32_t width, uint32_t height);
void havel_backend_renderer_end(havel_backend_t* backend);
void havel_backend_renderer_clear(havel_backend_t* backend, float r, float g, float b, float a);

// ============================================================================
// Session and System Integration
// ============================================================================

bool havel_backend_is_session_active(havel_backend_t* backend);
bool havel_backend_change_vt(havel_backend_t* backend, unsigned int vt);
const char* havel_backend_get_seat_name(havel_backend_t* backend);
bool havel_backend_has_capability(havel_backend_t* backend, const char* capability);

// ============================================================================
// Debug and Statistics
// ============================================================================

typedef struct {
    size_t output_count;
    size_t keyboard_count;
    size_t pointer_count;
    size_t view_count;
    size_t layer_count;
    uint64_t frame_count;
    uint32_t uptime_ms;
    float fps;
    size_t gpu_memory_used;  // bytes (estimated)
} havel_backend_stats_t;

bool havel_backend_get_stats(havel_backend_t* backend, havel_backend_stats_t* stats);
void havel_backend_print_debug_info(havel_backend_t* backend);

#ifdef __cplusplus
}
#endif
