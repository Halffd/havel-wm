#pragma once

#include <wlr/types/wlr_surface.h>
#include <wlr/render/wlr_renderer.h>

namespace havel {

/**
 * Get OpenGL texture ID from a wlr_surface
 * Returns 0 if surface has no buffer or texture is invalid
 */
static inline GLuint get_surface_texture_id(struct wlr_surface* surface) {
    if (!surface) {
        return 0;
    }
    
    if (!wlr_surface_has_buffer(surface)) {
        return 0;
    }
    
    struct wlr_texture* texture = wlr_surface_get_texture(surface);
    if (!texture) {
        return 0;
    }
    
    // wlr_texture is a wrapper around OpenGL texture
    // For GLES2, we can get the texture ID directly
    return (GLuint)(uintptr_t)texture;
}

/**
 * Check if surface has valid texture for rendering
 */
static inline bool surface_has_valid_texture(struct wlr_surface* surface) {
    return surface && wlr_surface_has_buffer(surface) && wlr_surface_get_texture(surface);
}

} // namespace havel
