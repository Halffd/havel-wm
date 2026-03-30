// Backend Server - Main wlroots server initialization
// Pure C implementation for performance

#include "Backend.h"
#include "../BackendTypes.h"
#include "../output/OutputManager.h"
#include "../input/InputHandler.h"
#include "../shell/XdgShell.h"
#include "../shell/LayerShell.h"
#include "../cursor/Cursor.h"
#include <Logger.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>

// Helper: Get monotonic time in milliseconds
static uint64_t get_monotonic_time_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000ULL + (uint64_t)ts.tv_nsec / 1000000ULL;
}

// ============================================================================
// Server Event Handlers
// ============================================================================

static void handle_new_input(struct wl_listener *listener, void *data) {
    havel_wlr_server_t *server = wl_container_of(listener, server, new_input);
    struct wlr_input_device *device = data;
    
    switch (device->type) {
        case WLR_INPUT_DEVICE_KEYBOARD:
            havel_keyboard_create(server, device);
            break;
        case WLR_INPUT_DEVICE_POINTER:
            havel_pointer_create(server, device);
            break;
        default:
            LOG_WARN("[BACKEND] Unknown input device type: %d", device->type);
            break;
    }
    
    // Update capabilities
    uint32_t caps = WL_SEAT_CAPABILITY_POINTER;
    if (!wl_list_empty(&server->keyboards)) {
        caps |= WL_SEAT_CAPABILITY_KEYBOARD;
    }
    wlr_seat_set_capabilities(server->seat, caps);
}

// ============================================================================
// Server Creation
// ============================================================================

havel_wlr_server_t* havel_server_create(struct wl_display *display) {
    LOG_INFO("[BACKEND] Creating wlroots server");
    
    // Allocate server structure
    havel_wlr_server_t *server = calloc(1, sizeof(havel_wlr_server_t));
    if (!server) {
        LOG_ERROR("[BACKEND] Failed to allocate server");
        return NULL;
    }
    
    server->display = display;
    wl_list_init(&server->outputs);
    wl_list_init(&server->keyboards);
    wl_list_init(&server->pointers);
    server->active_workspace = 0;
    
    // Initialize performance metrics
    server->frame_count = 0;
    server->current_fps = 0.0f;
    server->startup_time = get_monotonic_time_ms();
    
    // Create backend (auto-detect) - wlroots 0.20 uses wl_event_loop
    struct wl_event_loop *event_loop = wl_display_get_event_loop(display);
    server->backend = wlr_backend_autocreate(event_loop, &server->session);
    if (!server->backend) {
        LOG_ERROR("[BACKEND] Failed to create backend");
        free(server);
        return NULL;
    }
    
    // Create renderer and allocator
    server->renderer = wlr_renderer_autocreate(server->backend);
    if (!server->renderer) {
        LOG_ERROR("[BACKEND] Failed to create renderer");
        wlr_backend_destroy(server->backend);
        free(server);
        return NULL;
    }
    
    wlr_renderer_init_wl_display(server->renderer, display);
    
    server->allocator = wlr_allocator_autocreate(server->backend, server->renderer);
    if (!server->allocator) {
        LOG_ERROR("[BACKEND] Failed to create allocator");
        wlr_renderer_destroy(server->renderer);
        wlr_backend_destroy(server->backend);
        free(server);
        return NULL;
    }
    
    // Create scene graph
    server->scene = wlr_scene_create();
    if (!server->scene) {
        LOG_ERROR("[BACKEND] Failed to create scene");
        wlr_allocator_destroy(server->allocator);
        wlr_renderer_destroy(server->renderer);
        wlr_backend_destroy(server->backend);
        free(server);
        return NULL;
    }
    
    // Create output layout
    server->output_layout = wlr_output_layout_create(display);
    if (!server->output_layout) {
        LOG_ERROR("[BACKEND] Failed to create output layout");
        // Note: scene is auto-freed in wlroots 0.20
        wlr_allocator_destroy(server->allocator);
        wlr_renderer_destroy(server->renderer);
        wlr_backend_destroy(server->backend);
        free(server);
        return NULL;
    }
    
    LOG_INFO("[BACKEND] Server created successfully");
    
    return server;
}

// ============================================================================
// Server Initialization
// ============================================================================

void havel_server_start(havel_wlr_server_t *server) {
    LOG_INFO("[BACKEND] Starting server components");
    
    // Initialize subsystems
    havel_output_init(server);
    havel_input_init(server);
    havel_cursor_init(server);
    havel_seat_init(server);
    
    // Create workspace trees
    for (uint32_t i = 0; i < 10; i++) {
        server->workspaces[i] = wlr_scene_tree_create(&server->scene->tree);
        wlr_scene_node_set_enabled(&server->workspaces[i]->node, (i == server->active_workspace));
        LOG_INFO("[WORKSPACE] Workspace %u created", i);
    }
    
    // Create overlay layer
    server->overlay_layer = wlr_scene_tree_create(&server->scene->tree);
    wlr_scene_node_raise_to_top(&server->overlay_layer->node);
    wlr_scene_node_set_enabled(&server->overlay_layer->node, true);
    LOG_INFO("[OVERLAY] Overlay layer created");
    
    // Initialize shells
    havel_xdg_shell_init(server);
    havel_layer_shell_init(server);
    
    // Setup input handler
    server->new_input.notify = handle_new_input;
    wl_signal_add(&server->backend->events.new_input, &server->new_input);
    
    LOG_INFO("[BACKEND] Server started");
}

// ============================================================================
// Server Destruction
// ============================================================================

void havel_server_destroy(havel_wlr_server_t *server) {
    if (!server) return;
    
    LOG_INFO("[BACKEND] Destroying server");
    
    // Destroy subsystems
    havel_cursor_destroy(server);
    havel_seat_destroy(server);
    
    // Destroy scene (destroys all nodes including workspaces and overlay)
    // Note: wlr_scene_destroy may not exist in wlroots 0.20, scene is auto-freed
    
    // Destroy shells - these are auto-destroyed with display in wlroots 0.20
    
    // Destroy output layout
    if (server->output_layout) {
        wlr_output_layout_destroy(server->output_layout);
    }
    
    // Destroy allocator and renderer
    if (server->allocator) {
        wlr_allocator_destroy(server->allocator);
    }
    
    if (server->renderer) {
        wlr_renderer_destroy(server->renderer);
    }
    
    // Destroy backend
    if (server->backend) {
        wlr_backend_destroy(server->backend);
    }
    
    free(server);
    
    LOG_INFO("[BACKEND] Server destroyed");
}
