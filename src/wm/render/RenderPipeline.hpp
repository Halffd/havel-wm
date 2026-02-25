#pragma once

#include <wm/render_c.h>
#include <wm/Types.hpp>
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
 */
class RenderPipeline {
public:
    RenderPipeline();
    ~RenderPipeline();
    
    bool initialize(void* output, void* renderer);
    void cleanup();
    bool isInitialized() const { return m_initialized; }
    
    void addEffect(std::unique_ptr<RenderEffect> effect);
    void removeEffect(const char* name);
    RenderEffect* getEffect(const char* name);
    void setEffectsEnabled(bool enabled);
    
    void render(void* scene, void* sceneOutput);
    
    int width() const { return m_width; }
    int height() const { return m_height; }
    
    void setZoom(float zoom);
    float zoom() const { return m_zoom; }
    
    void setGamma(float gamma);
    void setBrightness(float brightness);
    
private:
    havel_render_pipeline_t* m_pipeline = nullptr;
    std::vector<std::unique_ptr<RenderEffect>> m_effects;
    
    int m_width = 0;
    int m_height = 0;
    float m_zoom = 1.0f;
    bool m_initialized = false;
    bool m_effectsEnabled = true;
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
