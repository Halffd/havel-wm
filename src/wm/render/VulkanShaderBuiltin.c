// Vulkan Built-in Shaders (File-based Loading)

#include "VulkanShader.h"
#include <utils/Common.h>
#include <Logger.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

// ============================================================================
// Shader File Paths
// ============================================================================

#define SHADER_DIR "/usr/share/havel-wm/shaders/"

#define QUAD_VERTEX_SHADER_FILE   SHADER_DIR "quad.vert.spv"
#define QUAD_FRAGMENT_SHADER_FILE SHADER_DIR "quad.frag.spv"
#define TEXTURED_VERTEX_SHADER_FILE   SHADER_DIR "textured.vert.spv"
#define TEXTURED_FRAGMENT_SHADER_FILE SHADER_DIR "textured.frag.spv"
#define BLUR_VERTEX_SHADER_FILE   SHADER_DIR "blur.vert.spv"
#define BLUR_FRAGMENT_SHADER_FILE SHADER_DIR "blur.frag.spv"

// ============================================================================
// Built-in Pipeline Creation
// ============================================================================

VulkanShaderError vulkan_shader_create_quad_pipeline(
    VulkanShaderManager* manager,
    VkRenderPass render_pass,
    VulkanShaderPipeline* out_pipeline) {
    
    if (!manager || !out_pipeline) {
        return VULKAN_SHADER_ERROR_INVALID_DEVICE;
    }
    
    // Load vertex shader
    VulkanShaderModule* vert = vulkan_shader_create_module_from_file(
        manager, QUAD_VERTEX_SHADER_FILE, 
        VK_SHADER_STAGE_VERTEX_BIT, "quad_vert", "main");
    
    if (!vert) {
        LOG_ERROR("[VulkanShader] Failed to load quad vertex shader");
        return VULKAN_SHADER_ERROR_FILE_NOT_FOUND;
    }
    
    // Load fragment shader
    VulkanShaderModule* frag = vulkan_shader_create_module_from_file(
        manager, QUAD_FRAGMENT_SHADER_FILE,
        VK_SHADER_STAGE_FRAGMENT_BIT, "quad_frag", "main");
    
    if (!frag) {
        vulkan_shader_destroy_module(vert);
        LOG_ERROR("[VulkanShader] Failed to load quad fragment shader");
        return VULKAN_SHADER_ERROR_FILE_NOT_FOUND;
    }
    
    // Create pipeline
    VulkanShaderError err = vulkan_shader_create_pipeline(
        manager, vert, frag, NULL, NULL, NULL, NULL, render_pass, out_pipeline);
    
    if (err != VULKAN_SHADER_OK) {
        vulkan_shader_destroy_module(vert);
        vulkan_shader_destroy_module(frag);
        LOG_ERROR("[VulkanShader] Failed to create quad pipeline");
        return err;
    }
    
    LOG_INFO("[VulkanShader] Created quad pipeline");
    return VULKAN_SHADER_OK;
}

VulkanShaderError vulkan_shader_create_textured_pipeline(
    VulkanShaderManager* manager,
    VkRenderPass render_pass,
    VkDescriptorSetLayout texture_descriptor_layout,
    VulkanShaderPipeline* out_pipeline) {
    
    if (!manager || !out_pipeline) {
        return VULKAN_SHADER_ERROR_INVALID_DEVICE;
    }
    
    // Load shaders
    VulkanShaderModule* vert = vulkan_shader_create_module_from_file(
        manager, TEXTURED_VERTEX_SHADER_FILE,
        VK_SHADER_STAGE_VERTEX_BIT, "textured_vert", "main");
    
    VulkanShaderModule* frag = vulkan_shader_create_module_from_file(
        manager, TEXTURED_FRAGMENT_SHADER_FILE,
        VK_SHADER_STAGE_FRAGMENT_BIT, "textured_frag", "main");
    
    if (!vert || !frag) {
        if (vert) vulkan_shader_destroy_module(vert);
        if (frag) vulkan_shader_destroy_module(frag);
        return VULKAN_SHADER_ERROR_FILE_NOT_FOUND;
    }
    
    // Create pipeline with descriptor layout
    VulkanShaderError err = vulkan_shader_create_pipeline_with_descriptors(
        manager, vert, frag,
        &texture_descriptor_layout, 1,
        NULL, 0,
        NULL, NULL, NULL, NULL,
        render_pass, out_pipeline);
    
    if (err != VULKAN_SHADER_OK) {
        vulkan_shader_destroy_module(vert);
        vulkan_shader_destroy_module(frag);
        return err;
    }
    
    LOG_INFO("[VulkanShader] Created textured pipeline");
    return VULKAN_SHADER_OK;
}

VulkanShaderError vulkan_shader_create_blur_pipeline(
    VulkanShaderManager* manager,
    VkRenderPass render_pass,
    VkDescriptorSetLayout texture_descriptor_layout,
    int blur_pass,
    float blur_strength,
    VulkanShaderPipeline* out_pipeline) {
    
    (void)blur_pass;
    (void)blur_strength;
    
    if (!manager || !out_pipeline) {
        return VULKAN_SHADER_ERROR_INVALID_DEVICE;
    }
    
    // Load shaders
    VulkanShaderModule* vert = vulkan_shader_create_module_from_file(
        manager, BLUR_VERTEX_SHADER_FILE,
        VK_SHADER_STAGE_VERTEX_BIT, "blur_vert", "main");
    
    VulkanShaderModule* frag = vulkan_shader_create_module_from_file(
        manager, BLUR_FRAGMENT_SHADER_FILE,
        VK_SHADER_STAGE_FRAGMENT_BIT, "blur_frag", "main");
    
    if (!vert || !frag) {
        if (vert) vulkan_shader_destroy_module(vert);
        if (frag) vulkan_shader_destroy_module(frag);
        return VULKAN_SHADER_ERROR_FILE_NOT_FOUND;
    }
    
    // Create pipeline
    VulkanShaderError err = vulkan_shader_create_pipeline_with_descriptors(
        manager, vert, frag,
        &texture_descriptor_layout, 1,
        NULL, 0,
        NULL, NULL, NULL, NULL,
        render_pass, out_pipeline);
    
    if (err != VULKAN_SHADER_OK) {
        vulkan_shader_destroy_module(vert);
        vulkan_shader_destroy_module(frag);
        return err;
    }
    
    LOG_INFO("[VulkanShader] Created blur pipeline (pass %d, strength %.2f)", 
             blur_pass, blur_strength);
    return VULKAN_SHADER_OK;
}
