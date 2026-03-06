// Vulkan Renderer C Bridge - Isolates wlroots headers from C++

#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Opaque handle to Vulkan renderer
typedef struct VulkanRenderer VulkanRenderer;

// Vulkan renderer configuration
typedef struct {
    bool enableValidation;
    bool enableDebugMarkers;
    uint32_t desiredImageCount;
    bool enableVSync;
    bool enableHDR;
} VulkanRendererConfig;

// Create Vulkan renderer
VulkanRenderer* vulkan_renderer_create(const VulkanRendererConfig* config);

// Destroy Vulkan renderer
void vulkan_renderer_destroy(VulkanRenderer* renderer);

// Frame rendering
bool vulkan_renderer_begin_frame(VulkanRenderer* renderer);
bool vulkan_renderer_end_frame(VulkanRenderer* renderer);
void vulkan_renderer_wait_idle(VulkanRenderer* renderer);

// Resize
void vulkan_renderer_resize(VulkanRenderer* renderer, uint32_t width, uint32_t height);

// Clear color
void vulkan_renderer_set_clear_color(VulkanRenderer* renderer, 
                                     float r, float g, float b, float a);

// Draw operations
void vulkan_renderer_draw_quad(VulkanRenderer* renderer,
                               float x, float y, float w, float h);

// Texture operations (opaque handles)
typedef struct VulkanTexture VulkanTexture;

VulkanTexture* vulkan_renderer_create_texture_from_buffer(VulkanRenderer* renderer,
                                                          void* wlr_buffer);
void vulkan_renderer_destroy_texture(VulkanRenderer* renderer, VulkanTexture* texture);
void vulkan_renderer_bind_texture(VulkanRenderer* renderer, VulkanTexture* texture);

// Info
const char* vulkan_renderer_get_gpu_info(VulkanRenderer* renderer);
bool vulkan_renderer_is_available(void);

// Get native Vulkan handles (for advanced integration)
void* vulkan_renderer_get_instance(VulkanRenderer* renderer);
void* vulkan_renderer_get_device(VulkanRenderer* renderer);
void* vulkan_renderer_get_physical_device(VulkanRenderer* renderer);
void* vulkan_renderer_get_graphics_queue(VulkanRenderer* renderer);

// Vulkan 1.4 feature queries
bool vulkan_renderer_has_dynamic_rendering(VulkanRenderer* renderer);
bool vulkan_renderer_has_shader_objects(VulkanRenderer* renderer);
bool vulkan_renderer_has_maintenance5(VulkanRenderer* renderer);
uint32_t vulkan_renderer_get_version(VulkanRenderer* renderer);
const char* vulkan_renderer_get_version_string(VulkanRenderer* renderer);

#ifdef __cplusplus
}
#endif
