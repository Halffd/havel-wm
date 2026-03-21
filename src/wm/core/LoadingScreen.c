// Loading Screen Implementation

#include "LoadingScreen.h"
#include <Logger.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_output.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

// Loading screen state
static struct {
    struct wlr_scene_tree* root;
    struct wlr_scene_rect* background;
    struct wlr_scene_rect* logo_box;
    struct wlr_scene_rect* progress_bar_bg;
    struct wlr_scene_rect* progress_bar_fg;
    struct wlr_scene_rect* status_box;
    struct wlr_scene_rect* tip_box;
    
    bool visible;
    int progress;
    char status_text[256];
    char tip_text[512];
    
    struct LoadingScreenConfig config;
    
    // Timer
    struct wl_event_source* timer_source;
    struct wlr_scene* scene;
} g_loading_screen = {0};

// Tips to show during loading
static const char* loading_tips[] = {
    "Tip: Press Meta+Space to open the app launcher",
    "Tip: Use Meta+Tab to switch between windows",
    "Tip: Meta+W shows the workspace overview",
    "Tip: Right-click on desktop for context menu",
    "Tip: Drag files to the taskbar to pin apps",
    "Tip: Use Meta+Enter to open a terminal",
    "Tip: Meta+Q closes the focused window",
    "Tip: Meta+Shift+Q quits the compositor",
    "Tip: Use Alt+Tab for classic window switching",
    "Tip: Meta+1-9 switches to workspace 1-9",
    "Tip: Drag window edges to resize",
    "Tip: Meta+drag to move windows",
    "Tip: Meta+Right-click to resize windows",
    "Tip: Use the overview to organize workspaces",
    "Tip: Hot corners can be configured in settings"
};

static int get_random_tip_index(void) {
    static bool seeded = false;
    if (!seeded) {
        srand(time(NULL));
        seeded = true;
    }
    return rand() % (sizeof(loading_tips) / sizeof(loading_tips[0]));
}

void loading_screen_init(struct wlr_scene_tree* parent) {
    if (!parent) {
        LOG_ERROR("[LoadingScreen] No parent scene tree provided");
        return;
    }
    
    LOG_INFO("[LoadingScreen] Initializing...");
    
    // Create root tree for loading screen (raised to top)
    g_loading_screen.root = wlr_scene_tree_create(parent);
    wlr_scene_node_raise_to_top(&g_loading_screen.root->node);
    
    // Background (full screen, semi-transparent dark)
    g_loading_screen.background = wlr_scene_rect_create(
        g_loading_screen.root, 1920, 1080, 
        (float[4]){0.05f, 0.05f, 0.1f, 0.95f});
    
    // Logo box (centered)
    g_loading_screen.logo_box = wlr_scene_rect_create(
        g_loading_screen.root, 300, 150,
        (float[4]){0.3f, 0.3f, 0.4f, 1.0f});
    
    // Progress container (tree to hold progress bars)
    struct wlr_scene_tree* progress_container = wlr_scene_tree_create(g_loading_screen.root);
    
    // Progress bar background
    g_loading_screen.progress_bar_bg = wlr_scene_rect_create(
        progress_container, 400, 20,
        (float[4]){0.2f, 0.2f, 0.3f, 1.0f});
    
    // Progress bar foreground
    g_loading_screen.progress_bar_fg = wlr_scene_rect_create(
        progress_container, 0, 20,
        (float[4]){0.4f, 0.6f, 0.9f, 1.0f});
    wlr_scene_node_set_position(&g_loading_screen.progress_bar_fg->node, 0, 0);
    
    // Status box
    g_loading_screen.status_box = wlr_scene_rect_create(
        g_loading_screen.root, 400, 40,
        (float[4]){0.0f, 0.0f, 0.0f, 0.0f});  // Invisible, just for text
    
    // Tip box
    g_loading_screen.tip_box = wlr_scene_rect_create(
        g_loading_screen.root, 500, 30,
        (float[4]){0.0f, 0.0f, 0.0f, 0.0f});  // Invisible
    
    // Default config
    g_loading_screen.config.enabled = true;
    g_loading_screen.config.timeout_ms = 0;  // No auto-hide
    g_loading_screen.config.show_progress = true;
    g_loading_screen.config.show_logo = true;
    g_loading_screen.config.show_tips = true;
    
    g_loading_screen.visible = false;
    g_loading_screen.progress = 0;
    g_loading_screen.status_text[0] = '\0';
    g_loading_screen.tip_text[0] = '\0';
    g_loading_screen.timer_source = NULL;
    
    // Hide initially
    wlr_scene_node_set_enabled(&g_loading_screen.root->node, false);
    
    LOG_INFO("[LoadingScreen] Initialized");
}

