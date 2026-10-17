// Loading Screen Implementation

#include "LoadingScreen.h"
#include <wm/render_c.h>
#include <Logger.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_output.h>
#include <wayland-util.h>
#include <wayland-server-core.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/random.h>
#include <unistd.h>

// Loading screen state
static struct {
    struct wlr_scene_tree* root;
    struct wlr_scene_rect* background;
    struct wlr_scene_rect* logo_box;
    struct wlr_scene_rect* progress_bar_bg;
    struct wlr_scene_rect* progress_bar_fg;
    struct wlr_scene_rect* status_box;
    struct wlr_scene_rect* tip_box;
    struct wlr_scene_buffer* status_text_node;
    struct wlr_scene_buffer* tip_text_node;
    int screen_w;
    int screen_h;
    
    bool visible;
    int progress;
    char status_text[256];
    char tip_text[512];
    
    struct LoadingScreenConfig config;
    
    // Timer
    struct wl_event_source* timer_source;
    struct wl_event_loop* event_loop;
    struct wlr_scene* scene;
} g_loading_screen = {0};

// Tips to show during loading
static const char* loading_tips[] = {
    "Tip: Press Meta+D to open the app launcher",
    "Tip: Use Meta+Tab to switch between workspaces",
    "Tip: Meta+W shows the workspace overview",
    "Tip: Right-click on desktop for context menu",
    "Tip: Drag files to the taskbar to pin apps",
    "Tip: Use Meta+Enter to open a terminal",
    "Tip: Meta+Shift+Q closes the focused window",
    "Tip: Ctrl+Meta+F4 quits the compositor",
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
    static uint32_t seed = 0;
    
    if (!seeded) {
        // Use getrandom for better entropy, fallback to time + pid
        if (getrandom(&seed, sizeof(seed), GRND_NONBLOCK) != sizeof(seed)) {
            seed = (uint32_t)(time(NULL) ^ (getpid() << 16));
        }
        seeded = true;
    }
    
    // Simple LCG for random number generation
    seed = seed * 1664525u + 1013904223u;
    
    const int tip_count = (int)(sizeof(loading_tips) / sizeof(loading_tips[0]));
    return (int)(seed % (uint32_t)tip_count);
}

void loading_screen_init(struct wlr_scene_tree* parent, struct wl_event_loop* loop) {
    if (!parent) {
        LOG_ERROR("[LoadingScreen] No parent scene tree provided");
        return;
    }

    g_loading_screen.event_loop = loop;
    if (!loop) {
        LOG_WARN("[LoadingScreen] No event loop - auto-hide timer disabled");
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
    g_loading_screen.config.timeout_ms = 5000;  // Auto-hide after 5 seconds
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

// Center the splash on the real output size. At creation time no output
// exists yet, so the original hardcoded 1920x1080 background and the
// boxes all parked at (0,0) - the status text had nowhere sensible to
// be and the whole splash sat crammed in the top-left corner.
void loading_screen_refresh_text(void);

void loading_screen_layout(int w, int h) {
    if (w <= 0 || h <= 0) return;
    g_loading_screen.screen_w = w;
    g_loading_screen.screen_h = h;

    if (g_loading_screen.background) {
        wlr_scene_rect_set_size(g_loading_screen.background, w, h);
    }
    if (g_loading_screen.logo_box) {
        wlr_scene_node_set_position(&g_loading_screen.logo_box->node,
                                    (w - 300) / 2, (h - 150) / 2 - 60);
    }
    if (g_loading_screen.progress_bar_bg) {
        wlr_scene_node_set_position(&g_loading_screen.progress_bar_bg->node,
                                    (w - 400) / 2, (h - 150) / 2 + 110);
    }
    // status text sits under the progress bar; the fg bar shares the
    // container so it follows automatically
    loading_screen_refresh_text();
}

// (Re)draw the status and tip text. The status was previously only
// stored and logged - nothing ever rendered it, so the splash looked
// frozen ("loading screen not changing"). Text renders through the same
// font pipeline as every other overlay; if no font is up yet it is
// skipped and retried on the next status.
void loading_screen_refresh_text(void) {
    if (!g_loading_screen.root || !g_loading_screen.visible) return;
    int w = g_loading_screen.screen_w > 0 ? g_loading_screen.screen_w : 1280;
    int h = g_loading_screen.screen_h > 0 ? g_loading_screen.screen_h : 800;

    if (g_loading_screen.status_text_node) {
        wlr_scene_node_destroy(&g_loading_screen.status_text_node->node);
        g_loading_screen.status_text_node = NULL;
    }
    if (g_loading_screen.tip_text_node) {
        wlr_scene_node_destroy(&g_loading_screen.tip_text_node->node);
        g_loading_screen.tip_text_node = NULL;
    }

    if (g_loading_screen.status_text[0]) {
        g_loading_screen.status_text_node = havel_scene_text(
            g_loading_screen.root, g_loading_screen.status_text,
            (w - 400) / 2, (h - 150) / 2 + 145, 18,
            (float[4]){0.92f, 0.92f, 0.92f, 1.0f});
    }
    if (g_loading_screen.tip_text[0]) {
        g_loading_screen.tip_text_node = havel_scene_text(
            g_loading_screen.root, g_loading_screen.tip_text,
            40, h - 60, 14,
            (float[4]){0.55f, 0.6f, 0.7f, 1.0f});
    }
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

    loading_screen_refresh_text();
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

static int loading_screen_timer_cb(void* data) {
    (void)data;
    if (g_loading_screen.visible) {
        LOG_INFO("[LoadingScreen] Auto-hide timeout reached");
        loading_screen_hide();
    }
    // One-shot: source removed in loading_screen_hide/stop_timer
    return 0;
}

void loading_screen_start_timer(void) {
    if (g_loading_screen.config.timeout_ms <= 0) {
        return;
    }
    
    if (!g_loading_screen.event_loop) {
        LOG_WARN("[LoadingScreen] No event loop, cannot start auto-hide timer");
        return;
    }
    
    if (g_loading_screen.timer_source) {
        loading_screen_stop_timer();
    }
    
    g_loading_screen.timer_source = wl_event_loop_add_timer(
        g_loading_screen.event_loop, loading_screen_timer_cb, NULL);
    if (!g_loading_screen.timer_source) {
        LOG_ERROR("[LoadingScreen] Failed to create auto-hide timer");
        return;
    }
    wl_event_source_timer_update(g_loading_screen.timer_source,
                                 g_loading_screen.config.timeout_ms);
    
    LOG_DEBUG("[LoadingScreen] Timer started for %dms", 
              g_loading_screen.config.timeout_ms);
}

void loading_screen_stop_timer(void) {
    if (g_loading_screen.timer_source) {
        wl_event_source_remove(g_loading_screen.timer_source);
        g_loading_screen.timer_source = NULL;
    }
}

void loading_screen_destroy(void) {
    loading_screen_hide();
    
    if (g_loading_screen.root) {
        // Scene tree will be destroyed with parent
        g_loading_screen.root = NULL;
    }
    
    LOG_INFO("[LoadingScreen] Destroyed");
}
