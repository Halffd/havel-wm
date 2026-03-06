// Vulkan Renderer - Hardware-accelerated rendering for Havel WM

#pragma once

#include <vulkan/vulkan.h>
#include <wayland-server-core.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Vulkan renderer configuration
struct VulkanRendererConfig {
    bool enableValidation;
    bool enableDebugMarkers;
    uint32_t desiredImageCount;
    bool enableVSync;
    bool enableHDR;
};

// Vulkan renderer state
struct VulkanRenderer {
    // Instance
    VkInstance instance;
    VkDebugUtilsMessengerEXT debugMessenger;
    
    // Physical/Logical device
    VkPhysicalDevice physicalDevice;
    VkDevice device;
    
    // Queue family indices
    uint32_t graphicsFamilyIndex;
    uint32_t presentFamilyIndex;
    
    // Queues
    VkQueue graphicsQueue;
    VkQueue presentQueue;
    
    // Swapchain
    VkSwapchainKHR swapchain;
    VkFormat swapchainFormat;
    VkExtent2D swapchainExtent;
    VkImage* swapchainImages;
    VkImageView* swapchainImageViews;
    uint32_t swapchainImageCount;
    
    // Render pass
    VkRenderPass renderPass;
    
    // Graphics pipeline
    VkPipelineLayout pipelineLayout;
    VkPipeline graphicsPipeline;
    
    // Framebuffers
    VkFramebuffer* framebuffers;
    
    // Command pool and buffers
    VkCommandPool commandPool;
    VkCommandBuffer* commandBuffers;
    
    // Synchronization
    VkSemaphore* imageAvailableSemaphores;
    VkSemaphore* renderFinishedSemaphores;
    VkFence* inFlightFences;
    
    // Current frame index
    uint32_t currentFrame;
    bool framebufferResized;
    
    // Configuration
    struct VulkanRendererConfig config;
    
    // wlroots integration
    struct wlr_renderer* wlrRenderer;
    struct wlr_scene* scene;
};

// Initialize Vulkan renderer
bool vulkan_renderer_init(struct VulkanRenderer* renderer, 
                          struct VulkanRendererConfig* config);

// Cleanup Vulkan renderer
void vulkan_renderer_cleanup(struct VulkanRenderer* renderer);

// Begin frame rendering
bool vulkan_renderer_begin_frame(struct VulkanRenderer* renderer);

// End frame rendering
bool vulkan_renderer_end_frame(struct VulkanRenderer* renderer);

// Wait for GPU to finish
void vulkan_renderer_wait_idle(struct VulkanRenderer* renderer);

// Resize swapchain
void vulkan_renderer_resize_swapchain(struct VulkanRenderer* renderer,
                                      uint32_t width, uint32_t height);

// Set clear color
void vulkan_renderer_set_clear_color(struct VulkanRenderer* renderer,
                                     float r, float g, float b, float a);

// Draw textured quad
void vulkan_renderer_draw_quad(struct VulkanRenderer* renderer,
                               float x, float y, float w, float h,
                               VkDescriptorSet descriptorSet);

// Create texture from wlroots buffer
VkResult vulkan_renderer_create_texture(struct VulkanRenderer* renderer,
                                        struct wlr_buffer* buffer,
                                        VkImage* image,
                                        VkImageView* view,
                                        VkSampler* sampler);

// Destroy texture
void vulkan_renderer_destroy_texture(struct VulkanRenderer* renderer,
                                     VkImage image,
                                     VkImageView view,
                                     VkSampler sampler);

// Get Vulkan instance
VkInstance vulkan_renderer_get_instance(struct VulkanRenderer* renderer);

// Get Vulkan device
VkDevice vulkan_renderer_get_device(struct VulkanRenderer* renderer);

// Get physical device
VkPhysicalDevice vulkan_renderer_get_physical_device(
    struct VulkanRenderer* renderer);

// Get graphics queue
VkQueue vulkan_renderer_get_graphics_queue(struct VulkanRenderer* renderer);

// Check if Vulkan is available
bool vulkan_renderer_is_available(void);

// Get Vulkan renderer info
const char* vulkan_renderer_get_info(struct VulkanRenderer* renderer);

#ifdef __cplusplus
}
#endif
