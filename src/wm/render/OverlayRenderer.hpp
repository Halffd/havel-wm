#pragma once

#include <wm/Types.hpp>
#include <GLES2/gl2.h>
#include <cstdint>
#include <string>

namespace havel {

/**
 * Float-based rectangle for rendering
 */
struct FloatRect {
    float x, y, w, h;
    
    constexpr FloatRect(float x = 0, float y = 0, float w = 0, float h = 0)
        : x(x), y(y), w(w), h(h) {}
    
    // Convert from int Rect
    constexpr FloatRect(const Rect& r) : x((float)r.x), y((float)r.y), w((float)r.w), h((float)r.h) {}
};

/**
 * GLES2 Overlay Renderer
 * 
 * Provides basic 2D rendering on top of wlroots scene.
 * Used for:
 * - Alt-Tab overlay
 * - Workspace overview
 * - App launcher UI
 * - Notifications
 * - Debug overlays
 * 
 * Rendering order:
 * 1. wlroots renders scene
 * 2. OverlayRenderer draws on top
 * 3. Buffers are swapped
 */
class OverlayRenderer {
public:
    OverlayRenderer();
    ~OverlayRenderer();
    
    // Initialize with OpenGL context (called after context is current)
    bool initialize();
    void shutdown();
    
    // Begin/end frame rendering
    void beginFrame(int screenWidth, int screenHeight);
    void endFrame();
    
    // Primitive drawing
    void drawRect(const FloatRect& rect, const Color& color);
    void drawRect(float x, float y, float w, float h, const Color& color);
    void drawBorder(const FloatRect& rect, const Color& color, float thickness = 2.0f);
    void drawCircle(float cx, float cy, float radius, const Color& color);
    
    // Texture drawing
    void drawTexture(GLuint texture, const FloatRect& rect, float alpha = 1.0f);
    void drawTexture(GLuint texture, float x, float y, float w, float h, float alpha = 1.0f);
    
    // Text rendering (bitmap font)
    void drawText(const char* text, float x, float y, float size, const Color& color);
    void drawTextCentered(const char* text, float cx, float cy, float size, const Color& color);
    
    // State
    bool isInitialized() const { return m_initialized; }
    int getScreenWidth() const { return m_screenWidth; }
    int getScreenHeight() const { return m_screenHeight; }
    
    // Shader programs
    GLuint getColorShader() const { return m_colorShader; }
    GLuint getTextureShader() const { return m_textureShader; }
    
private:
    bool compileShaders();
    void createGeometry();
    void cleanup();
    
    bool m_initialized = false;
    int m_screenWidth = 0;
    int m_screenHeight = 0;
    
    // Shader programs
    GLuint m_colorShader = 0;    // For solid color rectangles
    GLuint m_textureShader = 0;  // For textured quads
    
    // Vertex arrays
    GLuint m_vao = 0;
    GLuint m_vbo = 0;
    
    // Texture for bitmap font (loaded on demand)
    GLuint m_fontTexture = 0;
    bool m_fontLoaded = false;
};

} // namespace havel
