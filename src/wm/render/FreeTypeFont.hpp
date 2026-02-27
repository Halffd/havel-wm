#pragma once

#include <GLES2/gl2.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#include <string>
#include <vector>
#include <array>
#include <memory>

namespace havel {

/**
 * Glyph metrics for FreeType font
 */
struct Glyph {
    GLuint textureID = 0;     // Glyph texture
    int width = 0;            // Glyph width in pixels
    int height = 0;           // Glyph height in pixels
    int bearingX = 0;         // Offset from baseline to left
    int bearingY = 0;         // Offset from baseline to top
    unsigned int advance = 0; // Distance to next glyph (in 1/64 pixels)
};

/**
 * FreeType bitmap font renderer
 * 
 * Loads TrueType fonts dynamically using FreeType.
 * Generates glyph textures on-demand.
 * Supports variable-width fonts.
 * 
 * Note: Kerning not yet implemented (FT_HAS_KERNING check missing)
 */
class FreeTypeFont {
public:
    FreeTypeFont();
    ~FreeTypeFont();
    
    // Load font from TTF file
    bool loadFont(const char* fontPath, unsigned int fontSize);
    void shutdown();
    
    // Set projection matrix (call when screen size changes)
    void setProjection(int screenWidth, int screenHeight);
    
    // Render text at screen coordinates
    void renderText(float x, float y, float scale, const char* text, 
                    float r = 1.0f, float g = 1.0f, float b = 1.0f, float a = 1.0f);
    
    // Get text dimensions
    float getTextWidth(const char* text, float scale = 1.0f);
    float getTextHeight(float scale = 1.0f);
    
    // Check if initialized
    bool isInitialized() const { return m_initialized; }
    
    // Get/set font path for reloading
    const char* getFontPath() const { return m_fontPath.c_str(); }
    unsigned int getFontSize() const { return m_fontSize; }
    
private:
    bool loadGlyph(unsigned char c);
    bool createShaderProgram();
    
    bool m_initialized = false;
    std::string m_fontPath;
    unsigned int m_fontSize = 0;
    
    // FreeType state
    FT_Library m_ftLibrary = nullptr;
    FT_Face m_ftFace = nullptr;
    
    // Glyph cache for ASCII 0-127
    std::array<std::unique_ptr<Glyph>, 128> m_glyphs;
    
    // VBO for rendering quads
    GLuint m_vbo = 0;
    GLuint m_vao = 0;
    
    // Shader program
    GLuint m_shaderProgram = 0;
    
    // Projection matrix uniform location
    GLint m_projectionLoc = -1;
    
    // Screen dimensions for projection
    int m_screenWidth = 1920;
    int m_screenHeight = 1080;
};

} // namespace havel
