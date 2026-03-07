// Vulkan Shader Pipeline Management

#include "VulkanShader.h"
#include <utils/Common.h>
#include <Logger.h>
#include <stdlib.h>
#include <string.h>

// ============================================================================
// Graphics Pipeline Creation
// ============================================================================

VulkanShaderError vulkan_shader_create_pipeline(
    VulkanShaderManager* manager,
    VulkanShaderModule* vertex_shader,
    VulkanShaderModule* fragment_shader,
    const VkPipelineVertexInputStateCreateInfo* vertex_input,
    const VkPipelineInputAssemblyStateCreateInfo* input_assembly,
    const VkViewport* viewport,
    const VkRect2D* scissor,
    VkRenderPass render_pass,
    VulkanShaderPipeline* out_pipeline) {
    
    return vulkan_shader_create_pipeline_with_descriptors(
        manager, vertex_shader, fragment_shader,
        NULL, 0, NULL, 0,
        vertex_input, input_assembly, viewport, scissor,
        render_pass, out_pipeline);
}

VulkanShaderError vulkan_shader_create_pipeline_with_descriptors(
    VulkanShaderManager* manager,
    VulkanShaderModule* vertex_shader,
    VulkanShaderModule* fragment_shader,
    const VkDescriptorSetLayout* descriptor_layouts,
    uint32_t descriptor_layout_count,
    const VkPushConstantRange* push_constant_ranges,
    uint32_t push_constant_range_count,
    const VkPipelineVertexInputStateCreateInfo* vertex_input,
    const VkPipelineInputAssemblyStateCreateInfo* input_assembly,
    const VkViewport* viewport,
    const VkRect2D* scissor,
    VkRenderPass render_pass,
    VulkanShaderPipeline* out_pipeline) {
    
    VULKAN_SHADER_ASSERT(manager, manager, VULKAN_SHADER_ERROR_INVALID_DEVICE, "NULL manager");
    VULKAN_SHADER_ASSERT(vertex_shader && vertex_shader->is_valid, manager, 
                        VULKAN_SHADER_ERROR_INVALID_CODE, "Invalid vertex shader");
    VULKAN_SHADER_ASSERT(fragment_shader && fragment_shader->is_valid, manager,
                        VULKAN_SHADER_ERROR_INVALID_CODE, "Invalid fragment shader");
    VULKAN_SHADER_ASSERT(out_pipeline, manager, VULKAN_SHADER_ERROR_INVALID_DEVICE, "NULL output");
    
    memset(out_pipeline, 0, sizeof(VulkanShaderPipeline));
    
    // Create pipeline layout
    VkPipelineLayoutCreateInfo layout_info = {0};
    layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layout_info.setLayoutCount = descriptor_layout_count;
    layout_info.pSetLayouts = descriptor_layouts;
    layout_info.pushConstantRangeCount = push_constant_range_count;
    layout_info.pPushConstantRanges = push_constant_ranges;
    
    VkResult result = vkCreatePipelineLayout(manager->device, &layout_info, NULL, &out_pipeline->layout);
    VULKAN_SHADER_CHECK_RESULT(result, manager, "Failed to create pipeline layout");
    
    // Shader stages
    VkPipelineShaderStageCreateInfo stages[2] = {0};
    
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vertex_shader->module;
    stages[0].pName = vertex_shader->entry_point;
    
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fragment_shader->module;
    stages[1].pName = fragment_shader->entry_point;
    
    // Viewport state
    VkPipelineViewportStateCreateInfo viewport_state = {0};
    viewport_state.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewport_state.viewportCount = 1;
    viewport_state.pViewports = viewport;
    viewport_state.scissorCount = 1;
    viewport_state.pScissors = scissor;
    
    // Rasterizer
    VkPipelineRasterizationStateCreateInfo rasterizer = {0};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_NONE;
    rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE;
    
    // Multisample
    VkPipelineMultisampleStateCreateInfo multisample = {0};
    multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisample.sampleShadingEnable = VK_FALSE;
    multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    
    // Color blend
    VkPipelineColorBlendAttachmentState color_blend = {0};
    color_blend.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                 VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    color_blend.blendEnable = VK_TRUE;
    color_blend.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    color_blend.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    color_blend.colorBlendOp = VK_BLEND_OP_ADD;
    color_blend.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    color_blend.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    color_blend.alphaBlendOp = VK_BLEND_OP_ADD;
    
    VkPipelineColorBlendStateCreateInfo color_blend_state = {0};
    color_blend_state.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    color_blend_state.attachmentCount = 1;
    color_blend_state.pAttachments = &color_blend;
    
    // Dynamic state
    VkDynamicState dynamic_states[] = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR
    };
    
    VkPipelineDynamicStateCreateInfo dynamic_state = {0};
    dynamic_state.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamic_state.dynamicStateCount = ARRAY_LENGTH(dynamic_states);
    dynamic_state.pDynamicStates = dynamic_states;
    
    // Create pipeline
    VkGraphicsPipelineCreateInfo pipeline_info = {0};
    pipeline_info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipeline_info.stageCount = 2;
    pipeline_info.pStages = stages;
    pipeline_info.pVertexInputState = vertex_input;
    pipeline_info.pInputAssemblyState = input_assembly;
    pipeline_info.pViewportState = &viewport_state;
    pipeline_info.pRasterizationState = &rasterizer;
    pipeline_info.pMultisampleState = &multisample;
    pipeline_info.pColorBlendState = &color_blend_state;
    pipeline_info.pDynamicState = &dynamic_state;
    pipeline_info.layout = out_pipeline->layout;
    pipeline_info.renderPass = render_pass;
    pipeline_info.subpass = 0;
    
    result = vkCreateGraphicsPipelines(
        manager->device,
        manager->pipeline_cache,
        1,
        &pipeline_info,
        NULL,
        &out_pipeline->pipeline);
    
    VULKAN_SHADER_CHECK_RESULT(result, manager, "Failed to create graphics pipeline");
    
    out_pipeline->vertex = vertex_shader;
    out_pipeline->fragment = fragment_shader;
    out_pipeline->render_pass = render_pass;
    out_pipeline->is_valid = true;
    
    vertex_shader->ref_count++;
    fragment_shader->ref_count++;
    
    LOG_DEBUG("[VulkanShader] Created pipeline with '%s' + '%s'", 
             vertex_shader->name, fragment_shader->name);
    
    return VULKAN_SHADER_OK;
}

