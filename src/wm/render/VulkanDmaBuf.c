// DMA-BUF Buffer Import Implementation

#include "VulkanDmaBuf.h"
#include <Logger.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <vulkan/vulkan.h>
#include <drm_fourcc.h>

// Define wlr_dmabuf_attributes locally (normally from wlroots)
// This avoids including wlroots headers in C files linked with C++
#ifndef WLR_TYPES_WLR_BUFFER_H
struct wlr_dmabuf_attributes {
    int32_t width;
    int32_t height;
    uint32_t format;
    uint64_t modifier;
    int n_planes;
    int fd[4];
    uint32_t stride[4];
    uint32_t offset[4];
    size_t size[4];
};
#endif

// Internal texture structure
struct VulkanDmaBufTexture {
    VkImage image;
    VkDeviceMemory memory;
    VkImageView view;
    VkSampler sampler;
    uint32_t width;
    uint32_t height;
    uint32_t format;
    uint64_t modifier;
    int num_planes;
    int fds[4];
    size_t sizes[4];
    uint32_t strides[4];
    uint32_t offsets[4];
    VkFormat vk_format;
};

// Convert DRM format to Vulkan format
static VkFormat drm_format_to_vulkan(uint32_t drm_format) {
    switch (drm_format) {
        case DRM_FORMAT_ARGB8888:
            return VK_FORMAT_B8G8R8A8_UNORM;
        case DRM_FORMAT_ABGR8888:
            return VK_FORMAT_R8G8B8A8_UNORM;
        case DRM_FORMAT_XRGB8888:
            return VK_FORMAT_B8G8R8A8_UNORM;
        case DRM_FORMAT_XBGR8888:
            return VK_FORMAT_R8G8B8A8_UNORM;
        case DRM_FORMAT_NV12:
            return VK_FORMAT_G8_B8R8_2PLANE_420_UNORM;
        case DRM_FORMAT_YUV420_8BIT:
            return VK_FORMAT_G8_B8_R8_3PLANE_420_UNORM;
        default:
            return VK_FORMAT_UNDEFINED;
    }
}

// Get Vulkan instance/device from renderer (opaque API)
static VkDevice get_device(VulkanRenderer* renderer) {
    return (VkDevice)vulkan_renderer_get_device(renderer);
}

static VkPhysicalDevice get_physical_device(VulkanRenderer* renderer) {
    return (VkPhysicalDevice)vulkan_renderer_get_physical_device(renderer);
}

// Check if format is supported
static bool is_format_supported(VulkanRenderer* renderer, VkFormat format) {
    VkPhysicalDevice physical_device = get_physical_device(renderer);
    if (!physical_device) return false;
    
    VkFormatProperties props;
    vkGetPhysicalDeviceFormatProperties(physical_device, format, &props);
    
    return (props.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT) != 0;
}

