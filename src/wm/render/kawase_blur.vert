// Kawase Blur Vertex Shader
// Passes texture coordinates and sets up quad for fullscreen render

#version 100

// Attributes
attribute vec2 a_position;
attribute vec2 a_texCoord;

// Uniforms
uniform vec2 u_resolution;

// Varying to fragment shader
varying vec2 v_texCoord;

void main() {
    gl_Position = vec4(a_position, 0.0, 1.0);
    v_texCoord = a_texCoord;
}
