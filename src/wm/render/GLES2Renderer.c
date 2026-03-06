// GLES2 Renderer Implementation - Fallback for systems without Vulkan

#include "GLES2Renderer.h"
#include <Logger.h>
#include <stdlib.h>
#include <string.h>
#include <GLES2/gl2.h>
#include <EGL/egl.h>

// Forward declare wlroots structures (defined in wlroots headers)
struct wlr_egl {
    EGLDisplay display;
    EGLContext context;
    EGLSurface surface;
    // ... other fields we don't need
};

struct wlr_renderer {
    // ... wlroots renderer
};

// Internal texture structure
struct GLES2Texture {
    GLuint texture;
    uint32_t width;
    uint32_t height;
    bool imported;
};

// Internal renderer structure
struct GLES2Renderer {
    struct wlr_egl* egl;
    struct wlr_renderer* wlr_renderer;
    GLES2RendererConfig config;
    
    // GL state
    GLuint shaderProgram;
    GLuint vertexBuffer;
    GLuint indexBuffer;
    GLint positionLoc;
    GLint texCoordLoc;
    GLint textureLoc;
    GLint gammaLoc;
    GLint brightnessLoc;
    
    // Statistics
    GLES2Texture** textures;
    size_t textureCount;
    size_t textureCapacity;
    GLES2Stats stats;
};

// Vertex shader (GLES2)
static const char* vertex_shader_source =
    "attribute vec2 a_position;\n"
    "attribute vec2 a_texCoord;\n"
    "varying vec2 v_texCoord;\n"
    "void main() {\n"
    "    gl_Position = vec4(a_position, 0.0, 1.0);\n"
    "    v_texCoord = a_texCoord;\n"
    "}\n";

// Fragment shader with gamma/brightness correction
static const char* fragment_shader_source =
    "precision mediump float;\n"
    "varying vec2 v_texCoord;\n"
    "uniform sampler2D u_texture;\n"
    "uniform float u_gamma;\n"
    "uniform float u_brightness;\n"
    "void main() {\n"
    "    vec4 color = texture2D(u_texture, v_texCoord);\n"
    "    color.rgb = pow(color.rgb, vec3(1.0 / u_gamma));\n"
    "    color.rgb *= u_brightness;\n"
    "    gl_FragColor = color;\n"
    "}\n";

