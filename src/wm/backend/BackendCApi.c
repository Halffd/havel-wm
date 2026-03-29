// Backend C API Implementation
// Clean C interface that hides wlroots from C++

#include "BackendCApi.h"
#include "BackendTypes.h"
#include "output/OutputManager.h"
#include "input/InputHandler.h"
#include "shell/XdgShell.h"
#include "cursor/Cursor.h"
#include <Logger.h>
#include <string.h>
#include <wayland-server-core.h>

// ============================================================================
// Backend Lifecycle
// ============================================================================

havel_backend_t* havel_backend_create(void* display) {
    return (havel_backend_t*)havel_server_create((struct wl_display*)display);
}

void havel_backend_destroy(havel_backend_t* backend) {
    if (!backend) return;
    havel_server_destroy((havel_wlr_server_t*)backend);
}

void havel_backend_start(havel_backend_t* backend) {
    if (!backend) return;
    havel_server_start((havel_wlr_server_t*)backend);
}

void havel_backend_stop(havel_backend_t* backend) {
    // Cleanup handled by destroy
    (void)backend;
}

bool havel_backend_is_running(havel_backend_t* backend) {
    return backend != NULL;
}

// ============================================================================
// Output API
// ============================================================================

size_t havel_backend_get_output_count(havel_backend_t* backend) {
    if (!backend) return 0;
    
    havel_wlr_server_t* server = (havel_wlr_server_t*)backend;
    size_t count = 0;
    
    havel_output_t* output;
    wl_list_for_each(output, &server->outputs, link) {
        count++;
    }
    
    return count;
}

bool havel_backend_get_output_info(havel_backend_t* backend, size_t index, havel_output_info_t* info) {
    if (!backend || !info) return false;
    
    havel_wlr_server_t* server = (havel_wlr_server_t*)backend;
    
    size_t current = 0;
    havel_output_t* output;
    wl_list_for_each(output, &server->outputs, link) {
        if (current == index) {
            info->name = output->output ? output->output->name : "";
            info->width = output->output ? output->output->width : 0;
            info->height = output->output ? output->output->height : 0;
            info->refresh = output->output ? output->output->refresh : 0;
            info->scale = output->output ? output->output->scale : 1.0f;
            info->is_primary = output->is_primary;
            info->enabled = output->output ? output->output->enabled : false;
            return true;
        }
        current++;
    }
    
    return false;
}

void havel_output_set_gamma(havel_output_t* output, float gamma) {
    if (!output) return;
    output->gamma = gamma;
    output->gamma_ramp_dirty = true;
}

void havel_output_set_temperature(havel_output_t* output, int kelvin) {
    if (!output) return;
    output->temperature = kelvin;
    output->gamma_ramp_dirty = true;
}

void havel_output_set_brightness(havel_output_t* output, float brightness) {
    if (!output) return;
    output->brightness = brightness;
}

void havel_output_set_zoom(havel_output_t* output, float zoom) {
    if (!output) return;
    output->prev_zoom = output->zoom;
    output->zoom = zoom;
}

// ============================================================================
// Input API
// ============================================================================

size_t havel_backend_get_keyboard_count(havel_backend_t* backend) {
    if (!backend) return 0;
    
    havel_wlr_server_t* server = (havel_wlr_server_t*)backend;
    size_t count = 0;
    
    havel_keyboard_t* keyboard;
    wl_list_for_each(keyboard, &server->keyboards, link) {
        count++;
    }
    
    return count;
}

bool havel_backend_get_keyboard_info(havel_backend_t* backend, size_t index, havel_input_info_t* info) {
    if (!backend || !info) return false;
    
    havel_wlr_server_t* server = (havel_wlr_server_t*)backend;
    
    size_t current = 0;
    havel_keyboard_t* keyboard;
    wl_list_for_each(keyboard, &server->keyboards, link) {
        if (current == index) {
            info->name = keyboard->keyboard ? keyboard->keyboard->base.name : "";
            // wlroots 0.20 removed vendor/product from wlr_input_device
            info->vendor = 0;
            info->product = 0;
            info->is_keyboard = true;
            info->is_pointer = false;
            return true;
        }
        current++;
    }
    
    return false;
}

