#pragma once

#include <wlr/types/wlr_surface.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/render/gles2.h>
#include <GLES2/gl2.h>

namespace havel {

/**
 * Get OpenGL texture ID from a wlr_surface
 * Returns 0 if surface has no buffer or texture is invalid
 * 
 * Uses wlr_gles2_texture_get_attribs() to properly extract
 * the GL texture ID from the wlroots texture wrapper.
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

    // Properly extract GL texture ID using wlroots GLES2 interface
    // Casting the pointer directly is WRONG - wlr_texture is a struct wrapper
    struct wlr_gles2_texture_attribs attribs;
    if (!wlr_gles2_texture_get_attribs(texture, &attribs)) {
        return 0;
    }

    return (GLuint)attribs.tex;
}

/**
 * Check if surface has valid texture for rendering
 */
static inline bool surface_has_valid_texture(struct wlr_surface* surface) {
    return surface && wlr_surface_has_buffer(surface) && wlr_surface_get_texture(surface);
}

} // namespace havel
