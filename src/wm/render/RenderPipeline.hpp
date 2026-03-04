#pragma once

#include <wm/render_c.h>
#include <wm/Types.hpp>
#include <wm/render/ShaderEffect.hpp>
#include "AdvancedShaderEffects.hpp"
#include <wm/render/OverlayRenderer.hpp>
#include <vector>
#include <memory>

namespace havel {

/**
 * Render effect base class
 */
class RenderEffect {
public:
    virtual ~RenderEffect() = default;
    virtual const char* name() const = 0;
    virtual bool initialize() = 0;
    virtual void render() = 0;
    virtual void cleanup() = 0;

    void setEnabled(bool enabled) { m_enabled = enabled; }
    bool isEnabled() const { return m_enabled; }

protected:
    bool m_enabled = true;
};

/**
 * Render pipeline wrapper around C implementation
 * 
 * Supports:
 * - Multiple shader effects (blur, bloom, sharpen, etc.)
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

    // Effect management
    void addEffect(std::unique_ptr<RenderEffect> effect);
    void removeEffect(const char* name);
    RenderEffect* getEffect(const char* name);
    void setEffectsEnabled(bool enabled);

    // Quick effect toggles
    void setGrayscaleEnabled(bool enabled);
    void setNegativeEnabled(bool enabled);
    void setBlurEnabled(bool enabled);
    void setBloomEnabled(bool enabled);
    void setSharpenEnabled(bool enabled);
    void setVignetteEnabled(bool enabled);
    
    bool isGrayscaleEnabled() const { return m_grayscaleEnabled; }
    bool isNegativeEnabled() const { return m_negativeEnabled; }
    bool isBlurEnabled() const { return m_blurEnabled; }
    bool isBloomEnabled() const { return m_bloomEnabled; }
    bool isSharpenEnabled() const { return m_sharpenEnabled; }
    bool isVignetteEnabled() const { return m_vignetteEnabled; }

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

private:
    void renderEffects();
    void drawFullscreenQuad(GLuint texture);
    void createFBO();
    void destroyFBO();

    havel_render_pipeline_t* m_pipeline = nullptr;
    std::vector<std::unique_ptr<RenderEffect>> m_effects;
    std::unique_ptr<GrayscaleEffect> m_grayscaleEffect;
    std::unique_ptr<NegativeEffect> m_negativeEffect;
    std::unique_ptr<BlurEffect> m_blurEffect;
    std::unique_ptr<BloomEffect> m_bloomEffect;
    std::unique_ptr<SharpenEffect> m_sharpenEffect;
    std::unique_ptr<VignetteEffect> m_vignetteEffect;
    std::unique_ptr<OverlayRenderer> m_overlayRenderer;

    // FBO for effect processing
    GLuint m_fbo = 0;
    GLuint m_texture = 0;
    GLuint m_effectTexture = 0;

    int m_width = 0;
    int m_height = 0;
    float m_zoom = 1.0f;
    bool m_initialized = false;
    bool m_effectsEnabled = true;
    bool m_grayscaleEnabled = false;
    bool m_negativeEnabled = false;
    bool m_blurEnabled = false;
    bool m_bloomEnabled = false;
    bool m_sharpenEnabled = false;
    bool m_vignetteEnabled = false;
};

/**
 * Passthrough effect for testing
 */
class PassthroughEffect : public RenderEffect {
public:
    const char* name() const override { return "passthrough"; }
    bool initialize() override { return true; }
    void render() override {}
    void cleanup() override {}
};

} // namespace havel
