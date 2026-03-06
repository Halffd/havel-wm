// wlroots Binding - Connects wlroots to Vulkan renderer

#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Forward declarations
struct wlr_buffer;
struct wlr_scene;
struct wlr_scene_output;
struct wlr_output;
struct wlr_scene_tree;
struct wlr_scene_surface;

// wlroots buffer import result
typedef enum {
    WLR_BIND_SUCCESS = 0,
    WLR_BIND_FAILED,
    WLR_BIND_UNSUPPORTED_FORMAT,
    WLR_BIND_NO_MEMORY
} WlrBindResult;

// Imported texture handle
typedef struct WlrVulkanTexture WlrVulkanTexture;

// Import wlroots buffer as Vulkan texture
WlrBindResult wlr_buffer_import_as_vulkan(
    void* vulkan_renderer,
    struct wlr_buffer* buffer,
    WlrVulkanTexture** out_texture);

// Get Vulkan texture from imported buffer
void* wlr_texture_get_vulkan_image(WlrVulkanTexture* texture);
uint32_t wlr_texture_get_width(WlrVulkanTexture* texture);
uint32_t wlr_texture_get_height(WlrVulkanTexture* texture);

// Destroy imported texture
void wlr_vulkan_texture_destroy(void* vulkan_renderer, WlrVulkanTexture* texture);

// Scene graph rendering
typedef struct WlrSceneRenderer WlrSceneRenderer;

// Create scene renderer
WlrSceneRenderer* wlr_scene_renderer_create(
    void* vulkan_renderer,
    struct wlr_scene* scene);

void wlr_scene_renderer_destroy(WlrSceneRenderer* renderer);

// Render scene to output
bool wlr_scene_renderer_render(
    WlrSceneRenderer* scene_renderer,
    struct wlr_output* output,
    struct wlr_scene_output* scene_output);

// Render scene node recursively
bool wlr_scene_node_render(
    WlrSceneRenderer* scene_renderer,
    struct wlr_scene_tree* node,
    int x, int y);

// Set render options
void wlr_scene_renderer_set_gamma(WlrSceneRenderer* renderer, float gamma);
void wlr_scene_renderer_set_brightness(WlrSceneRenderer* renderer, float brightness);
void wlr_scene_renderer_set_blur_enabled(WlrSceneRenderer* renderer, bool enabled);

// Get statistics
typedef struct {
    uint32_t nodeCount;
    uint32_t surfaceCount;
    uint32_t textureCount;
    float renderTimeMs;
} WlrSceneStats;

void wlr_scene_renderer_get_stats(WlrSceneRenderer* renderer, WlrSceneStats* stats);

#ifdef __cplusplus
}
#endif
