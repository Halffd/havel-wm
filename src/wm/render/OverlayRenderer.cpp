#include "OverlayRenderer.hpp"
#include <GLES2/gl2ext.h>
#include <EGL/egl.h>
#include <cstdio>
#include <cstring>
#include <cmath>

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

// ============================================================================
// Shader Sources
// ============================================================================

static const char* colorVertSrc = R"(
    attribute vec2 a_position;
    attribute vec4 a_color;
    varying vec4 v_color;
    void main() {
        gl_Position = vec4(a_position, 0.0, 1.0);
        v_color = a_color;
    }
)";

static const char* colorFragSrc = R"(
    precision mediump float;
    varying vec4 v_color;
    void main() {
        gl_FragColor = v_color;
    }
)";

static const char* textureVertSrc = R"(
    attribute vec2 a_position;
    attribute vec2 a_texCoord;
    varying vec2 v_texCoord;
    void main() {
        gl_Position = vec4(a_position, 0.0, 1.0);
        v_texCoord = a_texCoord;
    }
)";

static const char* textureFragSrc = R"(
    precision mediump float;
    varying vec2 v_texCoord;
    uniform sampler2D u_texture;
    uniform float u_alpha;
    void main() {
        vec4 color = texture2D(u_texture, v_texCoord);
        gl_FragColor = vec4(color.rgb, color.a * u_alpha);
    }
)";

// ============================================================================
// Implementation
// ============================================================================

OverlayRenderer::OverlayRenderer() = default;

OverlayRenderer::~OverlayRenderer() {
    shutdown();
}

bool OverlayRenderer::initialize() {
    if (m_initialized) {
        return true;
    }

    // Initialize vertex array extension functions
    initVertexArrays();

    // Check GL is available
    const char* version = (const char*)glGetString(GL_VERSION);
    if (!version) {
        fprintf(stderr, "[OverlayRenderer] No GL context available\n");
        return false;
    }

    printf("[OverlayRenderer] GL Version: %s\n", version);
    
    // Compile shaders
    if (!compileShaders()) {
        fprintf(stderr, "[OverlayRenderer] Failed to compile shaders\n");
        return false;
    }
    
    // Create geometry buffers
    createGeometry();
    
    m_initialized = true;
    printf("[OverlayRenderer] Initialized\n");
    return true;
}

void OverlayRenderer::shutdown() {
    if (!m_initialized) return;
    
    cleanup();
    m_initialized = false;
}

bool OverlayRenderer::compileShaders() {
    // Color shader program
    GLuint vertShader = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vertShader, 1, &colorVertSrc, nullptr);
    glCompileShader(vertShader);
    
    GLint success;
    glGetShaderiv(vertShader, GL_COMPILE_STATUS, &success);
    if (!success) {
        char log[512];
        glGetShaderInfoLog(vertShader, 512, nullptr, log);
        fprintf(stderr, "[OverlayRenderer] Color vertex shader error: %s\n", log);
        return false;
    }
    
    GLuint fragShader = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fragShader, 1, &colorFragSrc, nullptr);
    glCompileShader(fragShader);
    
    glGetShaderiv(fragShader, GL_COMPILE_STATUS, &success);
    if (!success) {
        char log[512];
        glGetShaderInfoLog(fragShader, 512, nullptr, log);
        fprintf(stderr, "[OverlayRenderer] Color fragment shader error: %s\n", log);
        return false;
    }
    
    m_colorShader = glCreateProgram();
    glAttachShader(m_colorShader, vertShader);
    glAttachShader(m_colorShader, fragShader);
    glLinkProgram(m_colorShader);
    
    glGetProgramiv(m_colorShader, GL_LINK_STATUS, &success);
    if (!success) {
        char log[512];
        glGetProgramInfoLog(m_colorShader, 512, nullptr, log);
        fprintf(stderr, "[OverlayRenderer] Color shader program error: %s\n", log);
        return false;
    }
    
    glDeleteShader(vertShader);
    glDeleteShader(fragShader);
    
    // Texture shader program
    vertShader = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vertShader, 1, &textureVertSrc, nullptr);
    glCompileShader(vertShader);
    
    glGetShaderiv(vertShader, GL_COMPILE_STATUS, &success);
    if (!success) {
        char log[512];
        glGetShaderInfoLog(vertShader, 512, nullptr, log);
        return false;
    }
    
    fragShader = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fragShader, 1, &textureFragSrc, nullptr);
    glCompileShader(fragShader);
    
    glGetShaderiv(fragShader, GL_COMPILE_STATUS, &success);
    if (!success) {
        char log[512];
        glGetShaderInfoLog(fragShader, 512, nullptr, log);
        return false;
    }
    
    m_textureShader = glCreateProgram();
    glAttachShader(m_textureShader, vertShader);
    glAttachShader(m_textureShader, fragShader);
    glLinkProgram(m_textureShader);
    
    glGetProgramiv(m_textureShader, GL_LINK_STATUS, &success);
    if (!success) {
        char log[512];
        glGetProgramInfoLog(m_textureShader, 512, nullptr, log);
        return false;
    }
    
    glDeleteShader(vertShader);
    glDeleteShader(fragShader);
    
    printf("[OverlayRenderer] Shaders compiled successfully\n");
    return true;
}

