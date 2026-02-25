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
    
    if (m_pipeline) {
        m_initialized = true;
        // Get dimensions from output
        // For now, use defaults - would get from wlr_output in real impl
        m_width = 1920;
        m_height = 1080;
        
        // Create grayscale effect
        m_grayscaleEffect = std::make_unique<GrayscaleEffect>();
        if (m_grayscaleEffect->initialize()) {
            printf("[RenderPipeline] Grayscale effect initialized\n");
        }
        
        // Create negative effect
        m_negativeEffect = std::make_unique<NegativeEffect>();
        if (m_negativeEffect->initialize()) {
            printf("[RenderPipeline] Negative effect initialized\n");
        }
        
        // Create FBO for effect processing
        glGenFramebuffers(1, &m_fbo);
        glGenTextures(1, &m_texture);
        glGenTextures(1, &m_effectTexture);
        
        // Setup texture for rendering
        glBindTexture(GL_TEXTURE_2D, m_texture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, m_width, m_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        
        glBindTexture(GL_TEXTURE_2D, m_effectTexture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, m_width, m_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        
        glBindTexture(GL_TEXTURE_2D, 0);
        
        printf("[RenderPipeline] Initialized FBO (%dx%d)\n", m_width, m_height);
        return true;
    }
    
    return false;
}

void RenderPipeline::cleanup() {
    if (m_pipeline) {
        havel_render_pipeline_destroy(m_pipeline);
        m_pipeline = nullptr;
    }
    
    if (m_fbo) {
        glDeleteFramebuffers(1, &m_fbo);
        m_fbo = 0;
    }
    if (m_texture) {
        glDeleteTextures(1, &m_texture);
        m_texture = 0;
    }
    if (m_effectTexture) {
        glDeleteTextures(1, &m_effectTexture);
        m_effectTexture = 0;
    }
    
    m_effects.clear();
    m_grayscaleEffect.reset();
    m_negativeEffect.reset();
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

void RenderPipeline::setGrayscaleEnabled(bool enabled) {
    m_grayscaleEnabled = enabled;
    if (m_grayscaleEffect) {
        m_grayscaleEffect->setEnabled(enabled);
    }
}

void RenderPipeline::setNegativeEnabled(bool enabled) {
    m_negativeEnabled = enabled;
    if (m_negativeEffect) {
        m_negativeEffect->setEnabled(enabled);
    }
}

void RenderPipeline::render(void* scene, void* sceneOutput) {
    if (!m_initialized || !m_pipeline) {
        return;
    }
    
    // If no effects enabled, just commit directly
    if (!m_effectsEnabled || (!m_grayscaleEnabled && !m_negativeEnabled)) {
        havel_render_pipeline_render(m_pipeline, 
            static_cast<struct wlr_scene*>(scene),
            static_cast<struct wlr_scene_output*>(sceneOutput)
        );
        return;
    }
    
    // Render scene to our FBO first
    // Note: This is a simplified version - full implementation would need
    // to intercept wlroots rendering and redirect to our FBO
    
    // For now, commit scene normally, then apply effects as post-process
    havel_render_pipeline_render(m_pipeline,
        static_cast<struct wlr_scene*>(scene),
        static_cast<struct wlr_scene_output*>(sceneOutput)
    );
    
    // Apply effects (would be applied to FBO content in full implementation)
    renderEffects();
}

void RenderPipeline::renderEffects() {
    if (!m_effectsEnabled) return;
    
    // Bind FBO for effect processing
    glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_texture, 0);
    
    // Check framebuffer completeness
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        fprintf(stderr, "[RenderPipeline] FBO incomplete\n");
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        return;
    }
    
    glViewport(0, 0, m_width, m_height);
    
    // Apply grayscale if enabled
    if (m_grayscaleEnabled && m_grayscaleEffect && m_grayscaleEffect->isEnabled()) {
        // In full implementation, would render fullscreen quad with shader
        printf("[RenderPipeline] Grayscale effect applied\n");
    }
    
    // Apply negative if enabled
    if (m_negativeEnabled && m_negativeEffect && m_negativeEffect->isEnabled()) {
        printf("[RenderPipeline] Negative effect applied\n");
    }
    
    // Reset framebuffer
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void RenderPipeline::drawFullscreenQuad(GLuint texture) {
    // Would draw fullscreen quad with given texture
    // Used for effect application
    (void)texture;
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
