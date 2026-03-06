// Unified Renderer Implementation - Auto-selects Vulkan or GLES2

#include "UnifiedRenderer.h"
#include <Logger.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>
#include <vulkan/vulkan.h>

// Internal unified renderer structure
struct UnifiedRenderer {
    RendererBackend backend;
    
    // Vulkan renderer (if selected)
    VulkanRenderer* vulkan;
    
    // GLES2 renderer (if selected)
    GLES2Renderer* gles2;
    
    // Configuration
    UnifiedRendererConfig config;
    
    // Statistics
    UnifiedStats stats;
};

// Internal texture wrapper
struct UnifiedTexture {
    RendererBackend backend;
    void* vulkan_texture;
    void* gles2_texture;
    uint32_t width;
    uint32_t height;
};

// Detect if Vulkan is available
bool unified_renderer_vulkan_available(void) {
    // Try to load libvulkan
    void* handle = dlopen("libvulkan.so.1", RTLD_LAZY);
    if (!handle) {
        return false;
    }
    
    // Check if vkEnumerateInstanceVersion is available
    PFN_vkEnumerateInstanceVersion enumerate_version = 
        (PFN_vkEnumerateInstanceVersion)dlsym(handle, "vkEnumerateInstanceVersion");
    
    if (!enumerate_version) {
        dlclose(handle);
        return false;
    }
    
    uint32_t version;
    VkResult result = enumerate_version(&version);
    dlclose(handle);
    
    if (result != VK_SUCCESS) {
        return false;
    }
    
    // Check if version is at least 1.2
    if (version < VK_API_VERSION_1_2) {
        LOG_INFO("[Renderer] Vulkan available but version %d.%d < 1.2",
                 VK_VERSION_MAJOR(version), VK_VERSION_MINOR(version));
        return false;
    }
    
    LOG_INFO("[Renderer] Vulkan %d.%d available",
             VK_VERSION_MAJOR(version), VK_VERSION_MINOR(version));
    return true;
}

// Detect if GLES2 is available
bool unified_renderer_gles2_available(void) {
    // Try to load libGLESv2
    void* handle = dlopen("libGLESv2.so.2", RTLD_LAZY);
    if (!handle) {
        return false;
    }
    
    // Check for basic GLES2 function
    void* gl_clear = dlsym(handle, "glClear");
    dlclose(handle);
    
    if (!gl_clear) {
        return false;
    }
    
    LOG_INFO("[Renderer] GLES2 available");
    return true;
}

// Detect best available backend
RendererBackend unified_renderer_detect_best_backend(void) {
    // Prefer Vulkan if available
    if (unified_renderer_vulkan_available()) {
        return RENDERER_VULKAN;
    }
    
    // Fall back to GLES2
    if (unified_renderer_gles2_available()) {
        return RENDERER_GLES2;
    }
    
    // No renderer available
    return RENDERER_AUTO;
}

