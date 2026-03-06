// DMA-BUF Buffer Import - Import wlroots buffers as Vulkan textures

#pragma once

#include "VulkanRendererBridge.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Forward declarations
struct wlr_buffer;
struct wlr_dmabuf_attributes;

// DMA-BUF import result
typedef enum {
    DMA_BUF_IMPORT_SUCCESS = 0,
    DMA_BUF_IMPORT_FAILED,
    DMA_BUF_IMPORT_UNSUPPORTED_FORMAT,
    DMA_BUF_IMPORT_UNSUPPORTED_MODIFIER,
    DMA_BUF_IMPORT_NO_MEMORY
} DmaBufImportResult;

// Vulkan DMA-BUF texture
typedef struct VulkanDmaBufTexture VulkanDmaBufTexture;

// Import DMA-BUF attributes as Vulkan texture
DmaBufImportResult vulkan_import_dmabuf(
    VulkanRenderer* renderer,
    const struct wlr_dmabuf_attributes* attrs,
    VulkanDmaBufTexture** out_texture);

// Import wlroots buffer (auto-detects DMA-BUF or shm)
DmaBufImportResult vulkan_import_wlr_buffer(
    VulkanRenderer* renderer,
    struct wlr_buffer* buffer,
    VulkanDmaBufTexture** out_texture);

// Get Vulkan image from imported texture
void* vulkan_dmabuf_texture_get_image(VulkanDmaBufTexture* texture);
void* vulkan_dmabuf_texture_get_memory(VulkanDmaBufTexture* texture);
uint32_t vulkan_dmabuf_texture_get_width(VulkanDmaBufTexture* texture);
uint32_t vulkan_dmabuf_texture_get_height(VulkanDmaBufTexture* texture);
uint64_t vulkan_dmabuf_texture_get_modifier(VulkanDmaBufTexture* texture);

// Destroy imported texture
void vulkan_destroy_dmabuf_texture(VulkanRenderer* renderer, VulkanDmaBufTexture* texture);

// Query supported formats
bool vulkan_query_dmabuf_formats(
    VulkanRenderer* renderer,
    uint32_t** formats,
    uint32_t* format_count);

// Query supported modifiers for a format
bool vulkan_query_dmabuf_modifiers(
    VulkanRenderer* renderer,
    uint32_t format,
    uint64_t** modifiers,
    uint32_t* modifier_count);

// Check if a format/modifier combination is supported
bool vulkan_supports_dmabuf(
    VulkanRenderer* renderer,
    uint32_t format,
    uint64_t modifier);

#ifdef __cplusplus
}
#endif