uint32_t havel_keyboard_get_modifiers(havel_keyboard_t* keyboard) {
    if (!keyboard || !keyboard->keyboard) return 0;
    return keyboard->keyboard->modifiers.depressed;
}

size_t havel_backend_get_pointer_count(havel_backend_t* backend) {
    if (!backend) return 0;
    
    havel_wlr_server_t* server = (havel_wlr_server_t*)backend;
    size_t count = 0;
    
    havel_pointer_t* pointer;
    wl_list_for_each(pointer, &server->pointers, link) {
        count++;
    }
    
    return count;
}

// ============================================================================
// View API
// ============================================================================

size_t havel_backend_get_xdg_view_count(havel_backend_t* backend) {
    // Note: Views are tracked in C++ layer, not C layer
    // This would need a view list in havel_wlr_server_t
    (void)backend;
    return 0;
}

bool havel_backend_get_xdg_view_info(havel_backend_t* backend, size_t index, havel_view_info_t* info) {
    (void)backend;
    (void)index;
    (void)info;
    return false;
}

void havel_xdg_view_set_position(havel_xdg_view_t* view, int x, int y) {
    if (!view || !view->scene_tree) return;
    wlr_scene_node_set_position(&view->scene_tree->node, x, y);
}

void havel_xdg_view_set_size(havel_xdg_view_t* view, int width, int height) {
    if (!view || !view->xdg_surface || !view->xdg_surface->toplevel) return;
    wlr_xdg_toplevel_set_size(view->xdg_surface->toplevel, width, height);
}

void havel_xdg_view_set_activated(havel_xdg_view_t* view, bool activated) {
    if (!view || !view->xdg_surface || !view->xdg_surface->toplevel) return;
    wlr_xdg_toplevel_set_activated(view->xdg_surface->toplevel, activated);
}

void havel_xdg_view_set_maximized(havel_xdg_view_t* view, bool maximized) {
    if (!view || !view->xdg_surface || !view->xdg_surface->toplevel) return;
    wlr_xdg_toplevel_set_maximized(view->xdg_surface->toplevel, maximized);
}

void havel_xdg_view_set_fullscreen(havel_xdg_view_t* view, bool fullscreen) {
    if (!view || !view->xdg_surface || !view->xdg_surface->toplevel) return;
    wlr_xdg_toplevel_set_fullscreen(view->xdg_surface->toplevel, fullscreen);
}

void havel_xdg_view_set_minimized(havel_xdg_view_t* view, bool minimized) {
    if (!view || !view->scene_tree) return;
    wlr_scene_node_set_enabled(&view->scene_tree->node, !minimized);
}

void havel_xdg_view_raise_to_top(havel_xdg_view_t* view) {
    if (!view || !view->scene_tree) return;
    wlr_scene_node_raise_to_top(&view->scene_tree->node);
}

// ============================================================================
// Workspace API
// ============================================================================

uint32_t havel_backend_get_active_workspace(havel_backend_t* backend) {
    if (!backend) return 0;
    return ((havel_wlr_server_t*)backend)->active_workspace;
}

void havel_backend_set_active_workspace(havel_backend_t* backend, uint32_t workspace) {
    if (!backend || workspace >= 10) return;
    
    havel_wlr_server_t* server = (havel_wlr_server_t*)backend;
    
    // Disable all workspaces
    for (uint32_t i = 0; i < 10; i++) {
        wlr_scene_node_set_enabled(&server->workspaces[i]->node, false);
    }
    
    // Enable only active workspace
    wlr_scene_node_set_enabled(&server->workspaces[workspace]->node, true);
    server->active_workspace = workspace;
    
    LOG_INFO("[Backend] Switched to workspace %u", workspace);
}

// ============================================================================
// Cursor API
// ============================================================================

havel_cursor_pos_t havel_backend_get_cursor_position(havel_backend_t* backend) {
    havel_cursor_pos_t pos = {0, 0};
    
    if (!backend) return pos;
    
    havel_wlr_server_t* server = (havel_wlr_server_t*)backend;
    if (server->cursor) {
        pos.x = server->cursor->x;
        pos.y = server->cursor->y;
    }
    
    return pos;
}

