// Renderer Integration Implementation

#include "CompositorRenderer.h"
#include "UnifiedRenderer.h"
#include "DamageTracker.h"
#include "MultiGPU.h"
#include "WlrBinding.h"
#include "VulkanRendererBridge.h"
#include <Logger.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <wlr/types/wlr_output.h>

// Per-output state
typedef struct {
    struct wlr_output* output;
    DamageTracker* damageTracker;
    RendererStats stats;
    bool needsFullRedraw;
    struct timespec lastFrameTime;
} OutputState;

// Internal compositor renderer structure
struct CompositorRenderer {
    struct wlr_scene* scene;
    RendererConfig config;
    
    // Unified renderer (Vulkan or GLES2)
    UnifiedRenderer* unifiedRenderer;
    
    // Multi-GPU manager
    MultiGPUManager* gpuManager;
    
    // Output states
    OutputState* outputs;
    uint32_t outputCount;
    uint32_t outputCapacity;
    
    // Frame callback
    compositor_frame_callback frameCallback;
    void* frameCallbackUserData;
    
    // Global statistics
    uint32_t totalFrames;
    uint32_t droppedFrames;
};

CompositorRenderer* compositor_renderer_create(struct wlr_scene* scene, 
                                                const RendererConfig* config) {
    if (!scene) {
        LOG_ERROR("[CompositorRenderer] Invalid scene");
        return NULL;
    }
    
    CompositorRenderer* renderer = (CompositorRenderer*)calloc(1, sizeof(CompositorRenderer));
    if (!renderer) {
        return NULL;
    }
    
    renderer->scene = scene;
    
    if (config) {
        renderer->config = *config;
    } else {
        // Default config
        renderer->config.useVulkan = true;
        renderer->config.enableVSync = true;
        renderer->config.enableDamageTracking = true;
        renderer->config.enableMultiGPU = true;
        renderer->config.targetFrameRate = 0;
        renderer->config.maxFrameLatency = 1;
        renderer->config.gamma = 1.0f;
        renderer->config.brightness = 1.0f;
        renderer->config.enableBlur = false;
    }
    
    // Create unified renderer
    UnifiedRendererConfig unified_config = {0};
    unified_config.backend = renderer->config.useVulkan ? RENDERER_VULKAN : RENDERER_GLES2;
    unified_config.enableValidation = false;
    unified_config.enableVSync = renderer->config.enableVSync;
    
    renderer->unifiedRenderer = unified_renderer_create(NULL, NULL, &unified_config);
    if (!renderer->unifiedRenderer) {
        LOG_ERROR("[CompositorRenderer] Failed to create unified renderer");
        free(renderer);
        return NULL;
    }
    
    // Create multi-GPU manager
    if (renderer->config.enableMultiGPU) {
        renderer->gpuManager = multigpu_manager_create();
        if (renderer->gpuManager) {
            LOG_INFO("[CompositorRenderer] Multi-GPU enabled (%u GPUs)",
                    multigpu_get_count(renderer->gpuManager));
        }
    }
    
    renderer->outputs = NULL;
    renderer->outputCount = 0;
    renderer->outputCapacity = 0;
    
    renderer->frameCallback = NULL;
    renderer->frameCallbackUserData = NULL;
    
    renderer->totalFrames = 0;
    renderer->droppedFrames = 0;
    
    LOG_INFO("[CompositorRenderer] Created (Vulkan=%d, VSync=%d, Damage=%d)",
             renderer->config.useVulkan,
             renderer->config.enableVSync,
             renderer->config.enableDamageTracking);
    
    return renderer;
}

void compositor_renderer_destroy(CompositorRenderer* renderer) {
    if (!renderer) return;
    
    // Destroy all output states
    for (uint32_t i = 0; i < renderer->outputCount; i++) {
        if (renderer->outputs[i].damageTracker) {
            damage_tracker_destroy(renderer->outputs[i].damageTracker);
        }
    }
    free(renderer->outputs);
    
    if (renderer->unifiedRenderer) {
        unified_renderer_destroy(renderer->unifiedRenderer);
    }
    
    if (renderer->gpuManager) {
        multigpu_manager_destroy(renderer->gpuManager);
    }
    
    LOG_INFO("[CompositorRenderer] Destroyed");
    free(renderer);
}