// Create unified renderer
UnifiedRenderer* unified_renderer_create(void* egl, void* wlr_renderer,
                                         const UnifiedRendererConfig* config) {
    UnifiedRenderer* renderer = (UnifiedRenderer*)calloc(1, sizeof(UnifiedRenderer));
    if (!renderer) {
        LOG_ERROR("[UnifiedRenderer] Failed to allocate");
        return NULL;
    }
    
    if (config) {
        renderer->config = *config;
    }
    
    // Determine backend
    RendererBackend selected_backend = renderer->config.backend;
    
    if (selected_backend == RENDERER_AUTO) {
        selected_backend = unified_renderer_detect_best_backend();
    }
    
    LOG_INFO("[UnifiedRenderer] Selected backend: %s",
             selected_backend == RENDERER_VULKAN ? "Vulkan" :
             selected_backend == RENDERER_GLES2 ? "GLES2" : "None");
    
    renderer->backend = selected_backend;
    renderer->stats.backend = selected_backend;
    
    // Create appropriate renderer
    if (selected_backend == RENDERER_VULKAN) {
        VulkanRendererConfig vk_config = {0};
        vk_config.enableValidation = renderer->config.enableValidation;
        vk_config.enableVSync = renderer->config.enableVSync;
        vk_config.desiredImageCount = 3;
        
        renderer->vulkan = vulkan_renderer_create(&vk_config);
        if (!renderer->vulkan) {
            LOG_ERROR("[UnifiedRenderer] Failed to create Vulkan renderer");
            
            // Fall back to GLES2
            if (unified_renderer_gles2_available()) {
                LOG_INFO("[UnifiedRenderer] Falling back to GLES2");
                renderer->backend = RENDERER_GLES2;
                renderer->stats.backend = RENDERER_GLES2;
            } else {
                free(renderer);
                return NULL;
            }
        }
    }
    
    if (renderer->backend == RENDERER_GLES2) {
        GLES2RendererConfig gles_config = {0};
        gles_config.enableVSync = renderer->config.enableVSync;
        gles_config.gamma = renderer->config.gamma;
        
        renderer->gles2 = gles2_renderer_create((struct wlr_egl*)egl, 
                                                 (struct wlr_renderer*)wlr_renderer,
                                                 &gles_config);
        if (!renderer->gles2) {
            LOG_ERROR("[UnifiedRenderer] Failed to create GLES2 renderer");
            
            // If we were trying GLES2, fail
            if (selected_backend == RENDERER_GLES2) {
                if (renderer->vulkan) {
                    vulkan_renderer_destroy(renderer->vulkan);
                }
                free(renderer);
                return NULL;
            }
            
            // Otherwise fall back to Vulkan if available
            if (renderer->vulkan) {
                renderer->backend = RENDERER_VULKAN;
                renderer->stats.backend = RENDERER_VULKAN;
            } else {
                free(renderer);
                return NULL;
            }
        }
    }
    
    if (!renderer->vulkan && !renderer->gles2) {
        LOG_ERROR("[UnifiedRenderer] No renderer available");
        free(renderer);
        return NULL;
    }
    
    // Get initial stats
    unified_renderer_get_stats(renderer, &renderer->stats);
    
    LOG_INFO("[UnifiedRenderer] Initialized with %s backend",
             renderer->backend == RENDERER_VULKAN ? "Vulkan" : "GLES2");
    return renderer;
}

void unified_renderer_destroy(UnifiedRenderer* renderer) {
    if (!renderer) return;
    
    if (renderer->vulkan) {
        vulkan_renderer_destroy(renderer->vulkan);
    }
    if (renderer->gles2) {
        gles2_renderer_destroy(renderer->gles2);
    }
    
    free(renderer);
    LOG_INFO("[UnifiedRenderer] Destroyed");
}

RendererBackend unified_renderer_get_backend(UnifiedRenderer* renderer) {
    return renderer ? renderer->backend : RENDERER_AUTO;
}

const char* unified_renderer_get_backend_name(UnifiedRenderer* renderer) {
    if (!renderer) return "None";
    
    switch (renderer->backend) {
        case RENDERER_VULKAN: return "Vulkan";
        case RENDERER_GLES2: return "GLES2";
        default: return "Unknown";
    }
}

bool unified_renderer_begin_frame(UnifiedRenderer* renderer, int width, int height) {
    if (!renderer) return false;
    
    if (renderer->backend == RENDERER_VULKAN && renderer->vulkan) {
        return vulkan_renderer_begin_frame(renderer->vulkan);
    } else if (renderer->backend == RENDERER_GLES2 && renderer->gles2) {
        return gles2_renderer_begin_frame(renderer->gles2, width, height);
    }
    
    return false;
}

bool unified_renderer_end_frame(UnifiedRenderer* renderer) {
    if (!renderer) return false;
    
    if (renderer->backend == RENDERER_VULKAN && renderer->vulkan) {
        return vulkan_renderer_end_frame(renderer->vulkan);
    } else if (renderer->backend == RENDERER_GLES2 && renderer->gles2) {
        return gles2_renderer_end_frame(renderer->gles2);
    }
    
    return false;
}

void unified_renderer_set_clear_color(UnifiedRenderer* renderer, float r, float g, float b, float a) {
    if (!renderer) return;
    
    if (renderer->backend == RENDERER_VULKAN && renderer->vulkan) {
        vulkan_renderer_set_clear_color(renderer->vulkan, r, g, b, a);
    } else if (renderer->backend == RENDERER_GLES2 && renderer->gles2) {
        gles2_renderer_set_clear_color(renderer->gles2, r, g, b, a);
    }
}

