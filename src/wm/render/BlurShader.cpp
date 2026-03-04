// Blur Shader Implementation - Kawase blur
#include "BlurShader.hpp"
#include <cstdio>
#include <cstring>
#include <cmath>

namespace havel {

// Vertex shader source
static const char* s_vertexSource = R"(
attribute vec2 a_position;
attribute vec2 a_texCoord;
varying vec2 v_texCoord;
void main() {
    gl_Position = vec4(a_position, 0.0, 1.0);
    v_texCoord = a_texCoord;
}
)";

// Fragment shader source (Kawase blur)
static const char* s_fragmentSource = R"(
precision mediump float;
uniform vec2 u_resolution;
uniform vec2 u_offset;
uniform float u_sample_count;
uniform sampler2D u_texture;
varying vec2 v_texCoord;
void main() {
    vec2 pixel = 1.0 / u_resolution;
    vec4 color = vec4(0.0);
    float total_weight = 0.0;
    
    // Center sample
    color += texture2D(u_texture, v_texCoord);
    total_weight += 1.0;
    
    // Offset samples
    for (float i = 1.0; i <= u_sample_count; i++) {
        vec2 offset = u_offset * pixel * i;
        color += texture2D(u_texture, v_texCoord + offset);
        color += texture2D(u_texture, v_texCoord - offset);
        color += texture2D(u_texture, v_texCoord + offset.yx);
        color += texture2D(u_texture, v_texCoord - offset.yx);
        total_weight += 4.0;
    }
    
    gl_FragColor = color / total_weight;
}
)";

BlurShader::BlurShader() = default;

BlurShader::~BlurShader() {
    shutdown();
}

bool BlurShader::initialize(int width, int height, int radius) {
    if (m_initialized) return true;

    m_radius = radius;

    // Compile shader program
    if (!compileShader(s_vertexSource, s_fragmentSource)) {
        fprintf(stderr, "[BlurShader] Failed to compile shader\n");
        return false;
    }

    // Create FBOs for multi-pass blur
    createFBOs(width, height);

    // Create geometry (fullscreen quad)
    glGenBuffers(1, &m_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);

    // Fullscreen quad vertices
    float vertices[] = {
        // position x, y    texcoord u, v
        -1.0f, -1.0f,       0.0f, 1.0f,
         1.0f, -1.0f,       1.0f, 1.0f,
         1.0f,  1.0f,       1.0f, 0.0f,
        -1.0f, -1.0f,       0.0f, 1.0f,
         1.0f,  1.0f,       1.0f, 0.0f,
        -1.0f,  1.0f,       0.0f, 0.0f,
    };

    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

    m_initialized = true;
    printf("[BlurShader] Initialized (radius=%d, %dx%d)\n", m_radius, width, height);
    return true;
}

void BlurShader::shutdown() {
    if (m_program) {
        glDeleteProgram(m_program);
        m_program = 0;
    }

    destroyFBOs();

    if (m_vbo) {
        glDeleteBuffers(1, &m_vbo);
        m_vbo = 0;
    }

    m_initialized = false;
}

void BlurShader::blur(GLuint inputTexture, GLuint outputFBO, int width, int height) {
    if (!m_initialized || !inputTexture) return;

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);

    // Pass 1: input -> FBO1
    glBindFramebuffer(GL_FRAMEBUFFER, m_fbo1);
    glViewport(0, 0, width, height);

    glUseProgram(m_program);
    glUniform2f(m_resolutionLoc, static_cast<float>(width), static_cast<float>(height));
    glUniform2f(m_offsetLoc, static_cast<float>(m_radius), static_cast<float>(m_radius));
    glUniform1f(m_sampleCountLoc, static_cast<float>(m_radius));
    glUniform1i(m_textureLoc, 0);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, inputTexture);

    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glEnableVertexAttribArray(m_posLoc);
    glEnableVertexAttribArray(m_texCoordLoc);
    glVertexAttribPointer(m_posLoc, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    glVertexAttribPointer(m_texCoordLoc, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));

    glDrawArrays(GL_TRIANGLES, 0, 6);

    // Pass 2: FBO1 -> FBO2 (with larger offset for stronger blur)
    glBindFramebuffer(GL_FRAMEBUFFER, m_fbo2);
    glBindTexture(GL_TEXTURE_2D, m_texture1);

    float offsetScale = 1.5f;  // Increase offset for second pass
    glUniform2f(m_offsetLoc, 
                static_cast<float>(m_radius) * offsetScale,
                static_cast<float>(m_radius) * offsetScale);

    glDrawArrays(GL_TRIANGLES, 0, 6);

    // Pass 3: FBO2 -> output (final pass)
    glBindFramebuffer(GL_FRAMEBUFFER, outputFBO);
    glBindTexture(GL_TEXTURE_2D, m_texture2);

    glUniform2f(m_offsetLoc,
                static_cast<float>(m_radius) * offsetScale * 0.5f,
                static_cast<float>(m_radius) * offsetScale * 0.5f);

    glDrawArrays(GL_TRIANGLES, 0, 6);

    // Cleanup
    glBindTexture(GL_TEXTURE_2D, 0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glDisableVertexAttribArray(m_posLoc);
    glDisableVertexAttribArray(m_texCoordLoc);
}

