#pragma once

#include <GLES2/gl2.h>
#include <cstdint>

// Forward declaration - avoid including wlroots headers in C++
struct wlr_scene_surface;

namespace havel {

/**
 * Window texture capture for Alt-Tab thumbnails
 * Captures window surface and creates OpenGL texture
 */
class WindowTextureCapture {
public:
    WindowTextureCapture();
    ~WindowTextureCapture();
    
    // Capture window surface and create/update texture
    bool capture(struct wlr_scene_surface* sceneSurface);
    
    // Get the OpenGL texture ID (0 if not valid)
    GLuint getTextureId() const { return m_textureId; }
    
    // Get texture dimensions
    int getWidth() const { return m_width; }
    int getHeight() const { return m_height; }
    
    // Check if texture is valid
    bool isValid() const { return m_textureId != 0 && m_width > 0 && m_height > 0; }
    
    // Release texture resources
    void release();

private:
    GLuint m_textureId = 0;
    int m_width = 0;
    int m_height = 0;
    
    // Create OpenGL texture
    bool createTexture(int width, int height, const void* data);
    
    // Render surface to texture via FBO
    bool renderSurfaceToFBO(struct wlr_scene_surface* sceneSurface);
};

} // namespace havel
