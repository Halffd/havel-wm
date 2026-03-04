// Blur Shader - Kawase blur implementation for background effects
// Multi-pass blur using framebuffer objects

#pragma once

#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <EGL/egl.h>
#include <string>
#include <vector>

namespace havel {

/**
 * Kawase Blur Shader
 * 
 * Implements multi-pass Kawase blur for:
 * - Window background blur
 * - Desktop dimming with blur
 * - Overlay backdrop blur
 * 
 * Usage:
 * 1. Create BlurShader with desired radius
 * 2. Bind input texture
 * 3. Call blur() to render to target FBO
 * 4. Use result texture
 * 
 * The blur uses multiple passes with increasing offset
 * to achieve a Gaussian-like effect efficiently.
 */
class BlurShader {
public:
    BlurShader();
    ~BlurShader();

    // Initialize shader program and FBOs
    bool initialize(int width, int height, int radius = 3);
    void shutdown();

    // Perform blur on input texture, render to output FBO
    void blur(GLuint inputTexture, GLuint outputFBO, int width, int height);

    // Get the intermediate FBOs (for chaining)
    GLuint getIntermediateFBO() const { return m_fbo1; }
    GLuint getIntermediateTexture() const { return m_texture1; }

    // Settings
    void setRadius(int radius) { m_radius = radius; }
    void setStrength(float strength) { m_strength = strength; }
    int getRadius() const { return m_radius; }
    float getStrength() const { return m_strength; }

    bool isInitialized() const { return m_initialized; }

private:
    bool compileShader(const char* vertexSource, const char* fragmentSource);
    void createFBOs(int width, int height);
    void destroyFBOs();

    bool m_initialized = false;
    GLuint m_program = 0;
    GLint m_posLoc = -1;
    GLint m_texCoordLoc = -1;
    GLint m_resolutionLoc = -1;
    GLint m_offsetLoc = -1;
    GLint m_sampleCountLoc = -1;
    GLint m_textureLoc = -1;

    // Framebuffer objects for multi-pass
    GLuint m_fbo1 = 0;
    GLuint m_texture1 = 0;
    GLuint m_fbo2 = 0;
    GLuint m_texture2 = 0;

    // Settings
    int m_radius = 3;
    float m_strength = 1.0f;

    // Geometry
    GLuint m_vao = 0;
    GLuint m_vbo = 0;
};

} // namespace havel
