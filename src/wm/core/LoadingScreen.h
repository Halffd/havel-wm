// Loading Screen - Shows during compositor startup

#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Forward declare wlroots types (opaque pointers)
struct wlr_scene_tree;

// Loading screen configuration
struct LoadingScreenConfig {
    bool enabled;
    int timeout_ms;  // Auto-hide after this time (0 = manual)
    bool show_progress;
    bool show_logo;
    bool show_tips;
};

// Initialize loading screen
void loading_screen_init(struct wlr_scene_tree* parent);

// Show loading screen
void loading_screen_show(void);

// Hide loading screen
void loading_screen_hide(void);

// Update progress (0-100)
void loading_screen_set_progress(int percent);

// Set status text
void loading_screen_set_status(const char* text);

// Set tip text
void loading_screen_set_tip(const char* text);

// Check if loading screen is visible
bool loading_screen_is_visible(void);

// Get/set configuration
struct LoadingScreenConfig* loading_screen_get_config(void);
void loading_screen_set_config(struct LoadingScreenConfig* config);

// Auto-hide timer
void loading_screen_start_timer(void);
void loading_screen_stop_timer(void);

// Render/update (called per-frame)
void loading_screen_update(void);

// Cleanup
void loading_screen_destroy(void);

#ifdef __cplusplus
}
#endif
