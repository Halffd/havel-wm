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
