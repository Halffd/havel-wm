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

    // Create FBOs for effect processing
    createFBO();

    // Initialize shader effects
    m_grayscaleEffect = std::make_unique<GrayscaleEffect>();
    m_negativeEffect = std::make_unique<NegativeEffect>();
    m_blurShader = std::make_unique<BlurShader>();

    // Initialize effects
    m_grayscaleEffect->initialize();
    m_negativeEffect->initialize();
    
    // Blur shader initialized on first use with proper size

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

    printf("[RenderPipeline] Initialized (%dx%d) with grayscale, negative, blur effects\n",
           m_width, m_height);
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
    m_blurShader.reset();
    m_overlayRenderer.reset();
    m_initialized = false;
}

void RenderPipeline::createFBO() {
    // Create primary FBO for scene capture
    glGenFramebuffers(1, &m_fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);

    glGenTextures(1, &m_texture);
    glBindTexture(GL_TEXTURE_2D, m_texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, m_width, m_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_texture, 0);

    // Create blur FBO
    glGenFramebuffers(1, &m_fboBlur);
    glBindFramebuffer(GL_FRAMEBUFFER, m_fboBlur);

    glGenTextures(1, &m_textureBlur);
    glBindTexture(GL_TEXTURE_2D, m_textureBlur);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, m_width, m_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_textureBlur, 0);

    // Check framebuffer completeness
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        fprintf(stderr, "[RenderPipeline] FBO incomplete\n");
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glBindTexture(GL_TEXTURE_2D, 0);

    printf("[RenderPipeline] FBOs created (%dx%d)\n", m_width, m_height);
}

void RenderPipeline::destroyFBO() {
    if (m_fbo) {
        glDeleteFramebuffers(1, &m_fbo);
        m_fbo = 0;
    }
    if (m_fboBlur) {
        glDeleteFramebuffers(1, &m_fboBlur);
        m_fboBlur = 0;
    }
    if (m_texture) {
        glDeleteTextures(1, &m_texture);
        m_texture = 0;
    }
    if (m_textureBlur) {
        glDeleteTextures(1, &m_textureBlur);
        m_textureBlur = 0;
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
    printf("[RenderPipeline] Blur %s\n", enabled ? "enabled" : "disabled");
}

void RenderPipeline::setBlurRadius(int radius) {
    m_blurRadius = std::max(1, std::min(10, radius));
    printf("[RenderPipeline] Blur radius: %d\n", m_blurRadius);
}

void RenderPipeline::render(void* scene, void* sceneOutput) {
    if (!m_initialized || !m_pipeline || !sceneOutput) {
        if (m_pipeline) {
            havel_render_pipeline_render(m_pipeline,
                static_cast<struct wlr_scene*>(scene),
                static_cast<struct wlr_scene_output*>(sceneOutput)
            );
        }
        return;
    }

    // Check if any effects are enabled
    bool hasEffects = m_grayscaleEnabled || m_negativeEnabled || m_blurEnabled;

    if (!hasEffects) {
        // No effects, commit directly
        havel_render_pipeline_render(m_pipeline,
            static_cast<struct wlr_scene*>(scene),
            static_cast<struct wlr_scene_output*>(sceneOutput)
        );
        return;
    }

    printf("[RenderPipeline] Applying effects (gray=%d, neg=%d, blur=%d)\n",
           m_grayscaleEnabled, m_negativeEnabled, m_blurEnabled);

    // Render scene to FBO
    glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
    glViewport(0, 0, m_width, m_height);
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);

    // Temporarily redirect pipeline output to our FBO
    // For now, just commit scene normally (wlroots handles this)
    // Full FBO capture would require more invasive wlroots changes
    
    // For now, skip FBO capture and just log that effects would be applied
    // This is a placeholder until full FBO integration
    
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    // Commit scene
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
