// GLES2 Renderer - Fallback for systems without Vulkan

#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Forward declarations
struct wlr_egl;
struct wlr_renderer;

// GLES2 renderer configuration
typedef struct {
    bool enableVSync;
    bool enableMSAA;
    int msaaSamples;
    float gamma;
    bool enableDebug;
} GLES2RendererConfig;

// GLES2 renderer handle
typedef struct GLES2Renderer GLES2Renderer;

// Create/destroy GLES2 renderer
GLES2Renderer* gles2_renderer_create(struct wlr_egl* egl, struct wlr_renderer* wlr_renderer,
                                     const GLES2RendererConfig* config);
void gles2_renderer_destroy(GLES2Renderer* renderer);

// Rendering
bool gles2_renderer_begin_frame(GLES2Renderer* renderer, int width, int height);
bool gles2_renderer_end_frame(GLES2Renderer* renderer);
void gles2_renderer_set_clear_color(GLES2Renderer* renderer, float r, float g, float b, float a);

// Texture operations
typedef struct GLES2Texture GLES2Texture;

GLES2Texture* gles2_renderer_create_texture(GLES2Renderer* renderer, uint32_t width, uint32_t height);
GLES2Texture* gles2_renderer_import_wlr_texture(GLES2Renderer* renderer, void* wlr_texture);
void gles2_renderer_destroy_texture(GLES2Renderer* renderer, GLES2Texture* texture);

void gles2_renderer_bind_texture(GLES2Renderer* renderer, GLES2Texture* texture);
void gles2_renderer_draw_quad(GLES2Renderer* renderer, float x, float y, float w, float h);

// Post-processing
void gles2_renderer_set_gamma(GLES2Renderer* renderer, float gamma);
void gles2_renderer_set_brightness(GLES2Renderer* renderer, float brightness);

// Statistics
typedef struct {
    float fps;
    uint32_t textureCount;
    size_t gpuMemoryUsed;
    const char* glVersion;
    const char* glVendor;
    const char* glRenderer;
} GLES2Stats;

void gles2_renderer_get_stats(GLES2Renderer* renderer, GLES2Stats* stats);

// Check if GLES2 is available
bool gles2_renderer_is_available(void);

#ifdef __cplusplus
}
#endif