void havel_backend_warp_cursor(havel_backend_t* backend, double x, double y) {
    if (!backend) return;
    
    havel_wlr_server_t* server = (havel_wlr_server_t*)backend;
    if (server->cursor) {
        wlr_cursor_warp_closest(server->cursor, NULL, x, y);
    }
}

void havel_backend_set_cursor_theme(havel_backend_t* backend, const char* theme, int size) {
    if (!backend) return;
    
    havel_wlr_server_t* server = (havel_wlr_server_t*)backend;
    if (server->cursor_mgr) {
        wlr_xcursor_manager_destroy(server->cursor_mgr);
    }
    server->cursor_mgr = wlr_xcursor_manager_create(theme, size);
}

// ============================================================================
// Scene Graph Access
// ============================================================================

void* havel_backend_get_scene(havel_backend_t* backend) {
    if (!backend) return NULL;
    return ((havel_wlr_server_t*)backend)->scene;
}

void* havel_backend_get_output_scene(havel_backend_t* backend, size_t index) {
    if (!backend) return NULL;
    
    havel_wlr_server_t* server = (havel_wlr_server_t*)backend;
    
    size_t current = 0;
    havel_output_t* output;
    wl_list_for_each(output, &server->outputs, link) {
        if (current == index) {
            return output->scene_output;
        }
        current++;
    }
    
    return NULL;
}

void* havel_backend_get_output_layout(havel_backend_t* backend) {
    if (!backend) return NULL;
    return ((havel_wlr_server_t*)backend)->output_layout;
}

// ============================================================================
// C++ Bridge
// ============================================================================

void havel_backend_set_cpp_server(havel_backend_t* backend, void* server) {
    if (!backend) return;
    ((havel_wlr_server_t*)backend)->cpp_server = server;
}

void* havel_backend_get_cpp_server(havel_backend_t* backend) {
    if (!backend) return NULL;
    return ((havel_wlr_server_t*)backend)->cpp_server;
}

// ============================================================================
// Event Callbacks (stubs - would need callback storage in backend)
// ============================================================================

void havel_backend_set_output_add_callback(havel_backend_t* backend, havel_output_add_callback cb, void* user_data) {
    (void)backend; (void)cb; (void)user_data;
}

void havel_backend_set_output_remove_callback(havel_backend_t* backend, havel_output_remove_callback cb, void* user_data) {
    (void)backend; (void)cb; (void)user_data;
}

void havel_backend_set_keyboard_add_callback(havel_backend_t* backend, havel_keyboard_add_callback cb, void* user_data) {
    (void)backend; (void)cb; (void)user_data;
}

void havel_backend_set_pointer_add_callback(havel_backend_t* backend, havel_pointer_add_callback cb, void* user_data) {
    (void)backend; (void)cb; (void)user_data;
}

void havel_backend_set_view_add_callback(havel_backend_t* backend, havel_view_add_callback cb, void* user_data) {
    (void)backend; (void)cb; (void)user_data;
}

void havel_backend_set_view_remove_callback(havel_backend_t* backend, havel_view_remove_callback cb, void* user_data) {
    (void)backend; (void)cb; (void)user_data;
}

void havel_backend_set_layer_add_callback(havel_backend_t* backend, havel_layer_add_callback cb, void* user_data) {
    (void)backend; (void)cb; (void)user_data;
}

void havel_backend_set_layer_remove_callback(havel_backend_t* backend, havel_layer_remove_callback cb, void* user_data) {
    (void)backend; (void)cb; (void)user_data;
}

// ============================================================================
// Output Configuration (Advanced)
// ============================================================================

size_t havel_output_get_mode_count(havel_output_t* output) {
    if (!output || !output->output) return 0;
    return wl_list_length(&output->output->modes);
}

