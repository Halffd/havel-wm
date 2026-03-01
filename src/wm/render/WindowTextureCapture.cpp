#include "WindowTextureCapture.hpp"
#include <cstdio>
#include <cstdint>

namespace havel {

WindowTextureCapture::WindowTextureCapture() = default;

WindowTextureCapture::~WindowTextureCapture() {
    release();
}

void WindowTextureCapture::release() {
    if (m_textureId != 0) {
        glDeleteTextures(1, &m_textureId);
        m_textureId = 0;
    }
    m_width = 0;
    m_height = 0;
}

bool WindowTextureCapture::capture(struct wlr_scene_surface* sceneSurface) {
    // Full implementation would:
    // 1. Get wlr_surface from scene_surface
    // 2. Access surface buffer
    // 3. Render to FBO
    // 4. Read pixels and upload to texture
    
    // For now, create a placeholder texture
    // This demonstrates the architecture; full implementation
    // requires deeper wlroots integration
    
    (void)sceneSurface;  // Unused in placeholder
    
    // Create placeholder texture if not exists
    if (m_textureId == 0) {
        const int width = 100;
        const int height = 75;
        
        glGenTextures(1, &m_textureId);
        glBindTexture(GL_TEXTURE_2D, m_textureId);
        
        // Set texture parameters
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        
        m_width = width;
        m_height = height;
        
        // Create placeholder texture (blue gradient)
        uint32_t* pixels = new uint32_t[width * height];
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                // Gradient from light blue to dark blue
                uint8_t r = 50 + (y * 50 / height);
                uint8_t g = 100 + (y * 50 / height);
                uint8_t b = 150 + (y * 50 / height);
                pixels[y * width + x] = (255 << 24) | (b << 16) | (g << 8) | r;
            }
        }
        
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, 
                     GL_RGBA, GL_UNSIGNED_BYTE, pixels);
        glBindTexture(GL_TEXTURE_2D, 0);
        
        delete[] pixels;
        
        printf("[WindowTextureCapture] Created placeholder texture %dx%d\n", width, height);
    }
    
    return isValid();
}

bool WindowTextureCapture::createTexture(int width, int height, const void* data) {
    glGenTextures(1, &m_textureId);
    glBindTexture(GL_TEXTURE_2D, m_textureId);
    
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, data);
    
    glBindTexture(GL_TEXTURE_2D, 0);
    
    m_width = width;
    m_height = height;
    
    return true;
}

bool WindowTextureCapture::renderSurfaceToFBO(struct wlr_scene_surface* sceneSurface) {
    // Stub - full implementation would require:
    // - Access to wlr_renderer
    // - FBO creation and binding
    // - Surface rendering via wlroots APIs
    // - Pixel readback
    (void)sceneSurface;
    return false;
}

} // namespace havel
