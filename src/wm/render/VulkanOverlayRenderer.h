// Vulkan Overlay Renderer - For Alt-Tab, notifications, etc.

#pragma once

#include "VulkanRendererBridge.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Overlay types
typedef enum {
    OVERLAY_NONE = 0,
    OVERLAY_ALT_TAB,
    OVERLAY_NOTIFICATION,
    OVERLAY_OSD,
    OVERLAY_CUSTOM
} VulkanOverlayType;

// Overlay configuration
typedef struct {
    VulkanOverlayType type;
    int x, y;
    int width, height;
    float alpha;
    bool visible;
    const char* text;
    void* user_data;
} VulkanOverlay;

// Overlay renderer handle
typedef struct VulkanOverlayRenderer VulkanOverlayRenderer;

// Create/destroy overlay renderer
VulkanOverlayRenderer* vulkan_overlay_renderer_create(VulkanRenderer* renderer);
void vulkan_overlay_renderer_destroy(VulkanOverlayRenderer* renderer);

// Overlay management
int vulkan_overlay_create(
    VulkanOverlayRenderer* renderer,
    VulkanOverlayType type,
    int x, int y,
    int width, int height);

void vulkan_overlay_destroy(VulkanOverlayRenderer* renderer, int overlay_id);
void vulkan_overlay_set_visible(VulkanOverlayRenderer* renderer, int overlay_id, bool visible);
void vulkan_overlay_set_position(VulkanOverlayRenderer* renderer, int overlay_id, int x, int y);
void vulkan_overlay_set_size(VulkanOverlayRenderer* renderer, int overlay_id, int w, int h);
void vulkan_overlay_set_alpha(VulkanOverlayRenderer* renderer, int overlay_id, float alpha);
void vulkan_overlay_set_text(VulkanOverlayRenderer* renderer, int overlay_id, const char* text);

// Get overlay
VulkanOverlay* vulkan_overlay_get(VulkanOverlayRenderer* renderer, int overlay_id);

// Render all visible overlays
void vulkan_overlay_renderer_render(VulkanOverlayRenderer* renderer);

// Preset overlays
int vulkan_overlay_show_alt_tab(VulkanOverlayRenderer* renderer, const char** items, int count, int selected);
int vulkan_overlay_show_notification(VulkanOverlayRenderer* renderer, const char* title, const char* message, int timeout_ms);
int vulkan_overlay_show_osd(VulkanOverlayRenderer* renderer, const char* text, int timeout_ms);

#ifdef __cplusplus
}
#endif