bool compositor_renderer_add_output(CompositorRenderer* renderer, 
                                    struct wlr_output* output) {
    if (!renderer || !output) return false;
    
    // Check if output already exists
    for (uint32_t i = 0; i < renderer->outputCount; i++) {
        if (renderer->outputs[i].output == output) {
            return false;  // Already added
        }
    }
    
    // Expand array if needed
    if (renderer->outputCount >= renderer->outputCapacity) {
        renderer->outputCapacity = renderer->outputCapacity ? 
                                   renderer->outputCapacity * 2 : 4;
        renderer->outputs = (OutputState*)realloc(renderer->outputs, 
                          renderer->outputCapacity * sizeof(OutputState));
    }
    
    OutputState* state = &renderer->outputs[renderer->outputCount];
    memset(state, 0, sizeof(OutputState));
    state->output = output;
    state->needsFullRedraw = true;  // First frame always full redraw
    
    // Create damage tracker for this output
    if (renderer->config.enableDamageTracking) {
        state->damageTracker = damage_tracker_create(output->width, output->height);
        damage_tracker_set_max_age(state->damageTracker, 60);
    }
    
    clock_gettime(CLOCK_MONOTONIC, &state->lastFrameTime);
    
    renderer->outputCount++;
    
    LOG_INFO("[CompositorRenderer] Added output: %s (%dx%d)",
             output->name, output->width, output->height);
    
    return true;
}

void compositor_renderer_remove_output(CompositorRenderer* renderer,
                                       struct wlr_output* output) {
    if (!renderer || !output) return;
    
    for (uint32_t i = 0; i < renderer->outputCount; i++) {
        if (renderer->outputs[i].output == output) {
            // Destroy damage tracker
            if (renderer->outputs[i].damageTracker) {
                damage_tracker_destroy(renderer->outputs[i].damageTracker);
            }
            
            // Shift remaining outputs
            for (uint32_t j = i; j < renderer->outputCount - 1; j++) {
                renderer->outputs[j] = renderer->outputs[j + 1];
            }
            renderer->outputCount--;
            
            LOG_INFO("[CompositorRenderer] Removed output: %s", output->name);
            return;
        }
    }
}

bool compositor_renderer_render_output(CompositorRenderer* renderer,
                                       struct wlr_output* output,
                                       struct wlr_scene_output* scene_output) {
    if (!renderer || !output || !scene_output) return false;
    
    // Find output state
    OutputState* state = NULL;
    for (uint32_t i = 0; i < renderer->outputCount; i++) {
        if (renderer->outputs[i].output == output) {
            state = &renderer->outputs[i];
            break;
        }
    }
    
    if (!state) {
        LOG_ERROR("[CompositorRenderer] Output not found: %s", output->name);
        return false;
    }
    
    // Calculate frame time
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    uint32_t frame_time_ms = (now.tv_sec - state->lastFrameTime.tv_sec) * 1000 +
                            (now.tv_nsec - state->lastFrameTime.tv_nsec) / 1000000;
    state->lastFrameTime = now;
    
    // Check if full redraw needed
    bool fullRedraw = state->needsFullRedraw;
    if (!fullRedraw && state->damageTracker) {
        fullRedraw = damage_tracker_needs_full_redraw(state->damageTracker);
    }
    
    // Begin frame
    if (!unified_renderer_begin_frame(renderer->unifiedRenderer, output->width, output->height)) {
        return false;
    }
    
    // Render scene
    if (fullRedraw) {
        // Full screen render
        if (state->damageTracker) {
            damage_tracker_add_full_damage(state->damageTracker);
        }
        
        // Use wlroots scene renderer for full render
        WlrSceneRenderer* scene_renderer = wlr_scene_renderer_create(
            renderer->unifiedRenderer, renderer->scene);
        
        if (scene_renderer) {
            wlr_scene_renderer_render(scene_renderer, output, scene_output);
            wlr_scene_renderer_destroy(scene_renderer);
        }
        
        state->needsFullRedraw = false;
    } else {
        // Partial render using damage regions
        uint32_t damage_count;
        const DamageRegion* damage_regions = damage_tracker_get_regions(
            state->damageTracker, &damage_count);
        
        if (damage_regions && damage_count > 0) {
            // Render each damaged region
            for (uint32_t i = 0; i < damage_count; i++) {
                const DamageRegion* region = &damage_regions[i];
                
                // Set scissor to damage region
                // (would use vkCmdSetScissor in Vulkan)
                
                // Render scene for this region
                WlrSceneRenderer* scene_renderer = wlr_scene_renderer_create(
                    renderer->unifiedRenderer, renderer->scene);
                
                if (scene_renderer) {
                    wlr_scene_renderer_render(scene_renderer, output, scene_output);
                    wlr_scene_renderer_destroy(scene_renderer);
                }
            }
        }
    }
    
    // End frame
    if (!unified_renderer_end_frame(renderer->unifiedRenderer)) {
        return false;
    }
    
    // Clear damage
    if (state->damageTracker) {
        damage_tracker_clear(state->damageTracker);
    }
    
    // Update statistics
    state->stats.fps = frame_time_ms > 0 ? 1000.0f / frame_time_ms : 60.0f;
    state->stats.frameTimeMs = frame_time_ms;
    state->stats.vsyncEnabled = unified_renderer_is_vsync_enabled(renderer->unifiedRenderer);
    
    if (state->damageTracker) {
        DamageStats damage_stats;
        damage_tracker_get_stats(state->damageTracker, &damage_stats);
        state->stats.damagedArea = damage_stats.damagedArea;
        state->stats.damageRatio = damage_stats.damageRatio;
    }
    
    if (renderer->gpuManager) {
        state->stats.isMultiGPU = multigpu_is_available(renderer->gpuManager);
        GPUInfo* gpu = multigpu_get_active_gpu(renderer->gpuManager);
        state->stats.activeGPU = gpu ? gpu->index : 0;
    }
    
    // Call frame callback
    if (renderer->frameCallback) {
        renderer->frameCallback(renderer->frameCallbackUserData, frame_time_ms);
    }
    
    renderer->totalFrames++;
    
    return true;
}

