// Advanced Shader Effects Implementation

#include "AdvancedShaderEffects.hpp"
#include <cstring>

namespace havel {

// ============================================================================
// Blur Effect
// ============================================================================

const char* BlurEffect::fragmentSource() const {
    return R"(
        precision mediump float;

        varying vec2 v_texCoord;
        uniform sampler2D u_texture;
        uniform float u_intensity;
        uniform vec2 u_resolution;
        uniform float u_radius;
        uniform int u_direction;

        void main() {
            vec2 pixel = 1.0 / u_resolution;
            vec4 color = vec4(0.0);
            float totalWeight = 0.0;
            
            // Gaussian weights
            float radius = u_radius * u_intensity;
            
            for (float i = -radius; i <= radius; i++) {
                float weight = exp(-0.5 * (i * i) / (radius * radius));
                vec2 offset;
                if (u_direction == 0) {
                    offset = vec2(i * pixel.x, 0.0);
                } else {
                    offset = vec2(0.0, i * pixel.y);
                }
                color += texture2D(u_texture, v_texCoord + offset) * weight;
                totalWeight += weight;
            }
            
            gl_FragColor = color / totalWeight;
        }
    )";
}

// ============================================================================
// Bloom Effect
// ============================================================================

const char* BloomEffect::fragmentSource() const {
    return R"(
        precision mediump float;

        varying vec2 v_texCoord;
        uniform sampler2D u_texture;
        uniform float u_threshold;
        uniform float u_intensity;

        void main() {
            vec4 color = texture2D(u_texture, v_texCoord);
            
            // Extract bright areas
            float luminance = dot(color.rgb, vec3(0.299, 0.587, 0.114));
            float bloom = max(0.0, luminance - u_threshold) / (1.0 - u_threshold);
            
            // Add bloom glow
            color.rgb += bloom * u_intensity;
            
            gl_FragColor = color;
        }
    )";
}

// ============================================================================
// Sharpen Effect
// ============================================================================

const char* SharpenEffect::fragmentSource() const {
    return R"(
        precision mediump float;

        varying vec2 v_texCoord;
        uniform sampler2D u_texture;
        uniform float u_intensity;
        uniform vec2 u_resolution;

        void main() {
            vec2 pixel = 1.0 / u_resolution;
            vec4 center = texture2D(u_texture, v_texCoord);
            
            // Get neighboring pixels
            vec4 left = texture2D(u_texture, v_texCoord + vec2(-pixel.x, 0.0));
            vec4 right = texture2D(u_texture, v_texCoord + vec2(pixel.x, 0.0));
            vec4 top = texture2D(u_texture, v_texCoord + vec2(0.0, -pixel.y));
            vec4 bottom = texture2D(u_texture, v_texCoord + vec2(0.0, pixel.y));
            
            // Sharpening kernel
            vec4 neighbors = (left + right + top + bottom) * 0.25;
            vec4 sharpened = center + (center - neighbors) * u_intensity;
            
            gl_FragColor = sharpened;
        }
    )";
}

// ============================================================================
// Chromatic Aberration Effect
// ============================================================================

const char* ChromaticAberrationEffect::fragmentSource() const {
    return R"(
        precision mediump float;

        varying vec2 v_texCoord;
        uniform sampler2D u_texture;
        uniform float u_amount;

        void main() {
            vec2 center = vec2(0.5, 0.5);
            vec2 dir = v_texCoord - center;
            float dist = length(dir);
            
            // Offset RGB channels
            float rOffset = u_amount * dist;
            float bOffset = -u_amount * dist;
            
            float r = texture2D(u_texture, v_texCoord + dir * rOffset).r;
            float g = texture2D(u_texture, v_texCoord).g;
            float b = texture2D(u_texture, v_texCoord + dir * bOffset).b;
            
            gl_FragColor = vec4(r, g, b, 1.0);
        }
    )";
}

// ============================================================================
// Vignette Effect
// ============================================================================

const char* VignetteEffect::fragmentSource() const {
    return R"(
        precision mediump float;

        varying vec2 v_texCoord;
        uniform sampler2D u_texture;
        uniform float u_darkness;
        uniform float u_size;

        void main() {
            vec4 color = texture2D(u_texture, v_texCoord);
            
            // Calculate distance from center
            vec2 center = vec2(0.5, 0.5);
            float dist = distance(v_texCoord, center);
            
            // Apply vignette
            float vignette = smoothstep(u_size, u_size - 0.3, dist);
            color.rgb *= mix(1.0, 1.0 - u_darkness, vignette);
            
            gl_FragColor = color;
        }
    )";
}

// ============================================================================
// Pixelate Effect
// ============================================================================

const char* PixelateEffect::fragmentSource() const {
    return R"(
        precision mediump float;

        varying vec2 v_texCoord;
        uniform sampler2D u_texture;
        uniform float u_pixelSize;
        uniform vec2 u_resolution;

        void main() {
            vec2 pixel = 1.0 / u_resolution;
            vec2 coord = v_texCoord * u_resolution;
            
            // Snap to pixel grid
            coord = floor(coord / u_pixelSize) * u_pixelSize;
            
            vec2 uv = coord / u_resolution + pixel * u_pixelSize * 0.5;
            gl_FragColor = texture2D(u_texture, uv);
        }
    )";
}

// ============================================================================
// Scanline Effect
// ============================================================================

const char* ScanlineEffect::fragmentSource() const {
    return R"(
        precision mediump float;

        varying vec2 v_texCoord;
        uniform sampler2D u_texture;
        uniform float u_intensity;
        uniform float u_spacing;

        void main() {
            vec4 color = texture2D(u_texture, v_texCoord);
            
            // Create scanline pattern
            float scanline = sin(v_texCoord.y * u_resolution.y / u_spacing) * 0.5 + 0.5;
            float blend = mix(1.0, 1.0 - u_intensity, scanline);
            
            color.rgb *= blend;
            gl_FragColor = color;
        }
    )";
}

// ============================================================================
// Color Matrix Effect
// ============================================================================

const char* ColorMatrixEffect::fragmentSource() const {
    return R"(
        precision mediump float;

        varying vec2 v_texCoord;
        uniform sampler2D u_texture;
        uniform float u_brightness;
        uniform float u_contrast;
        uniform float u_saturation;

        void main() {
            vec4 color = texture2D(u_texture, v_texCoord);
            
            // Apply brightness
            color.rgb *= u_brightness;
            
            // Apply contrast
            color.rgb = (color.rgb - 0.5) * u_contrast + 0.5;
            
            // Apply saturation
            float luminance = dot(color.rgb, vec3(0.299, 0.587, 0.114));
            color.rgb = mix(vec3(luminance), color.rgb, u_saturation);
            
            gl_FragColor = color;
        }
    )";
}

void ColorMatrixEffect::setMatrix(const float* matrix) {
    std::memcpy(m_matrix, matrix, sizeof(m_matrix));
}

} // namespace havel
