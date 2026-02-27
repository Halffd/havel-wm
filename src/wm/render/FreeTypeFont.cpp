#include "FreeTypeFont.hpp"
#include <GLES2/gl2ext.h>
#include <EGL/egl.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <cstdio>
#include <cstring>

namespace havel {

// GLES2 vertex arrays extension
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

// Shader sources
static const char* fontVertSrc = R"(
    attribute vec4 vertex; // <vec2 pos, vec2 tex>
    varying vec2 TexCoords;
    uniform mat4 projection;
    void main() {
        gl_Position = projection * vec4(vertex.xy, 0.0, 1.0);
        TexCoords = vertex.zw;
    }
)";

static const char* fontFragSrc = R"(
    precision mediump float;
    varying vec2 TexCoords;
    uniform sampler2D text;
    uniform vec3 textColor;
    void main() {
        vec4 sampled = vec4(1.0, 1.0, 1.0, texture2D(text, TexCoords).r);
        gl_FragColor = vec4(textColor, 1.0) * sampled;
    }
)";

FreeTypeFont::FreeTypeFont() = default;

FreeTypeFont::~FreeTypeFont() {
    shutdown();
}

bool FreeTypeFont::loadFont(const char* fontPath, unsigned int fontSize) {
    if (m_initialized) {
        shutdown();
    }
    
    initVertexArrays();
    
    // Initialize FreeType library
    if (FT_Init_FreeType(&m_ftLibrary)) {
        fprintf(stderr, "[FreeTypeFont] ERROR: Could not init FreeType Library\n");
        return false;
    }
    
    // Load font face
    if (FT_New_Face(m_ftLibrary, fontPath, 0, &m_ftFace)) {
        fprintf(stderr, "[FreeTypeFont] ERROR: Failed to load font: %s\n", fontPath);
        FT_Done_FreeType(m_ftLibrary);
        return false;
    }
    
    // Set pixel size
    FT_Set_Pixel_Sizes(m_ftFace, 0, fontSize);
    
    m_fontPath = fontPath;
    m_fontSize = fontSize;
    
    // Create shader program
    if (!createShaderProgram()) {
        fprintf(stderr, "[FreeTypeFont] ERROR: Failed to create shader program\n");
        FT_Done_Face(m_ftFace);
        FT_Done_FreeType(m_ftLibrary);
        return false;
    }
    
    // Create VBO/VAO
    glGenVertexArrays(1, &m_vao);
    glGenBuffers(1, &m_vbo);
    
    glBindVertexArray(m_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    
    // Allocate buffer for 6 vertices (2 triangles) * 4 floats (pos + texcoord)
    glBufferData(GL_ARRAY_BUFFER, 6 * 4 * sizeof(float), nullptr, GL_DYNAMIC_DRAW);
    
    glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    
    // Disable byte alignment for 1-byte textures
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    
    m_initialized = true;
    printf("[FreeTypeFont] Loaded: %s (%u px)\n", fontPath, fontSize);
    return true;
}

void FreeTypeFont::shutdown() {
    if (!m_initialized) return;
    
    // Clean up glyphs
    for (auto& glyph : m_glyphs) {
        if (glyph && glyph->textureID) {
            glDeleteTextures(1, &glyph->textureID);
        }
    }
    
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
    if (m_ftFace) {
        FT_Done_Face(m_ftFace);
        m_ftFace = nullptr;
    }
    if (m_ftLibrary) {
        FT_Done_FreeType(m_ftLibrary);
        m_ftLibrary = nullptr;
    }
    
    m_initialized = false;
}

bool FreeTypeFont::createShaderProgram() {
    GLuint vertShader = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vertShader, 1, &fontVertSrc, nullptr);
    glCompileShader(vertShader);
    
    GLint success;
    glGetShaderiv(vertShader, GL_COMPILE_STATUS, &success);
    if (!success) {
        char log[512];
        glGetShaderInfoLog(vertShader, 512, nullptr, log);
        fprintf(stderr, "[FreeTypeFont] Vertex shader error: %s\n", log);
        return false;
    }
    
    GLuint fragShader = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fragShader, 1, &fontFragSrc, nullptr);
    glCompileShader(fragShader);
    
    glGetShaderiv(fragShader, GL_COMPILE_STATUS, &success);
    if (!success) {
        char log[512];
        glGetShaderInfoLog(fragShader, 512, nullptr, log);
        fprintf(stderr, "[FreeTypeFont] Fragment shader error: %s\n", log);
        return false;
    }
    
    m_shaderProgram = glCreateProgram();
    glAttachShader(m_shaderProgram, vertShader);
    glAttachShader(m_shaderProgram, fragShader);
    glLinkProgram(m_shaderProgram);
    
    glGetProgramiv(m_shaderProgram, GL_LINK_STATUS, &success);
    if (!success) {
        char log[512];
        glGetProgramInfoLog(m_shaderProgram, 512, nullptr, log);
        fprintf(stderr, "[FreeTypeFont] Shader program error: %s\n", log);
        return false;
    }
    
    glDeleteShader(vertShader);
    glDeleteShader(fragShader);
    
    m_projectionLoc = glGetUniformLocation(m_shaderProgram, "projection");
    
    return true;
}