void compositor_renderer_add_damage(CompositorRenderer* renderer,
                                    struct wlr_output* output,
                                    int x, int y, int width, int height) {
    if (!renderer || !output) return;
    
    for (uint32_t i = 0; i < renderer->outputCount; i++) {
        if (renderer->outputs[i].output == output) {
            if (renderer->outputs[i].damageTracker) {
                damage_tracker_add_damage(renderer->outputs[i].damageTracker, x, y, width, height);
            }
            return;
        }
    }
}

void compositor_renderer_add_full_damage(CompositorRenderer* renderer,
                                         struct wlr_output* output) {
    if (!renderer || !output) return;
    
    for (uint32_t i = 0; i < renderer->outputCount; i++) {
        if (renderer->outputs[i].output == output) {
            renderer->outputs[i].needsFullRedraw = true;
            if (renderer->outputs[i].damageTracker) {
                damage_tracker_add_full_damage(renderer->outputs[i].damageTracker);
            }
            return;
        }
    }
}

void compositor_renderer_set_vsync(CompositorRenderer* renderer, bool enabled) {
    if (!renderer) return;
    renderer->config.enableVSync = enabled;
    unified_renderer_set_vsync_enabled(renderer->unifiedRenderer, enabled);
    LOG_INFO("[CompositorRenderer] VSync %s", enabled ? "enabled" : "disabled");
}

bool compositor_renderer_get_vsync(CompositorRenderer* renderer) {
    return renderer ? unified_renderer_is_vsync_enabled(renderer->unifiedRenderer) : false;
}

void compositor_renderer_set_gamma(CompositorRenderer* renderer, float gamma) {
    if (!renderer) return;
    renderer->config.gamma = gamma;
    // Would apply to scene renderer
}

void compositor_renderer_set_brightness(CompositorRenderer* renderer, float brightness) {
    if (!renderer) return;
    renderer->config.brightness = brightness;
}

void compositor_renderer_set_blur(CompositorRenderer* renderer, bool enabled) {
    if (!renderer) return;
    renderer->config.enableBlur = enabled;
}

void compositor_renderer_get_stats(CompositorRenderer* renderer, 
                                   struct wlr_output* output,
                                   RendererStats* stats) {
    if (!renderer || !stats || !output) return;
    
    for (uint32_t i = 0; i < renderer->outputCount; i++) {
        if (renderer->outputs[i].output == output) {
            *stats = renderer->outputs[i].stats;
            return;
        }
    }
}

void compositor_renderer_set_frame_callback(CompositorRenderer* renderer,
                                            compositor_frame_callback callback,
                                            void* user_data) {
    if (!renderer) return;
    renderer->frameCallback = callback;
    renderer->frameCallbackUserData = user_data;
}
