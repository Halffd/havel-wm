// Vulkan Overlay Renderer Implementation

#include "VulkanOverlayRenderer.h"
#include <Logger.h>
#include <stdlib.h>
#include <string.h>

#define MAX_OVERLAYS 32

// Internal overlay structure
struct VulkanOverlayInternal {
    VulkanOverlay overlay;
    bool active;
    int id;
};

// Internal renderer structure
struct VulkanOverlayRenderer {
    VulkanRenderer* renderer;
    struct VulkanOverlayInternal overlays[MAX_OVERLAYS];
    int next_id;
    int overlay_count;
};

// ============================================================================
// Overlay Renderer
// ============================================================================

VulkanOverlayRenderer* vulkan_overlay_renderer_create(VulkanRenderer* renderer) {
    if (!renderer) {
        LOG_ERROR("[VulkanOverlay] Invalid renderer");
        return NULL;
    }
    
    struct VulkanOverlayRenderer* overlay_renderer = 
        (struct VulkanOverlayRenderer*)calloc(1, sizeof(struct VulkanOverlayRenderer));
    if (!overlay_renderer) {
        LOG_ERROR("[VulkanOverlay] Failed to allocate");
        return NULL;
    }
    
    overlay_renderer->renderer = renderer;
    overlay_renderer->next_id = 1;
    overlay_renderer->overlay_count = 0;
    
    memset(overlay_renderer->overlays, 0, sizeof(overlay_renderer->overlays));
    
    LOG_INFO("[VulkanOverlay] Renderer created");
    return overlay_renderer;
}

void vulkan_overlay_renderer_destroy(VulkanOverlayRenderer* renderer) {
    if (!renderer) return;
    
    // Destroy all overlays
    for (int i = 0; i < MAX_OVERLAYS; i++) {
        if (renderer->overlays[i].active) {
            free((char*)renderer->overlays[i].overlay.text);
        }
    }
    
    free(renderer);
    LOG_INFO("[VulkanOverlay] Renderer destroyed");
}

int vulkan_overlay_create(
    VulkanOverlayRenderer* renderer,
    VulkanOverlayType type,
    int x, int y,
    int width, int height) {
    
    if (!renderer) return -1;
    
    // Find free slot
    int slot = -1;
    for (int i = 0; i < MAX_OVERLAYS; i++) {
        if (!renderer->overlays[i].active) {
            slot = i;
            break;
        }
    }
    
    if (slot < 0) {
        LOG_ERROR("[VulkanOverlay] Max overlays reached");
        return -1;
    }
    
    struct VulkanOverlayInternal* overlay = &renderer->overlays[slot];
    overlay->id = renderer->next_id++;
    if (renderer->next_id > 10000) renderer->next_id = 1;
    
    overlay->overlay.type = type;
    overlay->overlay.x = x;
    overlay->overlay.y = y;
    overlay->overlay.width = width;
    overlay->overlay.height = height;
    overlay->overlay.alpha = 1.0f;
    overlay->overlay.visible = true;
    overlay->overlay.text = NULL;
    overlay->overlay.user_data = NULL;
    overlay->active = true;
    
    renderer->overlay_count++;
    
    LOG_DEBUG("[VulkanOverlay] Created overlay %d (type=%d)", overlay->id, type);
    return overlay->id;
}

void vulkan_overlay_destroy(VulkanOverlayRenderer* renderer, int overlay_id) {
    if (!renderer) return;
    
    for (int i = 0; i < MAX_OVERLAYS; i++) {
        if (renderer->overlays[i].active && renderer->overlays[i].id == overlay_id) {
            free((char*)renderer->overlays[i].overlay.text);
            memset(&renderer->overlays[i], 0, sizeof(renderer->overlays[i]));
            renderer->overlay_count--;
            LOG_DEBUG("[VulkanOverlay] Destroyed overlay %d", overlay_id);
            return;
        }
    }
}

void vulkan_overlay_set_visible(VulkanOverlayRenderer* renderer, int overlay_id, bool visible) {
    if (!renderer) return;
    
    for (int i = 0; i < MAX_OVERLAYS; i++) {
        if (renderer->overlays[i].active && renderer->overlays[i].id == overlay_id) {
            renderer->overlays[i].overlay.visible = visible;
            return;
        }
    }
}

void vulkan_overlay_set_position(VulkanOverlayRenderer* renderer, int overlay_id, int x, int y) {
    if (!renderer) return;
    
    for (int i = 0; i < MAX_OVERLAYS; i++) {
        if (renderer->overlays[i].active && renderer->overlays[i].id == overlay_id) {
            renderer->overlays[i].overlay.x = x;
            renderer->overlays[i].overlay.y = y;
            return;
        }
    }
}

void vulkan_overlay_set_size(VulkanOverlayRenderer* renderer, int overlay_id, int w, int h) {
    if (!renderer) return;
    
    for (int i = 0; i < MAX_OVERLAYS; i++) {
        if (renderer->overlays[i].active && renderer->overlays[i].id == overlay_id) {
            renderer->overlays[i].overlay.width = w;
            renderer->overlays[i].overlay.height = h;
            return;
        }
    }
}

