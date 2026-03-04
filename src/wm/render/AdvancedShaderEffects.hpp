// Advanced Shader Effects - Blur, Bloom, Sharpen, Chromatic Aberration

#pragma once

#include <wm/render/ShaderEffect.hpp>

namespace havel {

/**
 * Gaussian Blur Effect
 * 
 * Multi-pass Gaussian blur for smooth blurring.
 * Requires two passes (horizontal + vertical) for proper Gaussian blur.
 */
class BlurEffect : public ShaderEffect {
public:
    const char* name() const override { return "blur"; }
    const char* fragmentSource() const override;

    // Blur radius in pixels
    void setRadius(float radius) { m_radius = radius; }
    float radius() const { return m_radius; }

    // Blur direction (0=horizontal, 1=vertical)
    void setDirection(int dir) { m_direction = dir; }
    int direction() const { return m_direction; }

private:
    float m_radius = 5.0f;
    int m_direction = 0;  // 0=horizontal, 1=vertical
};

/**
 * Bloom Effect
 * 
 * Creates glow around bright areas.
 * Uses threshold + blur + additive blend.
 */
class BloomEffect : public ShaderEffect {
public:
    const char* name() const override { return "bloom"; }
    const char* fragmentSource() const override;

    // Bloom threshold (pixels brighter than this glow)
    void setThreshold(float threshold) { m_threshold = threshold; }
    float threshold() const { return m_threshold; }

    // Bloom intensity
    void setIntensity(float intensity) { m_intensity = intensity; }
    float intensity() const { return m_intensity; }

private:
    float m_threshold = 0.8f;
    float m_intensity = 0.5f;
};

/**
 * Sharpen Effect
 * 
 * Enhances edge contrast for sharper appearance.
 */
class SharpenEffect : public ShaderEffect {
public:
    const char* name() const override { return "sharpen"; }
    const char* fragmentSource() const override;

    // Sharpen amount
    void setAmount(float amount) { m_amount = amount; }
    float amount() const { return m_amount; }

private:
    float m_amount = 0.5f;
};

/**
 * Chromatic Aberration Effect
 * 
 * Simulates lens color fringing by offsetting RGB channels.
 */
class ChromaticAberrationEffect : public ShaderEffect {
public:
    const char* name() const override { return "chromatic_aberration"; }
    const char* fragmentSource() const override;

    // Aberration amount (offset in UV space)
    void setAmount(float amount) { m_amount = amount; }
    float amount() const { return m_amount; }

private:
    float m_amount = 0.002f;
};

/**
 * Vignette Effect
 * 
 * Darkens corners for cinematic look.
 */
class VignetteEffect : public ShaderEffect {
public:
    const char* name() const override { return "vignette"; }
    const char* fragmentSource() const override;

    // Vignette darkness
    void setDarkness(float darkness) { m_darkness = darkness; }
    float darkness() const { return m_darkness; }

    // Vignette size (0-1)
    void setSize(float size) { m_size = size; }
    float size() const { return m_size; }

private:
    float m_darkness = 0.5f;
    float m_size = 0.7f;
};

/**
 * Pixelate Effect
 * 
 * Creates retro pixel art look by reducing resolution.
 */
class PixelateEffect : public ShaderEffect {
public:
    const char* name() const override { return "pixelate"; }
    const char* fragmentSource() const override;

    // Pixel size (larger = more pixelated)
    void setPixelSize(float size) { m_pixelSize = size; }
    float pixelSize() const { return m_pixelSize; }

private:
    float m_pixelSize = 8.0f;
};

/**
 * Scanline Effect
 * 
 * Simulates CRT monitor scanlines.
 */
class ScanlineEffect : public ShaderEffect {
public:
    const char* name() const override { return "scanline"; }
    const char* fragmentSource() const override;

    // Scanline intensity
    void setIntensity(float intensity) { m_intensity = intensity; }
    float intensity() const { return m_intensity; }

    // Scanline spacing
    void setSpacing(float spacing) { m_spacing = spacing; }
    float spacing() const { return m_spacing; }

private:
    float m_intensity = 0.3f;
    float m_spacing = 2.0f;
};

/**
 * Color Matrix Effect
 * 
 * Apply color transformation matrix for advanced color grading.
 */
class ColorMatrixEffect : public ShaderEffect {
public:
    const char* name() const override { return "color_matrix"; }
    const char* fragmentSource() const override;

    // Set color matrix (4x4)
    void setMatrix(const float* matrix);
    
    // Set brightness/contrast/saturation
    void setBrightness(float b) { m_brightness = b; }
    void setContrast(float c) { m_contrast = c; }
    void setSaturation(float s) { m_saturation = s; }

private:
    float m_matrix[16];
    float m_brightness = 1.0f;
    float m_contrast = 1.0f;
    float m_saturation = 1.0f;
};

} // namespace havel
