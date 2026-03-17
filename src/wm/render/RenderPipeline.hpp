#pragma once

#include <wm/render_c.h>
#include <wm/Types.hpp>
#include <wm/render/ShaderEffect.hpp>
#include <wm/render/BlurShader.hpp>
#include <wm/render/OverlayRenderer.hpp>
#include <vector>
#include <memory>

namespace havel {

/**
 * Render pipeline wrapper around C implementation
 *
 * Supports:
 * - Shader effects (grayscale, negative, blur)
 * - FBO-based post-processing
 * - Effect chaining
 * - Overlay rendering
 */
class RenderPipeline {
public:
    RenderPipeline();
    ~RenderPipeline();

    bool initialize(void* output, void* renderer);
    void cleanup();
    bool isInitialized() const { return m_initialized; }

    // Effect toggles
    void setGrayscaleEnabled(bool enabled);
    void setNegativeEnabled(bool enabled);
    void setBlurEnabled(bool enabled);

    bool isGrayscaleEnabled() const { return m_grayscaleEnabled; }
    bool isNegativeEnabled() const { return m_negativeEnabled; }
    bool isBlurEnabled() const { return m_blurEnabled; }

    // Render scene through pipeline with effects
    void render(void* scene, void* sceneOutput);

    // Overlay rendering (for Alt-Tab, Overview, etc.)
    OverlayRenderer* overlayRenderer() { return m_overlayRenderer.get(); }

    int width() const { return m_width; }
    int height() const { return m_height; }

    void setZoom(float zoom);
    float zoom() const { return m_zoom; }

    void setGamma(float gamma);
    void setBrightness(float brightness);

    // Blur settings
    void setBlurRadius(int radius);
    int getBlurRadius() const { return m_blurRadius; }

private:
    void renderEffects();
    void drawFullscreenQuad(GLuint texture);
    void createFBO();
    void destroyFBO();

    havel_render_pipeline_t* m_pipeline = nullptr;
    std::unique_ptr<GrayscaleEffect> m_grayscaleEffect;
    std::unique_ptr<NegativeEffect> m_negativeEffect;
    std::unique_ptr<BlurShader> m_blurShader;
    std::unique_ptr<OverlayRenderer> m_overlayRenderer;

    // FBOs for effect processing
    GLuint m_fbo = 0;
    GLuint m_fboBlur = 0;
    GLuint m_texture = 0;
    GLuint m_textureBlur = 0;

    int m_width = 0;
    int m_height = 0;
    float m_zoom = 1.0f;
    bool m_initialized = false;
    bool m_grayscaleEnabled = false;
    bool m_negativeEnabled = false;
    bool m_blurEnabled = false;
    int m_blurRadius = 3;
};

} // namespace havel