// Compile shader
static GLuint compile_shader(GLenum type, const char* source) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, NULL);
    glCompileShader(shader);
    
    GLint compiled;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
    if (!compiled) {
        char log[512];
        glGetShaderInfoLog(shader, sizeof(log), NULL, log);
        LOG_ERROR("[GLES2] Shader compile failed: %s", log);
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

// Create shader program
static GLuint create_program(const char* vertex_src, const char* fragment_src) {
    GLuint vertex_shader = compile_shader(GL_VERTEX_SHADER, vertex_src);
    if (!vertex_shader) return 0;
    
    GLuint fragment_shader = compile_shader(GL_FRAGMENT_SHADER, fragment_src);
    if (!fragment_shader) {
        glDeleteShader(vertex_shader);
        return 0;
    }
    
    GLuint program = glCreateProgram();
    glAttachShader(program, vertex_shader);
    glAttachShader(program, fragment_shader);
    glLinkProgram(program);
    
    GLint linked;
    glGetProgramiv(program, GL_LINK_STATUS, &linked);
    if (!linked) {
        char log[512];
        glGetProgramInfoLog(program, sizeof(log), NULL, log);
        LOG_ERROR("[GLES2] Program link failed: %s", log);
        glDeleteProgram(program);
        return 0;
    }
    
    glDeleteShader(vertex_shader);
    glDeleteShader(fragment_shader);
    return program;
}

// Initialize renderer
GLES2Renderer* gles2_renderer_create(struct wlr_egl* egl, struct wlr_renderer* wlr_renderer,
                                     const GLES2RendererConfig* config) {
    if (!egl || !wlr_renderer) {
        LOG_ERROR("[GLES2] Invalid EGL or renderer");
        return NULL;
    }
    
    // Make context current
    eglMakeCurrent(egl->display, egl->surface, egl->surface, egl->context);
    
    // Check GLES2 availability
    const char* version_str = (const char*)glGetString(GL_VERSION);
    const char* vendor_str = (const char*)glGetString(GL_VENDOR);
    const char* renderer_str = (const char*)glGetString(GL_RENDERER);
    
    LOG_INFO("[GLES2] Version: %s", version_str ? version_str : "unknown");
    LOG_INFO("[GLES2] Vendor: %s", vendor_str ? vendor_str : "unknown");
    LOG_INFO("[GLES2] Renderer: %s", renderer_str ? renderer_str : "unknown");
    
    // Allocate renderer
    GLES2Renderer* renderer = (GLES2Renderer*)calloc(1, sizeof(GLES2Renderer));
    if (!renderer) {
        LOG_ERROR("[GLES2] Failed to allocate renderer");
        return NULL;
    }
    
    renderer->egl = egl;
    renderer->wlr_renderer = wlr_renderer;
    
    if (config) {
        renderer->config = *config;
    } else {
        renderer->config.enableVSync = true;
        renderer->config.enableMSAA = false;
        renderer->config.msaaSamples = 0;
        renderer->config.gamma = 1.0f;
        renderer->config.enableDebug = false;
    }
    
    // Create shader program
    renderer->shaderProgram = create_program(vertex_shader_source, fragment_shader_source);
    if (!renderer->shaderProgram) {
        LOG_ERROR("[GLES2] Failed to create shader program");
        free(renderer);
        return NULL;
    }
    
    // Get attribute/uniform locations
    renderer->positionLoc = glGetAttribLocation(renderer->shaderProgram, "a_position");
    renderer->texCoordLoc = glGetAttribLocation(renderer->shaderProgram, "a_texCoord");
    renderer->textureLoc = glGetUniformLocation(renderer->shaderProgram, "u_texture");
    renderer->gammaLoc = glGetUniformLocation(renderer->shaderProgram, "u_gamma");
    renderer->brightnessLoc = glGetUniformLocation(renderer->shaderProgram, "u_brightness");
    
    // Create vertex buffer (quad vertices)
    GLfloat vertices[] = {
        // Position        // TexCoord
        -1.0f,  1.0f,      0.0f, 1.0f,  // Top-left
        -1.0f, -1.0f,      0.0f, 0.0f,  // Bottom-left
         1.0f,  1.0f,      1.0f, 1.0f,  // Top-right
         1.0f, -1.0f,      1.0f, 0.0f,  // Bottom-right
    };
    
    glGenBuffers(1, &renderer->vertexBuffer);
    glBindBuffer(GL_ARRAY_BUFFER, renderer->vertexBuffer);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
    
    // Create index buffer
    GLushort indices[] = {
        0, 1, 2,  // First triangle
        1, 3, 2   // Second triangle
    };
    
    glGenBuffers(1, &renderer->indexBuffer);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, renderer->indexBuffer);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW);
    
    // Enable blending
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    
    // Initialize stats
    renderer->stats.glVersion = version_str ? version_str : "unknown";
    renderer->stats.glVendor = vendor_str ? vendor_str : "unknown";
    renderer->stats.glRenderer = renderer_str ? renderer_str : "unknown";
    renderer->stats.fps = 0.0f;
    renderer->stats.textureCount = 0;
    renderer->stats.gpuMemoryUsed = 0;
    
    renderer->textures = NULL;
    renderer->textureCount = 0;
    renderer->textureCapacity = 0;
    
    LOG_INFO("[GLES2] Renderer created successfully");
    return renderer;
}

void gles2_renderer_destroy(GLES2Renderer* renderer) {
    if (!renderer) return;
    
    // Destroy all textures
    for (size_t i = 0; i < renderer->textureCount; i++) {
        gles2_renderer_destroy_texture(renderer, renderer->textures[i]);
    }
    free(renderer->textures);
    
    // Destroy GL resources
    if (renderer->shaderProgram) {
        glDeleteProgram(renderer->shaderProgram);
    }
    if (renderer->vertexBuffer) {
        glDeleteBuffers(1, &renderer->vertexBuffer);
    }
    if (renderer->indexBuffer) {
        glDeleteBuffers(1, &renderer->indexBuffer);
    }
    
    free(renderer);
    LOG_INFO("[GLES2] Renderer destroyed");
}