UnifiedTexture* unified_renderer_create_texture(UnifiedRenderer* renderer, uint32_t width, uint32_t height) {
    if (!renderer) return NULL;
    
    UnifiedTexture* texture = (UnifiedTexture*)calloc(1, sizeof(UnifiedTexture));
    if (!texture) return NULL;
    
    texture->width = width;
    texture->height = height;
    
    if (renderer->backend == RENDERER_VULKAN && renderer->vulkan) {
        texture->backend = RENDERER_VULKAN;
        texture->vulkan_texture = vulkan_renderer_create_texture_from_buffer(renderer->vulkan, NULL);
    } else if (renderer->backend == RENDERER_GLES2 && renderer->gles2) {
        texture->backend = RENDERER_GLES2;
        texture->gles2_texture = gles2_renderer_create_texture(renderer->gles2, width, height);
    }
    
    return texture;
}

void unified_renderer_destroy_texture(UnifiedRenderer* renderer, UnifiedTexture* texture) {
    if (!renderer || !texture) return;
    
    if (texture->backend == RENDERER_VULKAN && renderer->vulkan) {
        vulkan_renderer_destroy_texture(renderer->vulkan, texture->vulkan_texture);
    } else if (texture->backend == RENDERER_GLES2 && renderer->gles2) {
        gles2_renderer_destroy_texture(renderer->gles2, texture->gles2_texture);
    }
    
    free(texture);
}

void unified_renderer_bind_texture(UnifiedRenderer* renderer, UnifiedTexture* texture) {
    if (!renderer || !texture) return;
    
    if (texture->backend == RENDERER_VULKAN && renderer->vulkan) {
        // Vulkan texture binding would go here
    } else if (texture->backend == RENDERER_GLES2 && renderer->gles2) {
        gles2_renderer_bind_texture(renderer->gles2, texture->gles2_texture);
    }
}

void unified_renderer_draw_quad(UnifiedRenderer* renderer, float x, float y, float w, float h) {
    if (!renderer) return;
    
    if (renderer->backend == RENDERER_VULKAN && renderer->vulkan) {
        vulkan_renderer_draw_quad(renderer->vulkan, x, y, w, h);
    } else if (renderer->backend == RENDERER_GLES2 && renderer->gles2) {
        gles2_renderer_draw_quad(renderer->gles2, x, y, w, h);
    }
}

void unified_renderer_set_gamma(UnifiedRenderer* renderer, float gamma) {
    if (!renderer) return;
    
    if (renderer->backend == RENDERER_VULKAN && renderer->vulkan) {
        // Vulkan scene compositor would handle this
    } else if (renderer->backend == RENDERER_GLES2 && renderer->gles2) {
        gles2_renderer_set_gamma(renderer->gles2, gamma);
    }
}

void unified_renderer_set_brightness(UnifiedRenderer* renderer, float brightness) {
    if (!renderer) return;
    
    if (renderer->backend == RENDERER_VULKAN && renderer->vulkan) {
        // Vulkan scene compositor would handle this
    } else if (renderer->backend == RENDERER_GLES2 && renderer->gles2) {
        gles2_renderer_set_brightness(renderer->gles2, brightness);
    }
}

void unified_renderer_get_stats(UnifiedRenderer* renderer, UnifiedStats* stats) {
    if (!renderer || !stats) return;
    
    stats->backend = renderer->backend;
    
    if (renderer->backend == RENDERER_VULKAN && renderer->vulkan) {
        // Would get Vulkan stats
        stats->driverVersion = vulkan_renderer_get_gpu_info(renderer->vulkan);
    } else if (renderer->backend == RENDERER_GLES2 && renderer->gles2) {
        GLES2Stats gles_stats;
        gles2_renderer_get_stats(renderer->gles2, &gles_stats);
        stats->fps = gles_stats.fps;
        stats->textureCount = gles_stats.textureCount;
        stats->gpuMemoryUsed = gles_stats.gpuMemoryUsed;
        stats->driverVersion = gles_stats.glVersion;
    }
}