bool BlurShader::compileShader(const char* vertexSource, const char* fragmentSource) {
    // Create vertex shader
    GLuint vertexShader = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vertexShader, 1, &vertexSource, nullptr);
    glCompileShader(vertexShader);

    // Check compilation
    GLint compiled = 0;
    glGetShaderiv(vertexShader, GL_COMPILE_STATUS, &compiled);
    if (!compiled) {
        char log[512];
        glGetShaderInfoLog(vertexShader, sizeof(log), nullptr, log);
        fprintf(stderr, "[BlurShader] Vertex shader compile error: %s\n", log);
        glDeleteShader(vertexShader);
        return false;
    }

    // Create fragment shader
    GLuint fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fragmentShader, 1, &fragmentSource, nullptr);
    glCompileShader(fragmentShader);

    // Check compilation
    glGetShaderiv(fragmentShader, GL_COMPILE_STATUS, &compiled);
    if (!compiled) {
        char log[512];
        glGetShaderInfoLog(fragmentShader, sizeof(log), nullptr, log);
        fprintf(stderr, "[BlurShader] Fragment shader compile error: %s\n", log);
        glDeleteShader(vertexShader);
        glDeleteShader(fragmentShader);
        return false;
    }

    // Create program
    m_program = glCreateProgram();
    glAttachShader(m_program, vertexShader);
    glAttachShader(m_program, fragmentShader);
    glLinkProgram(m_program);

    // Check linking
    GLint linked = 0;
    glGetProgramiv(m_program, GL_LINK_STATUS, &linked);
    if (!linked) {
        char log[512];
        glGetProgramInfoLog(m_program, sizeof(log), nullptr, log);
        fprintf(stderr, "[BlurShader] Program link error: %s\n", log);
        glDeleteProgram(m_program);
        glDeleteShader(vertexShader);
        glDeleteShader(fragmentShader);
        return false;
    }

    // Clean up shaders (they're linked into program now)
    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);

    // Get uniform and attribute locations
    m_posLoc = glGetAttribLocation(m_program, "a_position");
    m_texCoordLoc = glGetAttribLocation(m_program, "a_texCoord");
    m_resolutionLoc = glGetUniformLocation(m_program, "u_resolution");
    m_offsetLoc = glGetUniformLocation(m_program, "u_offset");
    m_sampleCountLoc = glGetUniformLocation(m_program, "u_sample_count");
    m_textureLoc = glGetUniformLocation(m_program, "u_texture");

    return true;
}

void BlurShader::createFBOs(int width, int height) {
    destroyFBOs();

    // Create texture 1
    glGenTextures(1, &m_texture1);
    glBindTexture(GL_TEXTURE_2D, m_texture1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    // Create FBO 1
    glGenFramebuffers(1, &m_fbo1);
    glBindFramebuffer(GL_FRAMEBUFFER, m_fbo1);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_texture1, 0);

    // Create texture 2
    glGenTextures(1, &m_texture2);
    glBindTexture(GL_TEXTURE_2D, m_texture2);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    // Create FBO 2
    glGenFramebuffers(1, &m_fbo2);
    glBindFramebuffer(GL_FRAMEBUFFER, m_fbo2);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_texture2, 0);

    // Check FBO completeness
    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        fprintf(stderr, "[BlurShader] FBO incomplete: 0x%x\n", status);
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glBindTexture(GL_TEXTURE_2D, 0);
}

void BlurShader::destroyFBOs() {
    if (m_fbo1) {
        glDeleteFramebuffers(1, &m_fbo1);
        m_fbo1 = 0;
    }
    if (m_texture1) {
        glDeleteTextures(1, &m_texture1);
        m_texture1 = 0;
    }
    if (m_fbo2) {
        glDeleteFramebuffers(1, &m_fbo2);
        m_fbo2 = 0;
    }
    if (m_texture2) {
        glDeleteTextures(1, &m_texture2);
        m_texture2 = 0;
    }
}

} // namespace havel
