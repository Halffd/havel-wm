#include "BitmapFont.hpp"
#include <GLES2/gl2ext.h>
#include <EGL/egl.h>
#include <cstdio>
#include <cstring>

// GLES2 compatibility - vertex arrays are an extension
static PFNGLBINDVERTEXARRAYOESPROC glBindVertexArrayOES_ptr = nullptr;
static PFNGLGENVERTEXARRAYSOESPROC glGenVertexArraysOES_ptr = nullptr;
static PFNGLDELETEVERTEXARRAYSOESPROC glDeleteVertexArraysOES_ptr = nullptr;

static void initVertexArrays() {
    static bool initialized = false;
    if (initialized) return;
    
    glBindVertexArrayOES_ptr = (PFNGLBINDVERTEXARRAYOESPROC)eglGetProcAddress("glBindVertexArrayOES");
    glGenVertexArraysOES_ptr = (PFNGLGENVERTEXARRAYSOESPROC)eglGetProcAddress("glGenVertexArraysOES");
    glDeleteVertexArraysOES_ptr = (PFNGLDELETEVERTEXARRAYSOESPROC)eglGetProcAddress("glDeleteVertexArraysOES");
    initialized = true;
}

#define glBindVertexArray glBindVertexArrayOES_ptr
#define glGenVertexArrays glGenVertexArraysOES_ptr
#define glDeleteVertexArrays glDeleteVertexArraysOES_ptr

namespace havel {

// Simple 8x16 monospace font embedded as bitmap data
// Each character is 8 pixels wide, 16 pixels tall
// Total atlas: 128 chars * 8 = 1024 pixels wide, 16 pixels tall
// This is a minimal embedded font - replace with proper font atlas for production

// For now, we'll use a simple approach: generate a basic font texture programmatically
// In production, you'd load a proper font atlas PNG

static const char* fontVertSrc = R"(
    attribute vec2 a_position;
    attribute vec2 a_texCoord;
    varying vec2 v_texCoord;
    void main() {
        gl_Position = vec4(a_position, 0.0, 1.0);
        v_texCoord = a_texCoord;
    }
)";

static const char* fontFragSrc = R"(
    precision mediump float;
    varying vec2 v_texCoord;
    uniform sampler2D u_texture;
    uniform vec4 u_color;
    void main() {
        float alpha = texture2D(u_texture, v_texCoord).r;
        gl_FragColor = vec4(u_color.rgb, u_color.a * alpha);
    }
)";

BitmapFont::BitmapFont() = default;

BitmapFont::~BitmapFont() {
    shutdown();
}

bool BitmapFont::initialize(GLuint textureAtlas, int glyphWidth, int glyphHeight) {
    if (m_initialized) {
        return true;
    }
    
    // Initialize vertex array extension functions
    initVertexArrays();

    m_textureAtlas = textureAtlas;
    m_glyphWidth = glyphWidth;
    m_glyphHeight = glyphHeight;
    
    // Compile shaders
    GLuint vertShader = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vertShader, 1, &fontVertSrc, nullptr);
    glCompileShader(vertShader);
    
    GLuint fragShader = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fragShader, 1, &fontFragSrc, nullptr);
    glCompileShader(fragShader);
    
    m_shaderProgram = glCreateProgram();
    glAttachShader(m_shaderProgram, vertShader);
    glAttachShader(m_shaderProgram, fragShader);
    glLinkProgram(m_shaderProgram);
    
    glDeleteShader(vertShader);
    glDeleteShader(fragShader);
    
    // Create VBO/VAO
    glGenVertexArrays(1, &m_vao);
    glGenBuffers(1, &m_vbo);
    
    glBindVertexArray(m_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    
    // Allocate buffer for 6 vertices (2 triangles) * 4 floats (pos + texcoord)
    glBufferData(GL_ARRAY_BUFFER, 6 * 4 * sizeof(float), nullptr, GL_DYNAMIC_DRAW);
    
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
    glEnableVertexAttribArray(1);
    
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    
    // Load glyph metrics (simplified - assumes monospace font)
    loadGlyphMetrics();
    
    m_initialized = true;
    printf("[BitmapFont] Initialized (%dx%d glyphs)\n", glyphWidth, glyphHeight);
    return true;
}

