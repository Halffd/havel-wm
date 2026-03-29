// Output Manager - Manages display outputs
// Pure C implementation for performance

#include "OutputManager.h"
#include "../BackendTypes.h"
#include <Logger.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>

// Forward declaration
struct havel_wlr_server;

// ============================================================================
// Output Frame Handler
// ============================================================================

static void output_frame(struct wl_listener *listener, void *data) {
    havel_output_t *output = wl_container_of(listener, output, frame);
    struct havel_wlr_server *server = output->server;
    
    // Commit scene output - wlroots handles all rendering
    const struct wlr_scene_output_state_options options = {
        .timer = NULL,
    };
    
    if (!output->scene_output) {
        LOG_ERROR("[OUTPUT] %s: scene_output is NULL!", output->output->name);
        return;
    }
    
    wlr_scene_output_commit(output->scene_output, &options);
}

static void output_destroy(struct wl_listener *listener, void *data) {
    havel_output_t *output = wl_container_of(listener, output, destroy);
    
    LOG_INFO("[OUTPUT] %s destroyed", output->output->name);
    
    // Free gamma LUT buffers
    if (output->gamma_ramp_red) free(output->gamma_ramp_red);
    if (output->gamma_ramp_green) free(output->gamma_ramp_green);
    if (output->gamma_ramp_blue) free(output->gamma_ramp_blue);
    
    // Remove listeners
    wl_list_remove(&output->frame.link);
    wl_list_remove(&output->destroy.link);
    wl_list_remove(&output->link);
    
    free(output);
}

// ============================================================================
// Output Creation
// ============================================================================

havel_output_t* havel_output_create(struct havel_wlr_server *server, struct wlr_output *wlr_output) {
    LOG_INFO("[OUTPUT] New output: %s", wlr_output->name);
    
    // Initialize output renderer
    wlr_output_init_render(wlr_output, server->allocator, server->renderer);
    
    // Enable output with preferred mode
    if (!wl_list_empty(&wlr_output->modes)) {
        struct wlr_output_mode *mode = wlr_output_preferred_mode(wlr_output);
        struct wlr_output_state state;
        wlr_output_state_init(&state);
        wlr_output_state_set_mode(&state, mode);
        wlr_output_state_set_enabled(&state, true);
        
        if (!wlr_output_commit_state(wlr_output, &state)) {
            wlr_output_state_finish(&state);
            return NULL;
        }
        wlr_output_state_finish(&state);
    }
    
    // Allocate output structure
    havel_output_t *output = calloc(1, sizeof(havel_output_t));
    if (!output) {
        LOG_ERROR("[OUTPUT] Failed to allocate output structure");
        return NULL;
    }
    
    output->server = server;
    output->output = wlr_output;
    
    // Create scene output
    output->scene_output = wlr_scene_output_create(server->scene, wlr_output);
    if (!output->scene_output) {
        LOG_ERROR("[OUTPUT] Failed to create scene output for %s", wlr_output->name);
        free(output);
        return NULL;
    }
    
    // Initialize display properties
    output->gamma = 1.0f;
    output->temperature = 6500;
    output->brightness = 1.0f;
    output->zoom = 1.0f;
    output->zoom_center_x = -1.0f;
    output->zoom_center_y = -1.0f;
    output->prev_zoom = 1.0f;
    
    // Allocate gamma LUT
    size_t gamma_size = wlr_output_get_gamma_size(wlr_output);
    if (gamma_size > 0) {
        output->gamma_ramp_size = gamma_size;
        output->gamma_ramp_red = calloc(gamma_size, sizeof(uint16_t));
        output->gamma_ramp_green = calloc(gamma_size, sizeof(uint16_t));
        output->gamma_ramp_blue = calloc(gamma_size, sizeof(uint16_t));
        output->gamma_ramp_dirty = true;
        LOG_INFO("[OUTPUT] Gamma LUT allocated for %s (size=%zu)", wlr_output->name, gamma_size);
    } else {
        output->gamma_ramp_size = 0;
        output->gamma_ramp_red = NULL;
        output->gamma_ramp_green = NULL;
        output->gamma_ramp_blue = NULL;
        output->gamma_ramp_dirty = false;
        LOG_WARN("[OUTPUT] %s does not support gamma control", wlr_output->name);
    }
    
    // Add to output list
    wl_list_insert(server->outputs.prev, &output->link);
    output->is_primary = wl_list_empty(&server->outputs) || (server->outputs.next == &output->link);
    
    LOG_DEBUG("[OUTPUT] %s is %s", wlr_output->name, output->is_primary ? "primary" : "secondary");
    
    // Setup listeners
    output->frame.notify = output_frame;
    wl_signal_add(&wlr_output->events.frame, &output->frame);
    
    output->destroy.notify = output_destroy;
    wl_signal_add(&wlr_output->events.destroy, &output->destroy);
    
    // Position output in layout
    if (wl_list_empty(&server->outputs)) {
        // First monitor - position at (0,0)
        wlr_output_layout_add_auto(server->output_layout, wlr_output);
    } else {
        // Additional monitor - position to the right
        struct havel_output *last_output;
        struct wlr_box last_box;
        
        struct wl_list *last_link = server->outputs.prev;
        last_output = wl_container_of(last_link, last_output, link);
        wlr_output_layout_get_box(server->output_layout, last_output->output, &last_box);
        
        int x = last_box.x + last_box.width;
        wlr_output_layout_add(server->output_layout, wlr_output, x, 0);
        
        LOG_INFO("[OUTPUT] Positioned %s at (%d,0) to the right of %s",
                 wlr_output->name, x, last_output->output->name);
    }
    
    LOG_INFO("[OUTPUT] %s setup complete (enabled=%d)", wlr_output->name, wlr_output->enabled);
    
    return output;
}

void havel_output_destroy(havel_output_t *output) {
    if (!output) return;
    
    // Handler will be called by wlroots
}

// ============================================================================
// Output Manager Initialization
// ============================================================================

void havel_output_init(havel_wlr_server_t *server) {
    wl_list_init(&server->outputs);
    LOG_INFO("[OUTPUT] Output manager initialized");
}