bool havel_output_get_mode(havel_output_t* output, size_t index, havel_output_mode_t* mode) {
    if (!output || !output->output || !mode) return false;
    
    size_t current = 0;
    struct wlr_output_mode* m;
    wl_list_for_each(m, &output->output->modes, link) {
        if (current == index) {
            mode->width = m->width;
            mode->height = m->height;
            mode->refresh = m->refresh;
            // wlroots 0.20: preferred_mode removed, use first mode as preferred
            mode->preferred = (current == 0);
            return true;
        }
        current++;
    }
    return false;
}

bool havel_output_set_mode(havel_output_t* output, size_t mode_index) {
    if (!output || !output->output) return false;
    
    size_t current = 0;
    struct wlr_output_mode* m;
    wl_list_for_each(m, &output->output->modes, link) {
        if (current == mode_index) {
            struct wlr_output_state state;
            wlr_output_state_init(&state);
            wlr_output_state_set_mode(&state, m);
            bool result = wlr_output_commit_state(output->output, &state);
            wlr_output_state_finish(&state);
            return result;
        }
        current++;
    }
    return false;
}

bool havel_output_set_custom_mode(havel_output_t* output, int32_t width, int32_t height, int32_t refresh) {
    if (!output || !output->output) return false;
    
    struct wlr_output_state state;
    wlr_output_state_init(&state);
    wlr_output_state_set_custom_mode(&state, width, height, refresh);
    bool result = wlr_output_commit_state(output->output, &state);
    wlr_output_state_finish(&state);
    return result;
}

bool havel_output_enable(havel_output_t* output, bool enabled) {
    if (!output || !output->output) return false;
    
    struct wlr_output_state state;
    wlr_output_state_init(&state);
    wlr_output_state_set_enabled(&state, enabled);
    bool result = wlr_output_commit_state(output->output, &state);
    wlr_output_state_finish(&state);
    return result;
}

bool havel_output_set_scale(havel_output_t* output, float scale) {
    if (!output || !output->output) return false;
    
    struct wlr_output_state state;
    wlr_output_state_init(&state);
    wlr_output_state_set_scale(&state, scale);
    bool result = wlr_output_commit_state(output->output, &state);
    wlr_output_state_finish(&state);
    return result;
}

bool havel_output_set_transform(havel_output_t* output, int32_t transform) {
    if (!output || !output->output) return false;
    
    struct wlr_output_state state;
    wlr_output_state_init(&state);
    wlr_output_state_set_transform(&state, transform);
    bool result = wlr_output_commit_state(output->output, &state);
    wlr_output_state_finish(&state);
    return result;
}

const char* havel_output_get_make(havel_output_t* output) {
    if (!output || !output->output) return "";
    return output->output->make ? output->output->make : "";
}

const char* havel_output_get_model(havel_output_t* output) {
    if (!output || !output->output) return "";
    return output->output->model ? output->output->model : "";
}

const char* havel_output_get_serial(havel_output_t* output) {
    if (!output || !output->output) return "";
    return output->output->serial ? output->output->serial : "";
}

// ============================================================================
// Input Device Configuration
// ============================================================================

bool havel_keyboard_set_config(havel_keyboard_t* keyboard, const havel_keyboard_config_t* config) {
    if (!keyboard || !config) return false;
    wlr_keyboard_set_repeat_info(keyboard->keyboard, config->repeat_rate, config->repeat_delay);
    return true;
}

bool havel_keyboard_get_config(havel_keyboard_t* keyboard, havel_keyboard_config_t* config) {
    if (!keyboard || !config) return false;
    // wlroots 0.20: no getter for repeat info, use defaults
    config->repeat_rate = 25;
    config->repeat_delay = 600;
    return true;
}

const char* havel_keyboard_get_keymap_as_string(havel_keyboard_t* keyboard) {
    if (!keyboard || !keyboard->keymap) return NULL;
    return xkb_keymap_get_as_string(keyboard->keymap, XKB_KEYMAP_FORMAT_TEXT_V1);
}

bool havel_keyboard_led_update(havel_keyboard_t* keyboard, uint32_t leds) {
    if (!keyboard) return false;
    // LEDs are typically updated by wlroots automatically
    // This would require direct xkb_state manipulation
    (void)leds;
    return false;
}

// ============================================================================
// Layer Surface API
// ============================================================================

