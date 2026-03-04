#pragma once

#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <cstdint>
#include <string>

namespace havel {

/**
 * Shader effect base class
 * 
 * Effects are applied in the render pipeline post-processing stage.
 */
class ShaderEffect {
public:
    virtual ~ShaderEffect() = default;
    
    // Effect name for identification
    virtual const char* name() const = 0;
    
    // Initialize shader program
    virtual bool initialize();
    
    // Cleanup shader resources
    virtual void cleanup();
    
    // Check if effect is enabled
    bool isEnabled() const { return m_enabled; }
    
    // Enable/disable effect
    void setEnabled(bool enabled) { m_enabled = enabled; }

    // Effect intensity (0.0 = off, 1.0 = full)
    float intensity() const { return m_intensity; }
    void setIntensity(float intensity) { m_intensity = intensity; }

    // Get shader program handle
    GLuint program() const { return m_program; }
    
    // Get uniform locations (cached after first use)
    GLint uniformTexture() const { return m_uniformTexture; }
    GLint uniformIntensity() const { return m_uniformIntensity; }
    
protected:
    // Compile shader from source
    static GLuint compileShader(GLenum type, const char* source);
    
    // Link shader program
    static GLuint linkProgram(GLuint vertShader, GLuint fragShader);
    
    // Vertex shader source (default fullscreen quad)
    virtual const char* vertexSource() const;
    
    // Fragment shader source (must be provided by subclass)
    virtual const char* fragmentSource() const = 0;
    
    GLuint m_program = 0;
    GLuint m_vertexBuffer = 0;
    
    GLint m_uniformTexture = -1;
    GLint m_uniformIntensity = -1;
    GLint m_uniformResolution = -1;

    float m_intensity = 1.0f;

    bool m_enabled = true;
    bool m_initialized = false;
};

/**
 * Grayscale effect - converts color to luminance
 */
class GrayscaleEffect : public ShaderEffect {
public:
    const char* name() const override { return "grayscale"; }
    const char* fragmentSource() const override;
    
    // Luminance weights (default: Rec. 709)
    void setWeights(float r, float g, float b);
    
private:
    float m_weightR = 0.2126f;
    float m_weightG = 0.7152f;
    float m_weightB = 0.0722f;
};

/**
 * Negative/Invert effect - inverts colors
 */
class NegativeEffect : public ShaderEffect {
public:
    const char* name() const override { return "negative"; }
    const char* fragmentSource() const override;
    
    // Invert alpha channel too?
    void setInvertAlpha(bool invert) { m_invertAlpha = invert; }
    bool invertAlpha() const { return m_invertAlpha; }
    
private:
    bool m_invertAlpha = false;
};

/**
 * Effect state for tracking enabled effects
 */
struct EffectState {
    bool grayscaleEnabled = false;
    bool negativeEnabled = false;
    float grayscaleIntensity = 1.0f;
    float negativeIntensity = 1.0f;
};

} // namespace havel
