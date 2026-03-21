// wlroots Binding Implementation - Connects wlroots to Vulkan renderer

#include "WlrBinding.h"
#include "VulkanRendererBridge.h"
#include "VulkanDmaBuf.h"
#include <Logger.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <wlr/types/wlr_buffer.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/render/dmabuf.h>

// Internal texture wrapper
struct WlrVulkanTexture {
    void* vulkan_texture;
    struct wlr_buffer* buffer;
    uint32_t width;
    uint32_t height;
    bool owns_buffer;
};

// Internal scene renderer
struct WlrSceneRenderer {
    void* vulkan_renderer;
    struct wlr_scene* scene;
    
    // Render state
    float gamma;
    float brightness;
    bool blurEnabled;
    
    // Statistics
    WlrSceneStats stats;
    struct timespec renderStartTime;
};

// ============================================================================
// Buffer Import
// ============================================================================

WlrBindResult wlr_buffer_import_as_vulkan(
    void* vulkan_renderer,
    struct wlr_buffer* buffer,
    WlrVulkanTexture** out_texture) {
    
    if (!vulkan_renderer || !buffer || !out_texture) {
        return WLR_BIND_FAILED;
    }
    
    // Allocate texture wrapper
    WlrVulkanTexture* texture = (WlrVulkanTexture*)calloc(1, sizeof(WlrVulkanTexture));
    if (!texture) {
        return WLR_BIND_NO_MEMORY;
    }
    
    texture->buffer = buffer;
    texture->owns_buffer = false;
    
    // Try to get DMA-BUF attributes from buffer
    struct wlr_dmabuf_attributes dmabuf_attrs;
    if (wlr_buffer_get_dmabuf(buffer, &dmabuf_attrs)) {
        // Import DMA-BUF as Vulkan texture
        VulkanDmaBufTexture* vk_texture;
        DmaBufImportResult result = vulkan_import_dmabuf(
            vulkan_renderer, &dmabuf_attrs, &vk_texture);
        
        if (result == DMA_BUF_IMPORT_SUCCESS) {
            texture->vulkan_texture = vk_texture;
            texture->width = dmabuf_attrs.width;
            texture->height = dmabuf_attrs.height;
            texture->owns_buffer = false;  // Buffer owned by wlroots
            
            *out_texture = texture;
            LOG_DEBUG("[WlrBinding] Imported DMA-BUF %dx%d", 
                     texture->width, texture->height);
            return WLR_BIND_SUCCESS;
        } else {
            LOG_WARN("[WlrBinding] DMA-BUF import failed: %d", result);
        }
    }
    
    // Fallback: Try SHM (shared memory) buffer
    // In production, would map SHM and upload to Vulkan texture
    LOG_DEBUG("[WlrBinding] Buffer is not DMA-BUF, trying SHM fallback");
    
    // For now, create placeholder texture
    texture->vulkan_texture = vulkan_renderer_create_texture_from_buffer(
        vulkan_renderer, buffer);
    
    if (texture->vulkan_texture) {
        texture->width = 1920;  // Would query from buffer
        texture->height = 1080;
        *out_texture = texture;
        return WLR_BIND_SUCCESS;
    }
    
    free(texture);
    return WLR_BIND_UNSUPPORTED_FORMAT;
}

void* wlr_texture_get_vulkan_image(WlrVulkanTexture* texture) {
    if (!texture) return NULL;
    
    // Get Vulkan image from DMA-BUF texture
    if (texture->vulkan_texture) {
        return vulkan_dmabuf_texture_get_image(texture->vulkan_texture);
    }
    return NULL;
}

uint32_t wlr_texture_get_width(WlrVulkanTexture* texture) {
    return texture ? texture->width : 0;
}

uint32_t wlr_texture_get_height(WlrVulkanTexture* texture) {
    return texture ? texture->height : 0;
}

void wlr_vulkan_texture_destroy(void* vulkan_renderer, WlrVulkanTexture* texture) {
    if (!texture) return;
    
    // Destroy Vulkan texture if we own it
    if (texture->vulkan_texture) {
        vulkan_destroy_dmabuf_texture(vulkan_renderer, texture->vulkan_texture);
    }
    
    // Don't destroy buffer - owned by wlroots
    
    free(texture);
}

// ============================================================================
// Scene Graph Rendering
// ============================================================================

WlrSceneRenderer* wlr_scene_renderer_create(
    void* vulkan_renderer,
    struct wlr_scene* scene) {
    
    if (!vulkan_renderer || !scene) {
        return NULL;
    }
    
    WlrSceneRenderer* renderer = (WlrSceneRenderer*)calloc(1, sizeof(WlrSceneRenderer));
    if (!renderer) {
        return NULL;
    }
    
    renderer->vulkan_renderer = vulkan_renderer;
    renderer->scene = scene;
    
    // Default render state
    renderer->gamma = 1.0f;
    renderer->brightness = 1.0f;
    renderer->blurEnabled = false;
    
    // Initialize stats
    memset(&renderer->stats, 0, sizeof(WlrSceneStats));
    
    LOG_INFO("[WlrBinding] Scene renderer created");
    return renderer;
}

void wlr_scene_renderer_destroy(WlrSceneRenderer* renderer) {
    if (!renderer) return;
    
    LOG_INFO("[WlrBinding] Scene renderer destroyed");
    free(renderer);
}