size_t havel_backend_get_layer_count(havel_backend_t* backend) {
    // Layer surfaces tracked in C++ layer
    (void)backend;
    return 0;
}

bool havel_backend_get_layer_info(havel_backend_t* backend, size_t index, havel_layer_info_t* info) {
    (void)backend; (void)index; (void)info;
    return false;
}

void havel_layer_set_exclusive_zone(havel_layer_surface_t* layer, int32_t zone) {
    if (!layer || !layer->layer_surface) return;
    layer->layer_surface->current.exclusive_zone = zone;
}

void havel_layer_set_anchor(havel_layer_surface_t* layer, uint32_t anchor) {
    if (!layer || !layer->layer_surface) return;
    layer->layer_surface->current.anchor = anchor;
}

void havel_layer_set_margin(havel_layer_surface_t* layer, int32_t top, int32_t right, int32_t bottom, int32_t left) {
    if (!layer || !layer->layer_surface) return;
    layer->layer_surface->current.margin.top = top;
    layer->layer_surface->current.margin.right = right;
    layer->layer_surface->current.margin.bottom = bottom;
    layer->layer_surface->current.margin.left = left;
}

void havel_layer_set_keyboard_interactivity(havel_layer_surface_t* layer, bool interactive) {
    if (!layer || !layer->layer_surface) return;
    layer->layer_surface->current.keyboard_interactive = interactive ? 
        ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_EXCLUSIVE :
        ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_NONE;
}

// ============================================================================
// Damage Tracking
// ============================================================================

bool havel_output_get_damage(havel_output_t* output, havel_damage_info_t* damage) {
    if (!output || !damage) return false;
    // wlroots 0.20 handles damage internally
    // This would require accessing wlr_output.pending.damage
    damage->rects = NULL;
    damage->count = 0;
    damage->age = 0;
    return true;
}

void havel_output_damage_done(havel_output_t* output) {
    (void)output;
}

void havel_output_damage_add(havel_output_t* output, int32_t x, int32_t y, int32_t width, int32_t height) {
    if (!output || !output->output) return;
    // wlroots 0.20: damage tracking changed
    // Damage is now handled through wlr_output_state
    (void)x; (void)y; (void)width; (void)height;
}

void havel_output_damage_add_whole(havel_output_t* output) {
    if (!output || !output->output) return;
    // wlroots 0.20: damage tracking changed
    (void)output;
}

// ============================================================================
// Clipboard and Data Device
// ============================================================================

bool havel_backend_get_selection(havel_backend_t* backend, havel_selection_type_t type, havel_selection_data_t* data) {
    (void)backend; (void)type; (void)data;
    return false;  // Requires data device integration
}

bool havel_backend_set_selection(havel_backend_t* backend, havel_selection_type_t type, const char* mime_type, const void* data, size_t size) {
    (void)backend; (void)type; (void)mime_type; (void)data; (void)size;
    return false;
}

void havel_backend_clear_selection(havel_backend_t* backend, havel_selection_type_t type) {
    (void)backend; (void)type;
}

// ============================================================================
// Idle and Power Management
// ============================================================================

uint32_t havel_backend_get_idle_time(havel_backend_t* backend) {
    // Would require idle inhibitor protocol integration
    (void)backend;
    return 0;
}

void havel_backend_reset_idle_timer(havel_backend_t* backend) {
    (void)backend;
}

bool havel_output_set_power_mode(havel_output_t* output, uint32_t mode) {
    if (!output || !output->output) return false;
    
    struct wlr_output_state state;
    wlr_output_state_init(&state);
    wlr_output_state_set_enabled(&state, mode != 0);
    bool result = wlr_output_commit_state(output->output, &state);
    wlr_output_state_finish(&state);
    return result;
}

uint32_t havel_output_get_power_mode(havel_output_t* output) {
    if (!output || !output->output) return 0;
    return output->output->enabled ? 1 : 0;
}

// ============================================================================
// Screen Recording (Screencopy)
// ============================================================================

bool havel_screencopy_output(havel_backend_t* backend, havel_output_t* output, havel_screencopy_buffer_t* buffer) {
    if (!backend || !output || !buffer) return false;
    // Requires wlr_screencopy_v1 integration
    (void)buffer;
    return false;
}