void vulkan_overlay_set_alpha(VulkanOverlayRenderer* renderer, int overlay_id, float alpha) {
    if (!renderer) return;
    
    for (int i = 0; i < MAX_OVERLAYS; i++) {
        if (renderer->overlays[i].active && renderer->overlays[i].id == overlay_id) {
            renderer->overlays[i].overlay.alpha = alpha;
            return;
        }
    }
}

void vulkan_overlay_set_text(VulkanOverlayRenderer* renderer, int overlay_id, const char* text) {
    if (!renderer) return;
    
    for (int i = 0; i < MAX_OVERLAYS; i++) {
        if (renderer->overlays[i].active && renderer->overlays[i].id == overlay_id) {
            free((char*)renderer->overlays[i].overlay.text);
            renderer->overlays[i].overlay.text = text ? strdup(text) : NULL;
            return;
        }
    }
}

VulkanOverlay* vulkan_overlay_get(VulkanOverlayRenderer* renderer, int overlay_id) {
    if (!renderer) return NULL;
    
    for (int i = 0; i < MAX_OVERLAYS; i++) {
        if (renderer->overlays[i].active && renderer->overlays[i].id == overlay_id) {
            return &renderer->overlays[i].overlay;
        }
    }
    return NULL;
}

void vulkan_overlay_renderer_render(VulkanOverlayRenderer* renderer) {
    if (!renderer) return;
    
    for (int i = 0; i < MAX_OVERLAYS; i++) {
        struct VulkanOverlayInternal* overlay = &renderer->overlays[i];
        if (overlay->active && overlay->overlay.visible) {
            // Would render overlay here using Vulkan
            // For now, just log
            LOG_DEBUG("[VulkanOverlay] Rendering overlay %d at (%d,%d) size %dx%d",
                     overlay->id, overlay->overlay.x, overlay->overlay.y,
                     overlay->overlay.width, overlay->overlay.height);
        }
    }
}

// ============================================================================
// Preset Overlays
// ============================================================================

int vulkan_overlay_show_alt_tab(VulkanOverlayRenderer* renderer, 
                                 const char** items, int count, int selected) {
    if (!renderer || !items || count <= 0) return -1;
    
    // Create Alt-Tab overlay (centered, 400x200)
    int id = vulkan_overlay_create(renderer, OVERLAY_ALT_TAB, 0, 0, 400, 200);
    if (id < 0) return -1;
    
    // Would position in center of screen
    vulkan_overlay_set_position(renderer, id, 760, 440);  // Assuming 1920x1080
    
    // Build text from items
    char text[1024] = "Alt+Tab:\n";
    for (int i = 0; i < count && i < 5; i++) {
        char line[64];
        snprintf(line, sizeof(line), "  %s %s\n", 
                 i == selected ? ">" : " ", items[i]);
        strncat(text, line, sizeof(text) - strlen(text) - 1);
    }
    
    vulkan_overlay_set_text(renderer, id, text);
    vulkan_overlay_set_visible(renderer, id, true);
    
    LOG_INFO("[VulkanOverlay] Alt-Tab shown (%d items, selected=%d)", count, selected);
    return id;
}

int vulkan_overlay_show_notification(VulkanOverlayRenderer* renderer,
                                      const char* title, const char* message, int timeout_ms) {
    if (!renderer || !title) return -1;
    
    // Create notification overlay (top-right, 300x100)
    int id = vulkan_overlay_create(renderer, OVERLAY_NOTIFICATION, 1580, 40, 300, 100);
    if (id < 0) return -1;
    
    // Build text
    char text[512];
    snprintf(text, sizeof(text), "%s\n%s", title, message ? message : "");
    vulkan_overlay_set_text(renderer, id, text);
    vulkan_overlay_set_visible(renderer, id, true);
    
    LOG_INFO("[VulkanOverlay] Notification shown: %s", title);
    
    // Would auto-hide after timeout_ms
    (void)timeout_ms;
    
    return id;
}

int vulkan_overlay_show_osd(VulkanOverlayRenderer* renderer, 
                             const char* text, int timeout_ms) {
    if (!renderer || !text) return -1;
    
    // Create OSD overlay (center-bottom, auto-size)
    int len = strlen(text);
    int width = len * 10 + 40;  // Approximate
    int id = vulkan_overlay_create(renderer, OVERLAY_OSD, 
                                   (1920 - width) / 2, 900, width, 60);
    if (id < 0) return -1;
    
    vulkan_overlay_set_text(renderer, id, text);
    vulkan_overlay_set_visible(renderer, id, true);
    
    LOG_INFO("[VulkanOverlay] OSD shown: %s", text);
    
    // Would auto-hide after timeout_ms
    (void)timeout_ms;
    
    return id;
}
