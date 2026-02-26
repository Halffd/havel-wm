#include "RenderPipeline.hpp"
#include <wm/render_c.h>
#include <cstring>
#include <algorithm>
#include <cstdio>

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
    
    if (!m_pipeline) {
        return false;
    }
    
    // Get output dimensions from C wrapper
    // For now, use reasonable defaults - actual dimensions come from output events
    m_width = 1920;
    m_height = 1080;
    
    // Initialize shader effects
    m_grayscaleEffect = std::make_unique<GrayscaleEffect>();
    m_negativeEffect = std::make_unique<NegativeEffect>();
    
    printf("[RenderPipeline] Initialized (%dx%d)\n", m_width, m_height);
    m_initialized = true;
    return true;
}

void RenderPipeline::cleanup() {
    if (m_pipeline) {
        havel_render_pipeline_destroy(m_pipeline);
        m_pipeline = nullptr;
    }
    
    m_grayscaleEffect.reset();
    m_negativeEffect.reset();
    m_initialized = false;
}

void RenderPipeline::setGrayscaleEnabled(bool enabled) {
    m_grayscaleEnabled = enabled;
    if (m_grayscaleEffect) {
        m_grayscaleEffect->setEnabled(enabled);
    }
    printf("[RenderPipeline] Grayscale %s\n", enabled ? "enabled" : "disabled");
}

void RenderPipeline::setNegativeEnabled(bool enabled) {
    m_negativeEnabled = enabled;
    if (m_negativeEffect) {
        m_negativeEffect->setEnabled(enabled);
    }
    printf("[RenderPipeline] Negative %s\n", enabled ? "enabled" : "disabled");
}

void RenderPipeline::render(void* scene, void* sceneOutput) {
    if (!m_initialized || !m_pipeline || !sceneOutput) {
        // Fallback to direct commit
        if (m_pipeline) {
            havel_render_pipeline_render(m_pipeline,
                static_cast<struct wlr_scene*>(scene),
                static_cast<struct wlr_scene_output*>(sceneOutput)
            );
        }
        return;
    }
    
    // Check if any effects are enabled
    bool hasEffects = m_effectsEnabled && (m_grayscaleEnabled || m_negativeEnabled);
    
    if (!hasEffects) {
        // No effects, just commit directly
        havel_render_pipeline_render(m_pipeline,
            static_cast<struct wlr_scene*>(scene),
            static_cast<struct wlr_scene_output*>(sceneOutput)
        );
        return;
    }
    
    // Effects would be applied via render pass in wlr_bridge.c
    // This is a stub - actual effect application happens in the render pass
    printf("[RenderPipeline] Effects active (grayscale=%d, negative=%d)\n",
           m_grayscaleEnabled, m_negativeEnabled);
    
    havel_render_pipeline_render(m_pipeline,
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
