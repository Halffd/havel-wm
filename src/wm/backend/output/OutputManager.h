// Output Manager - Manages display outputs
// Pure C implementation for performance

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <wayland-server-core.h>
#include <wlr/types/wlr_output.h>

#ifdef __cplusplus
extern "C" {
#endif

// Forward declarations
struct havel_wlr_server;
struct havel_output;

// Output handle
typedef struct havel_output {
    struct havel_wlr_server *server;
    struct wlr_output *output;
    struct wlr_scene_output *scene_output;
    
    struct wl_listener frame;
    struct wl_listener destroy;
    
    struct wl_list link;  // havel_wlr_server.outputs
    
    bool is_primary;
    
    // Display properties
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

// Initialize output manager
void havel_output_init(struct havel_wlr_server *server);

// Create output from wlr_output
havel_output_t* havel_output_create(struct havel_wlr_server *server, struct wlr_output *wlr_output);

// Destroy output
void havel_output_destroy(havel_output_t *output);

#ifdef __cplusplus
}
#endif