void BitmapFont::shutdown() {
    if (m_vao) {
        glDeleteVertexArrays(1, &m_vao);
        m_vao = 0;
    }
    if (m_vbo) {
        glDeleteBuffers(1, &m_vbo);
        m_vbo = 0;
    }
    if (m_shaderProgram) {
        glDeleteProgram(m_shaderProgram);
        m_shaderProgram = 0;
    }
    m_initialized = false;
}

bool BitmapFont::loadGlyphMetrics() {
    // Simplified monospace font metrics
    // In production, load from font file or atlas metadata
    for (int i = 0; i < 128; i++) {
        Glyph& glyph = m_glyphs[i];
        glyph.textureX = (i % 16) * m_glyphWidth;
        glyph.textureY = (i / 16) * m_glyphHeight;
        glyph.width = m_glyphWidth;
        glyph.height = m_glyphHeight;
        glyph.bearingX = 0;
        glyph.bearingY = m_glyphHeight;
        glyph.advance = m_glyphWidth;  // Monospace
    }
    return true;
}

void BitmapFont::renderText(float x, float y, float scale, const char* text,
                             float r, float g, float b, float a) {
    if (!m_initialized || !text || m_textureAtlas == 0) return;

    glUseProgram(m_shaderProgram);

    GLint colorLoc = glGetUniformLocation(m_shaderProgram, "u_color");
    GLint texLoc = glGetUniformLocation(m_shaderProgram, "u_texture");

    glUniform4f(colorLoc, r, g, b, a);
    glUniform1i(texLoc, 0);

    glBindVertexArray(m_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    
    float cursorX = x;
    float cursorY = y;
    
    // Enable blending for transparency
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    
    // Render each character
    for (const char* c = text; *c != '\0'; c++) {
        unsigned char charCode = (unsigned char)*c;
        if (charCode >= 128) continue;  // Skip non-ASCII
        
        const Glyph& glyph = m_glyphs[charCode];
        
        // Calculate quad vertices
        float xpos = cursorX + glyph.bearingX * scale;
        float ypos = cursorY - (glyph.height - glyph.bearingY) * scale;
        float w = glyph.width * scale;
        float h = glyph.height * scale;
        
        // Calculate texture coordinates
        float atlasWidth = 128.0f * m_glyphWidth;  // 128 chars across
        float atlasHeight = m_glyphHeight;
        
        float u0 = glyph.textureX / atlasWidth;
        float v0 = glyph.textureY / atlasHeight;
        float u1 = (glyph.textureX + glyph.width) / atlasWidth;
        float v1 = (glyph.textureY + glyph.height) / atlasHeight;
        
        // Vertex data: pos (x,y) + texcoord (u,v)
        float vertices[6][4] = {
            { xpos,     ypos + h,   u0, v0 },
            { xpos,     ypos,       u0, v1 },
            { xpos + w, ypos,       u1, v1 },
            { xpos,     ypos + h,   u0, v0 },
            { xpos + w, ypos,       u1, v1 },
            { xpos + w, ypos + h,   u1, v0 },
        };
        
        // Update VBO
        glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(vertices), vertices);
        
        // Draw
        glBindTexture(GL_TEXTURE_2D, m_textureAtlas);
        glDrawArrays(GL_TRIANGLES, 0, 6);
        
        // Advance cursor
        cursorX += glyph.advance * scale;
    }
    
    glDisable(GL_BLEND);
    glBindVertexArray(0);
    glBindTexture(GL_TEXTURE_2D, 0);
}

float BitmapFont::getTextWidth(const char* text, float scale) {
    if (!text) return 0.0f;
    return strlen(text) * m_glyphWidth * scale;
}

float BitmapFont::getTextHeight(float scale) {
    return m_glyphHeight * scale;
}

} // namespace havel