// Internal: Render a single scene node
static bool render_scene_node(
    WlrSceneRenderer* scene_renderer,
    struct wlr_scene_tree* node,
    int x, int y) {
    
    if (!node || !node->node.enabled) {
        return false;
    }
    
    scene_renderer->stats.nodeCount++;
    
    // Update position with parent offset
    int node_x = x + node->node.x;
    int node_y = y + node->node.y;
    
    // Check node type
    if (node->node.type == WLR_SCENE_NODE_BUFFER) {
        struct wlr_scene_buffer* scene_buffer = 
            (struct wlr_scene_buffer*)node;
        
        scene_renderer->stats.surfaceCount++;
        
        // Get buffer from scene buffer (wlroots 0.20 API)
        struct wlr_buffer* buffer = scene_buffer->buffer;
        if (!buffer) {
            return false;
        }
        
        // Import buffer as Vulkan texture
        WlrVulkanTexture* texture;
        WlrBindResult result = wlr_buffer_import_as_vulkan(
            scene_renderer->vulkan_renderer, buffer, &texture);
        
        if (result != WLR_BIND_SUCCESS) {
            LOG_DEBUG("[WlrBinding] Failed to import buffer");
            return false;
        }
        
        // Get texture dimensions
        uint32_t tex_width = wlr_texture_get_width(texture);
        uint32_t tex_height = wlr_texture_get_height(texture);

        // Use destination size from scene buffer
        int draw_width = scene_buffer->dst_width > 0 ? scene_buffer->dst_width : (int)tex_width;
        int draw_height = scene_buffer->dst_height > 0 ? scene_buffer->dst_height : (int)tex_height;
        
        // Bind and draw texture
        vulkan_renderer_bind_texture(scene_renderer->vulkan_renderer, texture->vulkan_texture);
        vulkan_renderer_draw_quad(scene_renderer->vulkan_renderer,
                                 node_x, node_y, draw_width, draw_height);
        
        scene_renderer->stats.textureCount++;
        
        // Clean up imported texture (not the buffer)
        wlr_vulkan_texture_destroy(scene_renderer->vulkan_renderer, texture);
        
        return true;
    } else if (node->node.type == WLR_SCENE_NODE_TREE) {
        // Recursively render children
        struct wlr_scene_node* child;
        wl_list_for_each(child, &node->children, link) {
            if (child->type == WLR_SCENE_NODE_TREE) {
                render_scene_node(scene_renderer, 
                                 (struct wlr_scene_tree*)child,
                                 node_x, node_y);
            }
        }
    }
    
    return true;
}

bool wlr_scene_node_render(
    WlrSceneRenderer* scene_renderer,
    struct wlr_scene_tree* node,
    int x, int y) {
    
    return render_scene_node(scene_renderer, node, x, y);
}

bool wlr_scene_renderer_render(
    WlrSceneRenderer* scene_renderer,
    struct wlr_output* output,
    struct wlr_scene_output* scene_output) {
    
    if (!scene_renderer || !output || !scene_output) {
        return false;
    }
    
    // Start timing
    clock_gettime(CLOCK_MONOTONIC, &scene_renderer->renderStartTime);
    
    // Reset stats
    memset(&scene_renderer->stats, 0, sizeof(WlrSceneStats));
    
    // Begin Vulkan frame
    if (!vulkan_renderer_begin_frame(scene_renderer->vulkan_renderer)) {
        return false;
    }
    
    // Set clear color
    vulkan_renderer_set_clear_color(scene_renderer->vulkan_renderer,
                                   0.1f, 0.1f, 0.15f, 1.0f);
    
    // Render from root node
    struct wlr_scene_tree* root = &scene_renderer->scene->tree;
    render_scene_node(scene_renderer, root, 0, 0);
    
    // End Vulkan frame
    if (!vulkan_renderer_end_frame(scene_renderer->vulkan_renderer)) {
        return false;
    }
    
    // Calculate render time
    struct timespec end_time;
    clock_gettime(CLOCK_MONOTONIC, &end_time);
    
    int64_t render_time_ns = 
        (end_time.tv_sec - scene_renderer->renderStartTime.tv_sec) * 1000000000LL +
        (end_time.tv_nsec - scene_renderer->renderStartTime.tv_nsec);
    
    scene_renderer->stats.renderTimeMs = (float)render_time_ns / 1000000.0f;
    
    LOG_DEBUG("[WlrBinding] Rendered %u nodes, %u surfaces, %u textures in %.2fms",
             scene_renderer->stats.nodeCount,
             scene_renderer->stats.surfaceCount,
             scene_renderer->stats.textureCount,
             scene_renderer->stats.renderTimeMs);
    
    return true;
}

void wlr_scene_renderer_set_gamma(WlrSceneRenderer* renderer, float gamma) {
    if (!renderer) return;
    renderer->gamma = gamma;
}

void wlr_scene_renderer_set_brightness(WlrSceneRenderer* renderer, float brightness) {
    if (!renderer) return;
    renderer->brightness = brightness;
}

void wlr_scene_renderer_set_blur_enabled(WlrSceneRenderer* renderer, bool enabled) {
    if (!renderer) return;
    renderer->blurEnabled = enabled;
}

void wlr_scene_renderer_get_stats(WlrSceneRenderer* renderer, WlrSceneStats* stats) {
    if (!renderer || !stats) return;
    *stats = renderer->stats;
}
