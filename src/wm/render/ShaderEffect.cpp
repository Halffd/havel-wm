#include "ShaderEffect.hpp"
#include <cstdio>
#include <cstring>

namespace havel {

// ============================================================================
// ShaderEffect Implementation
// ============================================================================

bool ShaderEffect::initialize() {
    if (m_initialized) {
        return true;
    }
    
    const char* vertSrc = vertexSource();
    const char* fragSrc = fragmentSource();
    
    if (!vertSrc || !fragSrc) {
        fprintf(stderr, "[ShaderEffect] Missing shader source for %s\n", name());
        return false;
    }
    
    // Compile shaders
    GLuint vertShader = compileShader(GL_VERTEX_SHADER, vertSrc);
    if (!vertShader) {
        fprintf(stderr, "[ShaderEffect] Failed to compile vertex shader for %s\n", name());
        return false;
    }
    
    GLuint fragShader = compileShader(GL_FRAGMENT_SHADER, fragSrc);
    if (!fragShader) {
        fprintf(stderr, "[ShaderEffect] Failed to compile fragment shader for %s\n", name());
        glDeleteShader(vertShader);
        return false;
    }
    
    // Link program
    m_program = linkProgram(vertShader, fragShader);
    glDeleteShader(vertShader);
    glDeleteShader(fragShader);
    
    if (!m_program) {
        fprintf(stderr, "[ShaderEffect] Failed to link program for %s\n", name());
        return false;
    }
    
    // Cache uniform locations
    m_uniformTexture = glGetUniformLocation(m_program, "u_texture");
    m_uniformIntensity = glGetUniformLocation(m_program, "u_intensity");
    m_uniformResolution = glGetUniformLocation(m_program, "u_resolution");
    
    // Create fullscreen quad VAO/VBO
    // Note: GLES2 may not have VAO, use VBO only if needed
    glGenBuffers(1, &m_vertexBuffer);
    glBindBuffer(GL_ARRAY_BUFFER, m_vertexBuffer);
    
    // Fullscreen quad vertices (2 triangles)
    float vertices[] = {
        // Position      // TexCoord
        -1.0f, -1.0f,    0.0f, 1.0f,
         1.0f, -1.0f,    1.0f, 1.0f,
         1.0f,  1.0f,    1.0f, 0.0f,
        -1.0f, -1.0f,    0.0f, 1.0f,
         1.0f,  1.0f,    1.0f, 0.0f,
        -1.0f,  1.0f,    0.0f, 0.0f
    };
    
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
    
    // Note: In GLES2, vertex attrib setup would be done during render
    // For now, just store the VBO
    
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    
    m_initialized = true;
    printf("[ShaderEffect] Initialized: %s (program=%u)\n", name(), m_program);
    
    return true;
}

void ShaderEffect::cleanup() {
    if (m_program) {
        glDeleteProgram(m_program);
        m_program = 0;
    }
    if (m_vertexBuffer) {
        glDeleteBuffers(1, &m_vertexBuffer);
        m_vertexBuffer = 0;
    }
    
    m_uniformTexture = -1;
    m_uniformIntensity = -1;
    m_uniformResolution = -1;
    m_initialized = false;
}

GLuint ShaderEffect::compileShader(GLenum type, const char* source) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);
    
    // Check compile status
    GLint compiled = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
    if (!compiled) {
        GLint logLen = 0;
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &logLen);
        if (logLen > 0) {
            char* log = new char[logLen];
            glGetShaderInfoLog(shader, logLen, &logLen, log);
            fprintf(stderr, "[ShaderEffect] Shader compile error:\n%s\n", log);
            delete[] log;
        }
        glDeleteShader(shader);
        return 0;
    }
    
    return shader;
}

GLuint ShaderEffect::linkProgram(GLuint vertShader, GLuint fragShader) {
    GLuint program = glCreateProgram();
    glAttachShader(program, vertShader);
    glAttachShader(program, fragShader);
    glLinkProgram(program);
    
    // Check link status
    GLint linked = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &linked);
    if (!linked) {
        GLint logLen = 0;
        glGetProgramiv(program, GL_INFO_LOG_LENGTH, &logLen);
        if (logLen > 0) {
            char* log = new char[logLen];
            glGetProgramInfoLog(program, logLen, &logLen, log);
            fprintf(stderr, "[ShaderEffect] Program link error:\n%s\n", log);
            delete[] log;
        }
        glDeleteProgram(program);
        return 0;
    }
    
    return program;
}

const char* ShaderEffect::vertexSource() const {
    // Default fullscreen quad vertex shader
    return R"(
        attribute vec2 a_position;
        attribute vec2 a_texCoord;
        varying vec2 v_texCoord;
        
        void main() {
            gl_Position = vec4(a_position, 0.0, 1.0);
            v_texCoord = a_texCoord;
        }
    )";
}

// ============================================================================
// GrayscaleEffect Implementation
// ============================================================================

const char* GrayscaleEffect::fragmentSource() const {
    return R"(
        precision mediump float;
        
        varying vec2 v_texCoord;
        uniform sampler2D u_texture;
        uniform float u_intensity;
        uniform vec3 u_weights;
        
        void main() {
            vec4 color = texture2D(u_texture, v_texCoord);
            float luminance = dot(color.rgb, u_weights);
            vec3 gray = vec3(luminance);
            color.rgb = mix(color.rgb, gray, u_intensity);
            gl_FragColor = color;
        }
    )";
}

void GrayscaleEffect::setWeights(float r, float g, float b) {
    m_weightR = r;
    m_weightG = g;
    m_weightB = b;
}

// ============================================================================
// NegativeEffect Implementation
// ============================================================================

const char* NegativeEffect::fragmentSource() const {
    return R"(
        precision mediump float;
        
        varying vec2 v_texCoord;
        uniform sampler2D u_texture;
        uniform float u_intensity;
        uniform bool u_invertAlpha;
        
        void main() {
            vec4 color = texture2D(u_texture, v_texCoord);
            color.rgb = mix(color.rgb, 1.0 - color.rgb, u_intensity);
            if (u_invertAlpha) {
                color.a = 1.0 - color.a;
            }
            gl_FragColor = color;
        }
    )";
}

} // namespace havel