// ============================================================================
// Pipeline Destruction
// ============================================================================

void vulkan_shader_destroy_pipeline(VulkanShaderPipeline* pipeline) {
    if (!pipeline) return;
    
    if (pipeline->pipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(pipeline->vertex->device, pipeline->pipeline, NULL);
        pipeline->pipeline = VK_NULL_HANDLE;
    }
    
    if (pipeline->layout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(pipeline->vertex->device, pipeline->layout, NULL);
        pipeline->layout = VK_NULL_HANDLE;
    }
    
    // Decrement shader ref counts
    if (pipeline->vertex) {
        vulkan_shader_destroy_module(pipeline->vertex);
    }
    if (pipeline->fragment) {
        vulkan_shader_destroy_module(pipeline->fragment);
    }
    
    memset(pipeline, 0, sizeof(VulkanShaderPipeline));
}

// ============================================================================
// Pipeline Binding
// ============================================================================

void vulkan_shader_bind_pipeline(VkCommandBuffer command_buffer, VulkanShaderPipeline* pipeline) {
    if (!command_buffer || !pipeline || !pipeline->is_valid) return;
    
    vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline->pipeline);
}

// ============================================================================
// Pipeline Validation
// ============================================================================

bool vulkan_shader_validate_pipeline(VulkanShaderPipeline* pipeline, char* error_buffer, size_t buffer_size) {
    if (!pipeline) {
        if (error_buffer) {
            snprintf(error_buffer, buffer_size, "NULL pipeline");
        }
        return false;
    }
    
    if (!pipeline->is_valid) {
        if (error_buffer) {
            snprintf(error_buffer, buffer_size, "%s", pipeline->last_error);
        }
        return false;
    }
    
    if (pipeline->pipeline == VK_NULL_HANDLE) {
        if (error_buffer) {
            snprintf(error_buffer, buffer_size, "Invalid pipeline handle");
        }
        return false;
    }
    
    if (!pipeline->vertex || !pipeline->fragment) {
        if (error_buffer) {
            snprintf(error_buffer, buffer_size, "Missing shader stages");
        }
        return false;
    }
    
    return true;
}