bool havel_screencopy_region(havel_backend_t* backend, havel_output_t* output, 
                              int32_t x, int32_t y, int32_t width, int32_t height,
                              havel_screencopy_buffer_t* buffer) {
    if (!backend || !output || !buffer) return false;
    (void)x; (void)y; (void)width; (void)height; (void)buffer;
    return false;
}

// ============================================================================
// Texture Import
// ============================================================================

uint32_t havel_texture_import(havel_backend_t* backend, const havel_texture_import_info_t* info) {
    if (!backend || !info) return 0;
    // Requires renderer integration
    (void)info;
    return 0;
}

void havel_texture_destroy(havel_backend_t* backend, uint32_t texture_id) {
    (void)backend; (void)texture_id;
}

bool havel_texture_bind(havel_backend_t* backend, uint32_t texture_id) {
    (void)backend; (void)texture_id;
    return false;
}

// ============================================================================
// Foreign Toplevel Management
// ============================================================================

size_t havel_backend_get_foreign_toplevel_count(havel_backend_t* backend) {
    // Requires foreign_toplevel_management_v1 protocol
    (void)backend;
    return 0;
}

bool havel_backend_get_foreign_toplevel_info(havel_backend_t* backend, size_t index, havel_foreign_toplevel_info_t* info) {
    (void)backend; (void)index; (void)info;
    return false;
}

void havel_foreign_toplevel_activate(havel_backend_t* backend, size_t index) {
    (void)backend; (void)index;
}

void havel_foreign_toplevel_close(havel_backend_t* backend, size_t index) {
    (void)backend; (void)index;
}

void havel_foreign_toplevel_set_maximized(havel_backend_t* backend, size_t index, bool maximized) {
    (void)backend; (void)index; (void)maximized;
}

void havel_foreign_toplevel_set_minimized(havel_backend_t* backend, size_t index, bool minimized) {
    (void)backend; (void)index; (void)minimized;
}

// ============================================================================
// Virtual Input
// ============================================================================

havel_keyboard_t* havel_keyboard_create_virtual(havel_backend_t* backend) {
    (void)backend;
    return NULL;  // Requires virtual keyboard protocol
}

havel_pointer_t* havel_pointer_create_virtual(havel_backend_t* backend) {
    (void)backend;
    return NULL;  // Requires virtual pointer protocol
}

void havel_keyboard_destroy_virtual(havel_keyboard_t* keyboard) {
    (void)keyboard;
}

void havel_pointer_destroy_virtual(havel_pointer_t* pointer) {
    (void)pointer;
}

void havel_keyboard_virtual_key(havel_keyboard_t* keyboard, uint32_t keycode, bool pressed) {
    if (!keyboard) return;
    // Would use wlr_virtual_keyboard_v1
    (void)keycode; (void)pressed;
}

void havel_keyboard_virtual_modifiers(havel_keyboard_t* keyboard, uint32_t modifiers) {
    (void)keyboard; (void)modifiers;
}

void havel_pointer_virtual_motion(havel_pointer_t* pointer, double dx, double dy) {
    if (!pointer) return;
    wlr_cursor_move(pointer->server->cursor, &pointer->pointer->base, dx, dy);
}

void havel_pointer_virtual_motion_absolute(havel_pointer_t* pointer, double x, double y) {
    if (!pointer) return;
    wlr_cursor_warp_absolute(pointer->server->cursor, &pointer->pointer->base, x, y);
}

void havel_pointer_virtual_button(havel_pointer_t* pointer, uint32_t button, bool pressed) {
    if (!pointer) return;
    wlr_seat_pointer_notify_button(pointer->server->seat, 0, button, pressed);
}

void havel_pointer_virtual_axis(havel_pointer_t* pointer, double dx, double dy) {
    if (!pointer) return;
    wlr_seat_pointer_notify_axis(pointer->server->seat, 0, WL_POINTER_AXIS_VERTICAL_SCROLL, dx, dx, WL_POINTER_AXIS_SOURCE_WHEEL, WL_POINTER_AXIS_RELATIVE_DIRECTION_IDENTICAL);
    (void)dy;
}