bool FreeTypeFont::loadGlyph(unsigned char c) {
    if (m_glyphs[c]) {
        return true;  // Already loaded
    }
    
    // Load character glyph
    if (FT_Load_Char(m_ftFace, c, FT_LOAD_RENDER)) {
        fprintf(stderr, "[FreeTypeFont] ERROR: Failed to load glyph '%c'\n", c);
        return false;
    }
    
    // Generate texture
    GLuint texture;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexImage2D(
        GL_TEXTURE_2D,
        0,
        GL_LUMINANCE,
        m_ftFace->glyph->bitmap.width,
        m_ftFace->glyph->bitmap.rows,
        0,
        GL_LUMINANCE,
        GL_UNSIGNED_BYTE,
        m_ftFace->glyph->bitmap.buffer
    );
    
    // Set texture options
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glBindTexture(GL_TEXTURE_2D, 0);
    
    // Store glyph metrics
    auto glyph = std::make_unique<Glyph>();
    glyph->textureID = texture;
    glyph->width = m_ftFace->glyph->bitmap.width;
    glyph->height = m_ftFace->glyph->bitmap.rows;
    glyph->bearingX = m_ftFace->glyph->bitmap_left;
    glyph->bearingY = m_ftFace->glyph->bitmap_top;
    glyph->advance = m_ftFace->glyph->advance.x;
    
    m_glyphs[c] = std::move(glyph);
    
    return true;
}

void FreeTypeFont::renderText(float x, float y, float scale, const char* text,
                               float r, float g, float b, float a) {
    if (!m_initialized || !text) return;

    glUseProgram(m_shaderProgram);

    // Create orthographic projection matrix using current screen dimensions
    glm::mat4 projection = glm::ortho(0.0f, (float)m_screenWidth, 0.0f, (float)m_screenHeight);
    glUniformMatrix4fv(m_projectionLoc, 1, GL_FALSE, &projection[0][0]);

    glUniform3f(glGetUniformLocation(m_shaderProgram, "textColor"), r, g, b);
    
    glActiveTexture(GL_TEXTURE0);
    glBindVertexArray(m_vao);
    
    // Enable blending
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    
    // Iterate through all characters
    for (const char* c = text; *c != '\0'; c++) {
        unsigned char charCode = (unsigned char)*c;
        
        // Load glyph if not in cache
        if (charCode < 128 && !m_glyphs[charCode]) {
            loadGlyph(charCode);
        }
        
        if (charCode >= 128 || !m_glyphs[charCode]) {
            continue;  // Skip non-ASCII or failed glyphs
        }
        
        Glyph* ch = m_glyphs[charCode].get();
        
        float xpos = x + ch->bearingX * scale;
        float ypos = y - (ch->height - ch->bearingY) * scale;
        
        float w = ch->width * scale;
        float h = ch->height * scale;
        
        // Update VBO for each character
        float vertices[6][4] = {
            { xpos,     ypos + h,   0.0f, 0.0f },
            { xpos,     ypos,       0.0f, 1.0f },
            { xpos + w, ypos,       1.0f, 1.0f },
            
            { xpos,     ypos + h,   0.0f, 0.0f },
            { xpos + w, ypos,       1.0f, 1.0f },
            { xpos + w, ypos + h,   1.0f, 0.0f },
        };
        
        // Render glyph texture over quad
        glBindTexture(GL_TEXTURE_2D, ch->textureID);
        
        // Update content of VBO memory
        glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
        glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(vertices), vertices);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        
        // Render quad
        glDrawArrays(GL_TRIANGLES, 0, 6);
        
        // Advance cursor for next glyph
        x += (ch->advance >> 6) * scale;  // Bitshift by 6 to get value in pixels
    }
    
    glBindVertexArray(0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glDisable(GL_BLEND);
}

float FreeTypeFont::getTextWidth(const char* text, float scale) {
    if (!text || !m_initialized) return 0.0f;
    
    float width = 0.0f;
    for (const char* c = text; *c != '\0'; c++) {
        unsigned char charCode = (unsigned char)*c;
        if (charCode < 128 && m_glyphs[charCode]) {
            width += (m_glyphs[charCode]->advance >> 6) * scale;
        } else {
            width += m_fontSize * scale;  // Estimate for unloaded glyphs
        }
    }
    return width;
}

void FreeTypeFont::setProjection(int screenWidth, int screenHeight) {
    m_screenWidth = screenWidth;
    m_screenHeight = screenHeight;
}

float FreeTypeFont::getTextHeight(float scale) {
    return m_fontSize * scale;
}

} // namespace havel
