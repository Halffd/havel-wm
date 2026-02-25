#include "RenderPipeline.hpp"
#include <wm/render_c.h>
#include <cstring>
#include <algorithm>

namespace havel {

RenderPipeline::RenderPipeline() = default;

RenderPipeline::~RenderPipeline() {
    cleanup();
}

bool RenderPipeline::initialize(void* output, void* renderer) {
    if (m_initialized) {
        return true;
    }
    
    m_pipeline = havel_render_pipeline_create(
        static_cast<struct wlr_output*>(output),
        static_cast<struct wlr_renderer*>(renderer)
    );
    
    if (m_pipeline) {
        m_initialized = true;
        // Default dimensions - would get from output in real implementation
        m_width = 1920;
        m_height = 1080;
        return true;
    }
    
    return false;
}

void RenderPipeline::cleanup() {
    if (m_pipeline) {
        havel_render_pipeline_destroy(m_pipeline);
        m_pipeline = nullptr;
    }
    
    m_effects.clear();
    m_initialized = false;
}

void RenderPipeline::addEffect(std::unique_ptr<RenderEffect> effect) {
    if (!effect) return;
    
    if (effect->initialize()) {
        m_effects.push_back(std::move(effect));
    }
}

void RenderPipeline::removeEffect(const char* name) {
    m_effects.erase(
        std::remove_if(m_effects.begin(), m_effects.end(),
            [name](const std::unique_ptr<RenderEffect>& e) {
                return strcmp(e->name(), name) == 0;
            }),
        m_effects.end()
    );
}

RenderEffect* RenderPipeline::getEffect(const char* name) {
    for (auto& effect : m_effects) {
        if (strcmp(effect->name(), name) == 0) {
            return effect.get();
        }
    }
    return nullptr;
}

void RenderPipeline::setEffectsEnabled(bool enabled) {
    m_effectsEnabled = enabled;
    if (m_pipeline) {
        havel_render_pipeline_set_effects_enabled(m_pipeline, enabled);
    }
}

void RenderPipeline::render(void* scene, void* sceneOutput) {
    if (!m_initialized || !m_pipeline) {
        return;
    }
    
    // For now, effects are not applied through the C wrapper
    // Real implementation would render scene to FBO, apply effects, present
    
    havel_render_pipeline_render(
        m_pipeline, 
        static_cast<struct wlr_scene*>(scene),
        static_cast<struct wlr_scene_output*>(sceneOutput)
    );
}

void RenderPipeline::setZoom(float zoom) {
    m_zoom = zoom;
    if (m_pipeline) {
        havel_render_pipeline_set_zoom(m_pipeline, zoom);
    }
}

void RenderPipeline::setGamma(float gamma) {
    if (m_pipeline) {
        havel_render_pipeline_set_gamma(m_pipeline, gamma);
    }
}

void RenderPipeline::setBrightness(float brightness) {
    if (m_pipeline) {
        havel_render_pipeline_set_brightness(m_pipeline, brightness);
    }
}

} // namespace havel