bool gles2_renderer_begin_frame(GLES2Renderer* renderer, int width, int height) {
    if (!renderer) return false;
    
    // Make context current
    eglMakeCurrent(renderer->egl->display, renderer->egl->surface, 
                   renderer->egl->surface, renderer->egl->context);
    
    // Set viewport
    glViewport(0, 0, width, height);
    
    // Clear screen
    glClearColor(0.1f, 0.1f, 0.15f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    
    // Use shader program
    glUseProgram(renderer->shaderProgram);
    
    // Set uniforms
    glUniform1f(renderer->gammaLoc, renderer->config.gamma);
    glUniform1f(renderer->brightnessLoc, 1.0f);
    glUniform1i(renderer->textureLoc, 0);  // Texture unit 0
    
    return true;
}

bool gles2_renderer_end_frame(GLES2Renderer* renderer) {
    if (!renderer) return false;
    
    // Swap buffers
    eglSwapBuffers(renderer->egl->display, renderer->egl->surface);
    
    return true;
}

void gles2_renderer_set_clear_color(GLES2Renderer* renderer, float r, float g, float b, float a) {
    if (!renderer) return;
    glClearColor(r, g, b, a);
}

GLES2Texture* gles2_renderer_create_texture(GLES2Renderer* renderer, uint32_t width, uint32_t height) {
    if (!renderer) return NULL;
    
    GLES2Texture* texture = (GLES2Texture*)calloc(1, sizeof(GLES2Texture));
    if (!texture) return NULL;
    
    glGenTextures(1, &texture->texture);
    glBindTexture(GL_TEXTURE_2D, texture->texture);
    
    // Set texture parameters
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    
    // Allocate texture storage
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    
    texture->width = width;
    texture->height = height;
    texture->imported = false;
    
    // Add to texture cache
    if (renderer->textureCount >= renderer->textureCapacity) {
        renderer->textureCapacity = renderer->textureCapacity ? renderer->textureCapacity * 2 : 16;
        renderer->textures = (GLES2Texture**)realloc(renderer->textures, 
                            renderer->textureCapacity * sizeof(GLES2Texture*));
    }
    renderer->textures[renderer->textureCount++] = texture;
    renderer->stats.textureCount = renderer->textureCount;
    renderer->stats.gpuMemoryUsed += width * height * 4;  // RGBA
    
    LOG_DEBUG("[GLES2] Created texture %dx%d", width, height);
    return texture;
}

GLES2Texture* gles2_renderer_import_wlr_texture(GLES2Renderer* renderer, void* wlr_texture) {
    if (!renderer || !wlr_texture) return NULL;
    
    // In full implementation, would import wlr_texture as GLES texture
    // For now, create a placeholder
    GLES2Texture* texture = gles2_renderer_create_texture(renderer, 1920, 1080);
    if (texture) {
        texture->imported = true;
    }
    return texture;
}

void gles2_renderer_destroy_texture(GLES2Renderer* renderer, GLES2Texture* texture) {
    if (!renderer || !texture) return;
    
    // Remove from cache
    for (size_t i = 0; i < renderer->textureCount; i++) {
        if (renderer->textures[i] == texture) {
            renderer->stats.gpuMemoryUsed -= texture->width * texture->height * 4;
            
            // Shift remaining textures
            for (size_t j = i; j < renderer->textureCount - 1; j++) {
                renderer->textures[j] = renderer->textures[j + 1];
            }
            renderer->textureCount--;
            renderer->stats.textureCount = renderer->textureCount;
            break;
        }
    }
    
    if (texture->texture) {
        glDeleteTextures(1, &texture->texture);
    }
    free(texture);
}

void gles2_renderer_bind_texture(GLES2Renderer* renderer, GLES2Texture* texture) {
    if (!renderer || !texture) return;
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture->texture);
}

void gles2_renderer_draw_quad(GLES2Renderer* renderer, float x, float y, float w, float h) {
    if (!renderer) return;
    
    // Enable vertex arrays
    glEnableVertexAttribArray(renderer->positionLoc);
    glEnableVertexAttribArray(renderer->texCoordLoc);
    
    // Bind vertex buffer
    glBindBuffer(GL_ARRAY_BUFFER, renderer->vertexBuffer);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, renderer->indexBuffer);
    
    // Set vertex attributes
    glVertexAttribPointer(renderer->positionLoc, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    glVertexAttribPointer(renderer->texCoordLoc, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
    
    // Draw
    glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, 0);
    
    // Disable vertex arrays
    glDisableVertexAttribArray(renderer->positionLoc);
    glDisableVertexAttribArray(renderer->texCoordLoc);
}

void gles2_renderer_set_gamma(GLES2Renderer* renderer, float gamma) {
    if (!renderer) return;
    renderer->config.gamma = gamma;
    glUniform1f(renderer->gammaLoc, gamma);
}

void gles2_renderer_set_brightness(GLES2Renderer* renderer, float brightness) {
    if (!renderer) return;
    glUniform1f(renderer->brightnessLoc, brightness);
}

void gles2_renderer_get_stats(GLES2Renderer* renderer, GLES2Stats* stats) {
    if (!renderer || !stats) return;
    *stats = renderer->stats;
}

bool gles2_renderer_is_available(void) {
    // Check if we can load GLES2
    return true;  // Would check actual availability in production
}
