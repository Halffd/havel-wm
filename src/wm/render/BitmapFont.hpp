#pragma once

#include <GLES2/gl2.h>
#include <string>
#include <vector>
#include <array>

namespace havel {

/**
 * Glyph metrics for bitmap font
 */
struct Glyph {
    uint32_t textureX = 0;  // X position in atlas
    uint32_t textureY = 0;  // Y position in atlas
    uint32_t width = 0;     // Glyph width in pixels
    uint32_t height = 0;    // Glyph height in pixels
    int32_t bearingX = 0;   // Offset from baseline to left
    int32_t bearingY = 0;   // Offset from baseline to top
    uint32_t advance = 0;   // Distance to next glyph
};

/**
 * Bitmap font renderer
 * 
 * Uses a pre-generated font atlas texture.
 * Simple, fast, no external dependencies.
 */
class BitmapFont {
public:
    BitmapFont();
    ~BitmapFont();
    
    // Initialize with font atlas texture
    bool initialize(GLuint textureAtlas, int glyphWidth, int glyphHeight);
    void shutdown();
    
    // Render text at screen coordinates
    void renderText(float x, float y, float scale, const char* text, 
                    float r = 1.0f, float g = 1.0f, float b = 1.0f, float a = 1.0f);
    
    // Get text dimensions
    float getTextWidth(const char* text, float scale = 1.0f);
    float getTextHeight(float scale = 1.0f);
    
    // Check if initialized
    bool isInitialized() const { return m_initialized; }
    
private:
    bool loadGlyphMetrics();
    
    bool m_initialized = false;
    GLuint m_textureAtlas = 0;
    int m_glyphWidth = 0;
    int m_glyphHeight = 0;
    
    // Glyph metrics for ASCII 0-127
    std::array<Glyph, 128> m_glyphs;
    
    // VBO for rendering quads
    GLuint m_vbo = 0;
    GLuint m_vao = 0;
    
    // Shader program
    GLuint m_shaderProgram = 0;
};

} // namespace havel