void loading_screen_show(void) {
    if (!g_loading_screen.root) {
        LOG_ERROR("[LoadingScreen] Not initialized");
        return;
    }
    
    if (g_loading_screen.visible) {
        return;  // Already visible
    }
    
    LOG_INFO("[LoadingScreen] Showing...");
    
    // Set initial tip
    if (g_loading_screen.config.show_tips) {
        loading_screen_set_tip(loading_tips[get_random_tip_index()]);
    }
    
    // Show
    wlr_scene_node_set_enabled(&g_loading_screen.root->node, true);
    g_loading_screen.visible = true;
    
    // Start timer if configured
    if (g_loading_screen.config.timeout_ms > 0) {
        loading_screen_start_timer();
    }
}

void loading_screen_hide(void) {
    if (!g_loading_screen.visible) {
        return;
    }
    
    LOG_INFO("[LoadingScreen] Hiding");
    
    loading_screen_stop_timer();
    wlr_scene_node_set_enabled(&g_loading_screen.root->node, false);
    g_loading_screen.visible = false;
}

void loading_screen_set_progress(int percent) {
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;
    
    g_loading_screen.progress = percent;
    
    if (g_loading_screen.progress_bar_fg && g_loading_screen.config.show_progress) {
        int width = (int)(400 * (percent / 100.0));
        wlr_scene_rect_set_size(g_loading_screen.progress_bar_fg, width, 20);
    }
}

void loading_screen_set_status(const char* text) {
    if (!text) return;
    
    strncpy(g_loading_screen.status_text, text, sizeof(g_loading_screen.status_text) - 1);
    g_loading_screen.status_text[sizeof(g_loading_screen.status_text) - 1] = '\0';
    
    LOG_INFO("[LoadingScreen] Status: %s", text);
}

void loading_screen_set_tip(const char* text) {
    if (!text) return;
    
    strncpy(g_loading_screen.tip_text, text, sizeof(g_loading_screen.tip_text) - 1);
    g_loading_screen.tip_text[sizeof(g_loading_screen.tip_text) - 1] = '\0';
}

bool loading_screen_is_visible(void) {
    return g_loading_screen.visible;
}

struct LoadingScreenConfig* loading_screen_get_config(void) {
    return &g_loading_screen.config;
}

void loading_screen_set_config(struct LoadingScreenConfig* config) {
    if (!config) return;
    memcpy(&g_loading_screen.config, config, sizeof(struct LoadingScreenConfig));
}

void loading_screen_start_timer(void) {
    if (g_loading_screen.config.timeout_ms <= 0) {
        return;
    }
    
    if (g_loading_screen.timer_source) {
        loading_screen_stop_timer();
    }
    
    // Note: In real implementation, would use wl_event_loop_add_timer
    // For now, this is a placeholder
    LOG_DEBUG("[LoadingScreen] Timer started for %dms", 
              g_loading_screen.config.timeout_ms);
}

void loading_screen_stop_timer(void) {
    if (g_loading_screen.timer_source) {
        // wl_event_source_remove(g_loading_screen.timer_source);
        g_loading_screen.timer_source = NULL;
    }
}

void loading_screen_update(void) {
    // This would be called per-frame to update any animations
    // For now, it's a placeholder
}

void loading_screen_destroy(void) {
    loading_screen_hide();
    
    if (g_loading_screen.root) {
        // Scene tree will be destroyed with parent
        g_loading_screen.root = NULL;
    }
    
    LOG_INFO("[LoadingScreen] Destroyed");
}
