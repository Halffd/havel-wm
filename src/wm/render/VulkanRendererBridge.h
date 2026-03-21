// Vulkan Renderer C Bridge - Isolates wlroots headers from C++

#pragma once

#include <vulkan/vulkan.h>
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
    // VSync options
    bool enableFrameTiming;
    uint32_t targetFrameRate;  // 0 = match display refresh
    float maxFrameLatency;     // Maximum frames in flight (1-3)
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

// Create texture from buffer with actual pixel data (for SHM fallback)
VulkanTexture* vulkan_renderer_create_texture_from_buffer_with_data(
    VulkanRenderer* renderer,
    void* pixelData,
    uint32_t width,
    uint32_t height);

// Create texture from buffer (deprecated, returns NULL)
VulkanTexture* vulkan_renderer_create_texture_from_buffer(VulkanRenderer* renderer,
                                                          void* wlr_buffer);
void vulkan_renderer_destroy_texture(VulkanRenderer* renderer, VulkanTexture* texture);
void vulkan_renderer_bind_texture(VulkanRenderer* renderer, VulkanTexture* texture);

// Update texture with new buffer data (for animated windows)
void vulkan_renderer_update_texture(VulkanRenderer* renderer, VulkanTexture* texture,
                                     void* pixelData, uint32_t width, uint32_t height);

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

// VSync and frame timing
void vulkan_renderer_set_vsync_enabled(VulkanRenderer* renderer, bool enabled);
bool vulkan_renderer_is_vsync_enabled(VulkanRenderer* renderer);
void vulkan_renderer_set_target_frame_rate(VulkanRenderer* renderer, uint32_t fps);
uint32_t vulkan_renderer_get_target_frame_rate(VulkanRenderer* renderer);
void vulkan_renderer_set_max_frame_latency(VulkanRenderer* renderer, uint32_t latency);
uint32_t vulkan_renderer_get_max_frame_latency(VulkanRenderer* renderer);

// HDR support
// Note: surface is a VkSurfaceKHR handle (cast to void*)
bool vulkan_renderer_is_hdr_capable(VulkanRenderer* renderer, void* surface);
void vulkan_renderer_set_hdr_enabled(VulkanRenderer* renderer, void* surface, bool enabled);
bool vulkan_renderer_is_hdr_enabled(VulkanRenderer* renderer);
void vulkan_renderer_set_hdr_exposure(VulkanRenderer* renderer, float exposure);
void vulkan_renderer_set_hdr_tonemap(VulkanRenderer* renderer, float peakNits, float gamma);

// Frame timing statistics
typedef struct {
    float currentFPS;
    float averageFPS;
    float frameTimeMs;
    float averageFrameTimeMs;
    uint32_t droppedFrames;
    bool vsyncEnabled;
    uint32_t presentMode;  // FIFO, MAILBOX, IMMEDIATE
} VulkanFrameStats;

void vulkan_renderer_get_frame_stats(VulkanRenderer* renderer, VulkanFrameStats* stats);

// KHR_surface/presentation queries
bool vulkan_renderer_get_surface_formats(VulkanRenderer* renderer, void* surface,
                                         VkSurfaceFormatKHR** formats, uint32_t* count);
bool vulkan_renderer_get_surface_present_modes(VulkanRenderer* renderer, void* surface,
                                               VkPresentModeKHR** modes, uint32_t* count);
bool vulkan_renderer_select_present_mode(VulkanRenderer* renderer, void* surface,
                                         VkPresentModeKHR* selected_mode);

#ifdef __cplusplus
}
#endif