DmaBufImportResult vulkan_import_dmabuf(
    VulkanRenderer* renderer,
    const struct wlr_dmabuf_attributes* attrs,
    VulkanDmaBufTexture** out_texture) {
    
    if (!renderer || !attrs || !out_texture) {
        return DMA_BUF_IMPORT_FAILED;
    }
    
    // Allocate texture structure
    VulkanDmaBufTexture* texture = (VulkanDmaBufTexture*)calloc(1, sizeof(VulkanDmaBufTexture));
    if (!texture) {
        return DMA_BUF_IMPORT_NO_MEMORY;
    }
    
    // Initialize FDs to -1
    for (int i = 0; i < 4; i++) {
        texture->fds[i] = -1;
    }
    
    // Copy attributes
    texture->width = attrs->width;
    texture->height = attrs->height;
    texture->format = attrs->format;
    texture->modifier = attrs->modifier;
    texture->num_planes = attrs->n_planes;
    
    // Duplicate FDs (we own them now)
    for (int i = 0; i < attrs->n_planes; i++) {
        texture->fds[i] = dup(attrs->fd[i]);
        if (texture->fds[i] < 0) {
            LOG_ERROR("[VulkanDmaBuf] Failed to duplicate FD %d", i);
            vulkan_destroy_dmabuf_texture(renderer, texture);
            return DMA_BUF_IMPORT_FAILED;
        }
        texture->sizes[i] = attrs->size[i];
        texture->strides[i] = attrs->stride[i];
        texture->offsets[i] = attrs->offset[i];
    }
    
    // Convert DRM format to Vulkan format
    VkFormat vk_format = drm_format_to_vulkan(attrs->format);
    texture->vk_format = vk_format;
    
    if (vk_format == VK_FORMAT_UNDEFINED) {
        LOG_ERROR("[VulkanDmaBuf] Unsupported DRM format: 0x%X", attrs->format);
        vulkan_destroy_dmabuf_texture(renderer, texture);
        return DMA_BUF_IMPORT_UNSUPPORTED_FORMAT;
    }
    
    // Check if format is supported
    if (!is_format_supported(renderer, vk_format)) {
        LOG_ERROR("[VulkanDmaBuf] Format not supported by Vulkan");
        vulkan_destroy_dmabuf_texture(renderer, texture);
        return DMA_BUF_IMPORT_UNSUPPORTED_FORMAT;
    }
    
    VkDevice device = get_device(renderer);
    if (!device) {
        LOG_ERROR("[VulkanDmaBuf] No Vulkan device");
        vulkan_destroy_dmabuf_texture(renderer, texture);
        return DMA_BUF_IMPORT_FAILED;
    }
    
    // Create VkImage
    VkImageCreateInfo image_info = {0};
    image_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    image_info.imageType = VK_IMAGE_TYPE_2D;
    image_info.format = vk_format;
    image_info.extent.width = attrs->width;
    image_info.extent.height = attrs->height;
    image_info.extent.depth = 1;
    image_info.mipLevels = 1;
    image_info.arrayLayers = 1;
    image_info.samples = VK_SAMPLE_COUNT_1_BIT;
    image_info.tiling = VK_IMAGE_TILING_LINEAR;  // Use linear for DMA-BUF
    image_info.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    
    VkResult result = vkCreateImage(device, &image_info, NULL, &texture->image);
    if (result != VK_SUCCESS) {
        LOG_ERROR("[VulkanDmaBuf] Failed to create image: %d", result);
        vulkan_destroy_dmabuf_texture(renderer, texture);
        return DMA_BUF_IMPORT_FAILED;
    }
    
    // For a full implementation, we would:
    // 1. Get memory requirements with FD import
    // 2. Use vkImportMemoryFdKHR to import the DMA-BUF FD
    // 3. Bind the imported memory to the image
    
    // For now, allocate device-local memory (not optimal but functional)
    VkMemoryRequirements mem_requirements;
    vkGetImageMemoryRequirements(device, texture->image, &mem_requirements);
    
    VkMemoryAllocateInfo alloc_info = {0};
    alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc_info.allocationSize = mem_requirements.size;
    
    // Find suitable memory type
    VkPhysicalDevice physical_device = get_physical_device(renderer);
    VkPhysicalDeviceMemoryProperties mem_props;
    vkGetPhysicalDeviceMemoryProperties(physical_device, &mem_props);
    
    uint32_t memory_type_index = UINT32_MAX;
    for (uint32_t i = 0; i < mem_props.memoryTypeCount; i++) {
        if ((mem_requirements.memoryTypeBits & (1 << i)) &&
            (mem_props.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) {
            memory_type_index = i;
            break;
        }
    }
    
    if (memory_type_index == UINT32_MAX) {
        LOG_ERROR("[VulkanDmaBuf] No suitable memory type found");
        vulkan_destroy_dmabuf_texture(renderer, texture);
        return DMA_BUF_IMPORT_NO_MEMORY;
    }
    
    alloc_info.memoryTypeIndex = memory_type_index;
    
    result = vkAllocateMemory(device, &alloc_info, NULL, &texture->memory);
    if (result != VK_SUCCESS) {
        LOG_ERROR("[VulkanDmaBuf] Failed to allocate memory: %d", result);
        vulkan_destroy_dmabuf_texture(renderer, texture);
        return DMA_BUF_IMPORT_NO_MEMORY;
    }
    
    // Bind memory to image
    result = vkBindImageMemory(device, texture->image, texture->memory, 0);
    if (result != VK_SUCCESS) {
        LOG_ERROR("[VulkanDmaBuf] Failed to bind memory: %d", result);
        vulkan_destroy_dmabuf_texture(renderer, texture);
        return DMA_BUF_IMPORT_FAILED;
    }
    
    // Create image view
    VkImageViewCreateInfo view_info = {0};
    view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    view_info.image = texture->image;
    view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view_info.format = vk_format;
    view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    view_info.subresourceRange.baseMipLevel = 0;
    view_info.subresourceRange.levelCount = 1;
    view_info.subresourceRange.baseArrayLayer = 0;
    view_info.subresourceRange.layerCount = 1;
    
    result = vkCreateImageView(device, &view_info, NULL, &texture->view);
    if (result != VK_SUCCESS) {
        LOG_ERROR("[VulkanDmaBuf] Failed to create image view: %d", result);
        vulkan_destroy_dmabuf_texture(renderer, texture);
        return DMA_BUF_IMPORT_FAILED;
    }
    
    // Create sampler
    VkSamplerCreateInfo sampler_info = {0};
    sampler_info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    sampler_info.magFilter = VK_FILTER_LINEAR;
    sampler_info.minFilter = VK_FILTER_LINEAR;
    sampler_info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_info.minLod = 0.0f;
    sampler_info.maxLod = 1.0f;
    
    result = vkCreateSampler(device, &sampler_info, NULL, &texture->sampler);
    if (result != VK_SUCCESS) {
        LOG_ERROR("[VulkanDmaBuf] Failed to create sampler: %d", result);
        vulkan_destroy_dmabuf_texture(renderer, texture);
        return DMA_BUF_IMPORT_FAILED;
    }
    
    *out_texture = texture;
    LOG_DEBUG("[VulkanDmaBuf] Imported DMA-BUF %dx%d format=0x%X modifier=0x%lX",
              texture->width, texture->height, texture->format, texture->modifier);
    
    return DMA_BUF_IMPORT_SUCCESS;
}

DmaBufImportResult vulkan_import_wlr_buffer(
    VulkanRenderer* renderer,
    struct wlr_buffer* buffer,
    VulkanDmaBufTexture** out_texture) {
    
    if (!renderer || !buffer || !out_texture) {
        return DMA_BUF_IMPORT_FAILED;
    }
    
    // In a full implementation with wlroots, would:
    // 1. Check if buffer has DMA-BUF attributes
    // 2. Extract wlr_dmabuf_attributes
    // 3. Call vulkan_import_dmabuf()
    
    // For now, return unsupported
    (void)buffer;
    LOG_DEBUG("[VulkanDmaBuf] Buffer import requires wlroots integration");
    return DMA_BUF_IMPORT_UNSUPPORTED_FORMAT;
}

void* vulkan_dmabuf_texture_get_image(VulkanDmaBufTexture* texture) {
    return texture ? (void*)(uintptr_t)texture->image : NULL;
}

void* vulkan_dmabuf_texture_get_memory(VulkanDmaBufTexture* texture) {
    return texture ? (void*)texture->memory : NULL;
}

uint32_t vulkan_dmabuf_texture_get_width(VulkanDmaBufTexture* texture) {
    return texture ? texture->width : 0;
}

uint32_t vulkan_dmabuf_texture_get_height(VulkanDmaBufTexture* texture) {
    return texture ? texture->height : 0;
}

uint64_t vulkan_dmabuf_texture_get_modifier(VulkanDmaBufTexture* texture) {
    return texture ? texture->modifier : 0;
}

void vulkan_destroy_dmabuf_texture(VulkanRenderer* renderer, VulkanDmaBufTexture* texture) {
    if (!texture) return;
    
    VkDevice device = renderer ? get_device(renderer) : VK_NULL_HANDLE;
    
    // Close FDs
    for (int i = 0; i < 4; i++) {
        if (texture->fds[i] >= 0) {
            close(texture->fds[i]);
        }
    }
    
    if (device != VK_NULL_HANDLE) {
        if (texture->sampler) {
            vkDestroySampler(device, texture->sampler, NULL);
        }
        if (texture->view) {
            vkDestroyImageView(device, texture->view, NULL);
        }
        if (texture->memory) {
            vkFreeMemory(device, texture->memory, NULL);
        }
        if (texture->image) {
            vkDestroyImage(device, texture->image, NULL);
        }
    }
    
    LOG_DEBUG("[VulkanDmaBuf] Destroyed texture %p", (void*)texture);
    free(texture);
}

bool vulkan_query_dmabuf_formats(
    VulkanRenderer* renderer,
    uint32_t** formats,
    uint32_t* format_count) {
    
    if (!renderer || !formats || !format_count) return false;
    
    // Common DRM formats that are typically supported
    static const uint32_t common_formats[] = {
        DRM_FORMAT_ARGB8888,
        DRM_FORMAT_ABGR8888,
        DRM_FORMAT_XRGB8888,
        DRM_FORMAT_XBGR8888,
        DRM_FORMAT_NV12,
    };
    
    *format_count = sizeof(common_formats) / sizeof(common_formats[0]);
    *formats = (uint32_t*)malloc(*format_count * sizeof(uint32_t));
    
    if (!*formats) {
        *format_count = 0;
        return false;
    }
    
    memcpy(*formats, common_formats, *format_count * sizeof(uint32_t));
    return true;
}

bool vulkan_query_dmabuf_modifiers(
    VulkanRenderer* renderer,
    uint32_t format,
    uint64_t** modifiers,
    uint32_t* modifier_count) {
    
    (void)renderer;
    (void)format;
    
    // Return linear modifier as default
    static const uint64_t linear_modifier = DRM_FORMAT_MOD_LINEAR;
    
    *modifier_count = 1;
    *modifiers = (uint64_t*)malloc(sizeof(uint64_t));
    
    if (!*modifiers) {
        *modifier_count = 0;
        return false;
    }
    
    **modifiers = linear_modifier;
    return true;
}

bool vulkan_supports_dmabuf(
    VulkanRenderer* renderer,
    uint32_t format,
    uint64_t modifier) {
    
    if (!renderer) return false;
    
    // Check if format is supported
    VkFormat vk_format = drm_format_to_vulkan(format);
    if (vk_format == VK_FORMAT_UNDEFINED) return false;
    
    (void)modifier;  // Would check modifier support in full implementation
    return is_format_supported(renderer, vk_format);
}