void OverlayRenderer::createGeometry() {
    glGenVertexArrays(1, &m_vao);
    glGenBuffers(1, &m_vbo);
    
    glBindVertexArray(m_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    
    // Allocate buffer (will be filled with data each frame)
    glBufferData(GL_ARRAY_BUFFER, 1024 * sizeof(float), nullptr, GL_DYNAMIC_DRAW);
    
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void OverlayRenderer::cleanup() {
    if (m_colorShader) {
        glDeleteProgram(m_colorShader);
        m_colorShader = 0;
    }
    if (m_textureShader) {
        glDeleteProgram(m_textureShader);
        m_textureShader = 0;
    }
    if (m_vao) {
        glDeleteVertexArrays(1, &m_vao);
        m_vao = 0;
    }
    if (m_vbo) {
        glDeleteBuffers(1, &m_vbo);
        m_vbo = 0;
    }
    if (m_fontTexture) {
        glDeleteTextures(1, &m_fontTexture);
        m_fontTexture = 0;
    }
}

void OverlayRenderer::beginFrame(int screenWidth, int screenHeight) {
    if (!m_initialized) return;
    
    m_screenWidth = screenWidth;
    m_screenHeight = screenHeight;
    
    // Setup viewport
    glViewport(0, 0, screenWidth, screenHeight);
    
    // Enable blending for transparency
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    
    // Disable depth test (2D overlay)
    glDisable(GL_DEPTH_TEST);
}

void OverlayRenderer::endFrame() {
    if (!m_initialized) return;
    
    // Restore state
    glDisable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);
}

void OverlayRenderer::drawRect(const FloatRect& rect, const Color& color) {
    drawRect(rect.x, rect.y, rect.w, rect.h, color);
}

void OverlayRenderer::drawRect(float x, float y, float w, float h, const Color& color) {
    if (!m_initialized) return;
    
    // Convert to clip space (-1 to 1)
    float left = (2.0f * x) / m_screenWidth - 1.0f;
    float top = 1.0f - (2.0f * y) / m_screenHeight;
    float right = (2.0f * (x + w)) / m_screenWidth - 1.0f;
    float bottom = 1.0f - (2.0f * (y + h)) / m_screenHeight;
    
    // Vertex data: position (x,y) + color (r,g,b,a)
    float vertices[] = {
        // pos x, pos y,  r, g, b, a
        left,   bottom,  color.r, color.g, color.b, color.a,
        right,  bottom,  color.r, color.g, color.b, color.a,
        right,  top,     color.r, color.g, color.b, color.a,
        left,   bottom,  color.r, color.g, color.b, color.a,
        right,  top,     color.r, color.g, color.b, color.a,
        left,   top,     color.r, color.g, color.b, color.a,
    };
    
    glUseProgram(m_colorShader);
    
    GLint posLoc = glGetAttribLocation(m_colorShader, "a_position");
    GLint colorLoc = glGetAttribLocation(m_colorShader, "a_color");
    
    glBindVertexArray(m_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(vertices), vertices);
    
    glEnableVertexAttribArray(posLoc);
    glVertexAttribPointer(posLoc, 2, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)0);
    
    glEnableVertexAttribArray(colorLoc);
    glVertexAttribPointer(colorLoc, 4, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)(2 * sizeof(float)));
    
    glDrawArrays(GL_TRIANGLES, 0, 6);
    
    glBindVertexArray(0);
}

