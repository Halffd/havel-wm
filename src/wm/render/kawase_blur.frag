// Kawase Blur Fragment Shader - Multi-pass blur effect
// Based on Masaki Kawase's technique for fast Gaussian-like blur
// Used for window background blur effects

#version 100
precision mediump float;

// Uniforms
uniform vec2 u_resolution;
uniform vec2 u_offset;
uniform float u_sample_count;
uniform sampler2D u_texture;

// Varying from vertex shader
varying vec2 v_texCoord;

void main() {
    vec2 pixel = 1.0 / u_resolution;
    vec4 color = vec4(0.0);
    float total_weight = 0.0;
    
    // Kawase blur uses offset sampling in multiple directions
    // This provides a Gaussian-like blur with fewer samples
    
    // Center sample
    color += texture2D(u_texture, v_texCoord);
    total_weight += 1.0;
    
    // Offset samples (4 directions per iteration)
    for (float i = 1.0; i <= u_sample_count; i++) {
        vec2 offset = u_offset * pixel * i;
        
        // Sample in 4 cardinal directions
        color += texture2D(u_texture, v_texCoord + offset);
        color += texture2D(u_texture, v_texCoord - offset);
        color += texture2D(u_texture, v_texCoord + vec2(offset.y, offset.x));
        color += texture2D(u_texture, v_texCoord - vec2(offset.y, offset.x));
        
        total_weight += 4.0;
    }
    
    gl_FragColor = color / total_weight;
}