// ============================================================================
// Renderer Access
// ============================================================================

void* havel_backend_get_renderer(havel_backend_t* backend) {
    if (!backend) return NULL;
    return ((havel_wlr_server_t*)backend)->renderer;
}

void* havel_backend_get_allocator(havel_backend_t* backend) {
    if (!backend) return NULL;
    return ((havel_wlr_server_t*)backend)->allocator;
}

bool havel_backend_renderer_begin(havel_backend_t* backend, uint32_t width, uint32_t height) {
    (void)backend; (void)width; (void)height;
    return false;  // wlroots handles rendering
}

void havel_backend_renderer_end(havel_backend_t* backend) {
    (void)backend;
}

void havel_backend_renderer_clear(havel_backend_t* backend, float r, float g, float b, float a) {
    (void)backend; (void)r; (void)g; (void)b; (void)a;
}

// ============================================================================
// Session and System Integration
// ============================================================================

bool havel_backend_is_session_active(havel_backend_t* backend) {
    if (!backend) return false;
    // wlroots 0.20: session active state accessed differently
    // For now, assume active if backend exists
    return true;
}

bool havel_backend_change_vt(havel_backend_t* backend, unsigned int vt) {
    if (!backend) return false;
    // wlroots 0.20: VT switching via session
    // This requires proper session handling
    (void)vt;
    return false;
}

const char* havel_backend_get_seat_name(havel_backend_t* backend) {
    if (!backend) return "";
    havel_wlr_server_t* server = (havel_wlr_server_t*)backend;
    return server->seat ? server->seat->name : "";
}

bool havel_backend_has_capability(havel_backend_t* backend, const char* capability) {
    if (!backend || !capability) return false;
    // Check seat capabilities
    havel_wlr_server_t* server = (havel_wlr_server_t*)backend;
    uint32_t caps = server->seat->capabilities;
    
    if (strcmp(capability, "keyboard") == 0) {
        return (caps & WL_SEAT_CAPABILITY_KEYBOARD) != 0;
    } else if (strcmp(capability, "pointer") == 0) {
        return (caps & WL_SEAT_CAPABILITY_POINTER) != 0;
    } else if (strcmp(capability, "touch") == 0) {
        return (caps & WL_SEAT_CAPABILITY_TOUCH) != 0;
    }
    return false;
}

// ============================================================================
// Debug and Statistics
// ============================================================================

bool havel_backend_get_stats(havel_backend_t* backend, havel_backend_stats_t* stats) {
    if (!backend || !stats) return false;
    
    havel_wlr_server_t* server = (havel_wlr_server_t*)backend;
    
    stats->output_count = havel_backend_get_output_count(backend);
    stats->keyboard_count = havel_backend_get_keyboard_count(backend);
    stats->pointer_count = havel_backend_get_pointer_count(backend);
    stats->view_count = 0;  // Tracked in C++ layer
    stats->layer_count = havel_backend_get_layer_count(backend);
    stats->frame_count = 0;  // Would need frame counter
    stats->uptime_ms = 0;  // Would need startup timestamp
    stats->fps = 0.0f;  // Would need FPS calculation
    stats->gpu_memory_used = 0;  // Would need GPU memory tracking
    
    (void)server;
    return true;
}

void havel_backend_print_debug_info(havel_backend_t* backend) {
    if (!backend) return;
    
    havel_wlr_server_t* server = (havel_wlr_server_t*)backend;
    
    LOG_INFO("[Backend Debug]");
    LOG_INFO("  Outputs: %zu", havel_backend_get_output_count(backend));
    LOG_INFO("  Keyboards: %zu", havel_backend_get_keyboard_count(backend));
    LOG_INFO("  Pointers: %zu", havel_backend_get_pointer_count(backend));
    LOG_INFO("  Active Workspace: %u", server->active_workspace);
    LOG_INFO("  Session Active: %s", havel_backend_is_session_active(backend) ? "yes" : "no");
    LOG_INFO("  Seat: %s", havel_backend_get_seat_name(backend));
}
