// Vulkan Scene Compositor - Integrates Vulkan with wlroots scene graph

#pragma once

#include "VulkanRendererBridge.h"
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Forward declarations
struct wlr_scene;
struct wlr_scene_output;
struct wlr_output;
struct wlr_buffer;

// Vulkan scene compositor configuration
typedef struct {
    bool enableHDR;
    bool enableWideColor;
    bool enableVSync;
    float gamma;
    int temperature;  // Kelvin
    float brightness;
    bool enableBlur;
    float blurRadius;
} VulkanSceneConfig;

// Vulkan scene compositor handle
typedef struct VulkanSceneCompositor VulkanSceneCompositor;

// Create/destroy scene compositor
VulkanSceneCompositor* vulkan_scene_compositor_create(
    VulkanRenderer* renderer,
    struct wlr_scene* scene,
    const VulkanSceneConfig* config);

void vulkan_scene_compositor_destroy(VulkanSceneCompositor* compositor);

// Render scene to output
bool vulkan_scene_compositor_render(
    VulkanSceneCompositor* compositor,
    struct wlr_output* output,
    struct wlr_scene_output* scene_output);

// Import wlroots buffer as Vulkan texture
typedef struct VulkanSceneTexture VulkanSceneTexture;

VulkanSceneTexture* vulkan_scene_import_buffer(
    VulkanSceneCompositor* compositor,
    struct wlr_buffer* buffer);

void vulkan_scene_destroy_texture(
    VulkanSceneCompositor* compositor,
    VulkanSceneTexture* texture);

// Get Vulkan texture handle for rendering
void* vulkan_scene_texture_get_image(VulkanSceneTexture* texture);
uint32_t vulkan_scene_texture_get_width(VulkanSceneTexture* texture);
uint32_t vulkan_scene_texture_get_height(VulkanSceneTexture* texture);

// Post-processing effects
void vulkan_scene_set_gamma(VulkanSceneCompositor* compositor, float gamma);
void vulkan_scene_set_temperature(VulkanSceneCompositor* compositor, int kelvin);
void vulkan_scene_set_brightness(VulkanSceneCompositor* compositor, float brightness);
void vulkan_scene_set_blur_enabled(VulkanSceneCompositor* compositor, bool enabled);
void vulkan_scene_set_blur_radius(VulkanSceneCompositor* compositor, float radius);

// Statistics
typedef struct {
    uint32_t frameCount;
    float fps;
    uint32_t textureCount;
    size_t gpuMemoryUsed;
    float renderTimeMs;
} VulkanSceneStats;

void vulkan_scene_compositor_get_stats(
    VulkanSceneCompositor* compositor,
    VulkanSceneStats* stats);

#ifdef __cplusplus
}
#endif