void OverlayRenderer::drawBorder(const FloatRect& rect, const Color& color, float thickness) {
    // Draw 4 rectangles for border
    drawRect(rect.x, rect.y, rect.w, thickness, color);  // Top
    drawRect(rect.x, rect.y + rect.h - thickness, rect.w, thickness, color);  // Bottom
    drawRect(rect.x, rect.y, thickness, rect.h, color);  // Left
    drawRect(rect.x + rect.w - thickness, rect.y, thickness, rect.h, color);  // Right
}

void OverlayRenderer::drawTexture(GLuint texture, const FloatRect& rect, float alpha) {
    drawTexture(texture, rect.x, rect.y, rect.w, rect.h, alpha);
}

void OverlayRenderer::drawTexture(GLuint texture, float x, float y, float w, float h, float alpha) {
    if (!m_initialized || !texture) return;
    
    // Convert to clip space
    float left = (2.0f * x) / m_screenWidth - 1.0f;
    float top = 1.0f - (2.0f * y) / m_screenHeight;
    float right = (2.0f * (x + w)) / m_screenWidth - 1.0f;
    float bottom = 1.0f - (2.0f * (y + h)) / m_screenHeight;
    
    // Vertex data: position (x,y) + texCoord (u,v)
    float vertices[] = {
        // pos x, pos y,  u, v
        left,   bottom,  0.0f, 1.0f,
        right,  bottom,  1.0f, 1.0f,
        right,  top,     1.0f, 0.0f,
        left,   bottom,  0.0f, 1.0f,
        right,  top,     1.0f, 0.0f,
        left,   top,     0.0f, 0.0f,
    };
    
    glUseProgram(m_textureShader);
    
    GLint posLoc = glGetAttribLocation(m_textureShader, "a_position");
    GLint texCoordLoc = glGetAttribLocation(m_textureShader, "a_texCoord");
    GLint alphaLoc = glGetUniformLocation(m_textureShader, "u_alpha");
    GLint texLoc = glGetUniformLocation(m_textureShader, "u_texture");
    
    glUniform1f(alphaLoc, alpha);
    glUniform1i(texLoc, 0);
    
    glBindVertexArray(m_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(vertices), vertices);
    
    glEnableVertexAttribArray(posLoc);
    glVertexAttribPointer(posLoc, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    
    glEnableVertexAttribArray(texCoordLoc);
    glVertexAttribPointer(texCoordLoc, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
    
    glBindTexture(GL_TEXTURE_2D, texture);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindTexture(GL_TEXTURE_2D, 0);
    
    glBindVertexArray(0);
}

void OverlayRenderer::drawText(const char* text, float x, float y, float size, const Color& color) {
    if (!m_initialized || !text) return;

    // STUB: Bitmap font rendering requires font atlas texture
    // For now, draw a placeholder rectangle

    float textWidth = strlen(text) * size * 0.6f;
    float textHeight = size;

    // Draw background box
    drawRect(x - 2, y - 2, textWidth + 4, textHeight + 4, Color(0.0f, 0.0f, 0.0f, 0.8f));

    // Draw border
    drawBorder(FloatRect(x - 2, y - 2, textWidth + 4, textHeight + 4), Color(1.0f, 1.0f, 1.0f, 0.5f), 1.0f);

    // TODO: Implement actual bitmap font rendering
    // Requires: font atlas texture, glyph metrics, texture coordinate lookup
}

void OverlayRenderer::drawTextCentered(const char* text, float cx, float cy, float size, const Color& color) {
    if (!text) return;
    
    float textWidth = strlen(text) * size * 0.6f;
    float x = cx - textWidth / 2.0f;
    float y = cy - size / 2.0f;
    
    drawText(text, x, y, size, color);
}

} // namespace havel
