// Renderer Integration - Connects compositor to new rendering features

#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Forward declarations
struct wlr_output;
struct wlr_scene;
struct wlr_scene_output;

// Renderer integration handle
typedef struct CompositorRenderer CompositorRenderer;

// Renderer configuration
typedef struct {
    bool useVulkan;           // Use Vulkan if available
    bool enableVSync;         // Enable VSync
    bool enableDamageTracking; // Enable partial updates
    bool enableMultiGPU;      // Enable multi-GPU support
    uint32_t targetFrameRate; // 0 = match display
    uint32_t maxFrameLatency; // 1-3
    float gamma;
    float brightness;
    bool enableBlur;
} RendererConfig;

// Create/destroy compositor renderer
CompositorRenderer* compositor_renderer_create(struct wlr_scene* scene, 
                                                const RendererConfig* config);
void compositor_renderer_destroy(CompositorRenderer* renderer);

// Output management
bool compositor_renderer_add_output(CompositorRenderer* renderer, 
                                    struct wlr_output* output);
void compositor_renderer_remove_output(CompositorRenderer* renderer,
                                       struct wlr_output* output);

// Render frame to output
bool compositor_renderer_render_output(CompositorRenderer* renderer,
                                       struct wlr_output* output,
                                       struct wlr_scene_output* scene_output);

// Damage tracking integration
void compositor_renderer_add_damage(CompositorRenderer* renderer,
                                    struct wlr_output* output,
                                    int x, int y, int width, int height);
void compositor_renderer_add_full_damage(CompositorRenderer* renderer,
                                         struct wlr_output* output);

// VSync control
void compositor_renderer_set_vsync(CompositorRenderer* renderer, bool enabled);
bool compositor_renderer_get_vsync(CompositorRenderer* renderer);

// Post-processing
void compositor_renderer_set_gamma(CompositorRenderer* renderer, float gamma);
void compositor_renderer_set_brightness(CompositorRenderer* renderer, float brightness);
void compositor_renderer_set_blur(CompositorRenderer* renderer, bool enabled);

// Statistics
typedef struct {
    float fps;
    float frameTimeMs;
    uint32_t damagedArea;
    float damageRatio;
    bool vsyncEnabled;
    uint32_t activeGPU;
    bool isMultiGPU;
} RendererStats;

void compositor_renderer_get_stats(CompositorRenderer* renderer, 
                                   struct wlr_output* output,
                                   RendererStats* stats);

// Frame callback
typedef void (*compositor_frame_callback)(void* user_data, uint32_t frame_time_ms);
void compositor_renderer_set_frame_callback(CompositorRenderer* renderer,
                                            compositor_frame_callback callback,
                                            void* user_data);

#ifdef __cplusplus
}
#endif
