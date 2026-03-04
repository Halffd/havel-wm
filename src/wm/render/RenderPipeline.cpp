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
    m_width = 1920;
    m_height = 1080;

    // Create FBO for effect processing
    createFBO();

    // Initialize shader effects
    m_grayscaleEffect = std::make_unique<GrayscaleEffect>();
    m_negativeEffect = std::make_unique<NegativeEffect>();
    m_blurEffect = std::make_unique<BlurEffect>();
    m_bloomEffect = std::make_unique<BloomEffect>();
    m_sharpenEffect = std::make_unique<SharpenEffect>();
    m_vignetteEffect = std::make_unique<VignetteEffect>();

    // Initialize all effects
    m_grayscaleEffect->initialize();
    m_negativeEffect->initialize();
    m_blurEffect->initialize();
    m_bloomEffect->initialize();
    m_sharpenEffect->initialize();
    m_vignetteEffect->initialize();

    // Initialize overlay renderer
    m_overlayRenderer = std::make_unique<OverlayRenderer>();
    if (!m_overlayRenderer->initialize()) {
        fprintf(stderr, "[RenderPipeline] Warning: Overlay renderer failed to initialize\n");
        m_overlayRenderer.reset();
    }

    // Set overlay renderer pointer in C pipeline struct
    if (m_pipeline && m_overlayRenderer) {
        havel_render_pipeline_set_overlay_renderer(m_pipeline, m_overlayRenderer.get());
    }

    printf("[RenderPipeline] Initialized (%dx%d) with %d shader effects\n", 
           m_width, m_height, 6);
    m_initialized = true;
    return true;
}

void RenderPipeline::cleanup() {
    destroyFBO();
    
    if (m_pipeline) {
        havel_render_pipeline_destroy(m_pipeline);
        m_pipeline = nullptr;
    }

    m_grayscaleEffect.reset();
    m_negativeEffect.reset();
    m_blurEffect.reset();
    m_bloomEffect.reset();
    m_sharpenEffect.reset();
    m_vignetteEffect.reset();
    m_overlayRenderer.reset();
    m_initialized = false;
}

void RenderPipeline::createFBO() {
    // Create framebuffer object for post-processing
    glGenFramebuffers(1, &m_fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);

    // Create texture for rendering
    glGenTextures(1, &m_texture);
    glBindTexture(GL_TEXTURE_2D, m_texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, m_width, m_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_texture, 0);

    // Check framebuffer completeness
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        fprintf(stderr, "[RenderPipeline] FBO incomplete\n");
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glBindTexture(GL_TEXTURE_2D, 0);

    printf("[RenderPipeline] FBO created (%dx%d)\n", m_width, m_height);
}

void RenderPipeline::destroyFBO() {
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

void RenderPipeline::setBlurEnabled(bool enabled) {
    m_blurEnabled = enabled;
    if (m_blurEffect) {
        m_blurEffect->setEnabled(enabled);
    }
    printf("[RenderPipeline] Blur %s\n", enabled ? "enabled" : "disabled");
}

void RenderPipeline::setBloomEnabled(bool enabled) {
    m_bloomEnabled = enabled;
    if (m_bloomEffect) {
        m_bloomEffect->setEnabled(enabled);
    }
    printf("[RenderPipeline] Bloom %s\n", enabled ? "enabled" : "disabled");
}

void RenderPipeline::setSharpenEnabled(bool enabled) {
    m_sharpenEnabled = enabled;
    if (m_sharpenEffect) {
        m_sharpenEffect->setEnabled(enabled);
    }
    printf("[RenderPipeline] Sharpen %s\n", enabled ? "enabled" : "disabled");
}

void RenderPipeline::setVignetteEnabled(bool enabled) {
    m_vignetteEnabled = enabled;
    if (m_vignetteEffect) {
        m_vignetteEffect->setEnabled(enabled);
    }
    printf("[RenderPipeline] Vignette %s\n", enabled ? "enabled" : "disabled");
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
    bool hasEffects = m_effectsEnabled && (
        m_grayscaleEnabled || m_negativeEnabled || 
        m_blurEnabled || m_bloomEnabled || 
        m_sharpenEnabled || m_vignetteEnabled
    );

    if (!hasEffects) {
        // No effects, just commit directly
        havel_render_pipeline_render(m_pipeline,
            static_cast<struct wlr_scene*>(scene),
            static_cast<struct wlr_scene_output*>(sceneOutput)
        );
        return;
    }

    // Apply effects via render pipeline
    printf("[RenderPipeline] Applying effects (gray=%d, neg=%d, blur=%d, bloom=%d, sharp=%d, vig=%d)\n",
           m_grayscaleEnabled, m_negativeEnabled, m_blurEnabled, 
           m_bloomEnabled, m_sharpenEnabled, m_vignetteEnabled);

    // Set effect parameters
    if (m_grayscaleEffect) {
        m_grayscaleEffect->setIntensity(1.0f);
    }
    if (m_negativeEffect) {
        m_negativeEffect->setIntensity(1.0f);
    }
    if (m_blurEffect) {
        m_blurEffect->setIntensity(1.0f);
        m_blurEffect->setRadius(5.0f);
    }
    if (m_bloomEffect) {
        m_bloomEffect->setIntensity(0.5f);
        m_bloomEffect->setThreshold(0.8f);
    }
    if (m_sharpenEffect) {
        m_sharpenEffect->setIntensity(0.5f);
    }
    if (m_vignetteEffect) {
        m_vignetteEffect->setIntensity(1.0f);
        m_vignetteEffect->setDarkness(0.5f);
        m_vignetteEffect->setSize(0.7f);
    }

    // Render with effects
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
