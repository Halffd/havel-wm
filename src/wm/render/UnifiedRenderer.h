// Unified Renderer - Auto-selects Vulkan or GLES2

#pragma once

#include "VulkanRendererBridge.h"
#include "GLES2Renderer.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Renderer backend type
typedef enum {
    RENDERER_AUTO = 0,
    RENDERER_VULKAN,
    RENDERER_GLES2
} RendererBackend;

// Unified renderer configuration
typedef struct {
    RendererBackend backend;
    bool enableValidation;
    bool enableVSync;
    float gamma;
} UnifiedRendererConfig;

// Unified renderer handle
typedef struct UnifiedRenderer UnifiedRenderer;

// Create/destroy unified renderer
UnifiedRenderer* unified_renderer_create(void* egl, void* wlr_renderer,
                                         const UnifiedRendererConfig* config);
void unified_renderer_destroy(UnifiedRenderer* renderer);

// Get selected backend
RendererBackend unified_renderer_get_backend(UnifiedRenderer* renderer);
const char* unified_renderer_get_backend_name(UnifiedRenderer* renderer);

// Rendering (automatically uses correct backend)
bool unified_renderer_begin_frame(UnifiedRenderer* renderer, int width, int height);
bool unified_renderer_end_frame(UnifiedRenderer* renderer);
void unified_renderer_set_clear_color(UnifiedRenderer* renderer, float r, float g, float b, float a);

// VSync control
void unified_renderer_set_vsync_enabled(UnifiedRenderer* renderer, bool enabled);
bool unified_renderer_is_vsync_enabled(UnifiedRenderer* renderer);

// Texture operations
typedef struct UnifiedTexture UnifiedTexture;

UnifiedTexture* unified_renderer_create_texture(UnifiedRenderer* renderer, uint32_t width, uint32_t height);
void unified_renderer_destroy_texture(UnifiedRenderer* renderer, UnifiedTexture* texture);
void unified_renderer_bind_texture(UnifiedRenderer* renderer, UnifiedTexture* texture);
void unified_renderer_draw_quad(UnifiedRenderer* renderer, float x, float y, float w, float h);

// Post-processing
void unified_renderer_set_gamma(UnifiedRenderer* renderer, float gamma);
void unified_renderer_set_brightness(UnifiedRenderer* renderer, float brightness);

// Statistics
typedef struct {
    RendererBackend backend;
    float fps;
    uint32_t textureCount;
    size_t gpuMemoryUsed;
    const char* driverVersion;
} UnifiedStats;

void unified_renderer_get_stats(UnifiedRenderer* renderer, UnifiedStats* stats);

// Backend detection
bool unified_renderer_vulkan_available(void);
bool unified_renderer_gles2_available(void);
RendererBackend unified_renderer_detect_best_backend(void);

#ifdef __cplusplus
}
#endif
