// Vulkan Shader Implementation with Debugging

#include "VulkanShader.h"
#include <utils/Common.h>
#include <Logger.h>
#include <stdlib.h>
#include <string.h>

// ============================================================================
// Global Debug Callback
// ============================================================================

static void default_debug_callback(
    VulkanShaderError error,
    const char* message,
    const VulkanShaderDebugInfo* debug_info,
    void* user_data) {
    
    (void)user_data;
    
    const char* error_str = "Unknown error";
    switch (error) {
        case VULKAN_SHADER_OK: error_str = "OK"; break;
        case VULKAN_SHADER_ERROR_INVALID_DEVICE: error_str = "Invalid device"; break;
        case VULKAN_SHADER_ERROR_INVALID_CODE: error_str = "Invalid code"; break;
        case VULKAN_SHADER_ERROR_COMPILATION_FAILED: error_str = "Compilation failed"; break;
        case VULKAN_SHADER_ERROR_LINK_FAILED: error_str = "Link failed"; break;
        case VULKAN_SHADER_ERROR_OUT_OF_MEMORY: error_str = "Out of memory"; break;
        case VULKAN_SHADER_ERROR_FILE_NOT_FOUND: error_str = "File not found"; break;
        case VULKAN_SHADER_ERROR_INVALID_STAGE: error_str = "Invalid stage"; break;
        case VULKAN_SHADER_ERROR_MODULE_LIMIT: error_str = "Module limit reached"; break;
        case VULKAN_SHADER_ERROR_DEBUG_FAILED: error_str = "Debug failed"; break;
    }
    
    if (debug_info && debug_info->shader_name) {
        LOG_ERROR("[VulkanShader] %s in '%s': %s", error_str, debug_info->shader_name, message);
        
        if (debug_info->file) {
            LOG_ERROR("  Source: %s:%d in %s()", debug_info->file, debug_info->line, 
                     debug_info->function ? debug_info->function : "unknown");
        }
    } else {
        LOG_ERROR("[VulkanShader] %s: %s", error_str, message);
    }
}

// ============================================================================
// Manager Functions
// ============================================================================

VulkanShaderError vulkan_shader_manager_init(
    VulkanShaderManager* manager,
    VkDevice device,
    VkPhysicalDevice physical_device,
    bool enable_debug,
    bool enable_validation) {
    
    if (!manager || !device) {
        return VULKAN_SHADER_ERROR_INVALID_DEVICE;
    }
    
    memset(manager, 0, sizeof(VulkanShaderManager));
    
    manager->device = device;
    manager->physical_device = physical_device;
    manager->module_count = 0;
    manager->debug_enabled = enable_debug;
    manager->validation_enabled = enable_validation;
    manager->debug_callback = default_debug_callback;
    manager->debug_user_data = NULL;
    
    // Create pipeline cache
    VkPipelineCacheCreateInfo cache_info = {0};
    cache_info.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
    
    VkResult result = vkCreatePipelineCache(device, &cache_info, NULL, &manager->pipeline_cache);
    if (result != VK_SUCCESS) {
        LOG_WARN("[VulkanShader] Failed to create pipeline cache: %d", result);
        manager->pipeline_cache = VK_NULL_HANDLE;
    }
    
    LOG_INFO("[VulkanShader] Manager initialized (debug=%d, validation=%d)", 
             enable_debug, enable_validation);
    
    return VULKAN_SHADER_OK;
}

void vulkan_shader_manager_cleanup(VulkanShaderManager* manager) {
    if (!manager) return;
    
    // Destroy all modules
    for (uint32_t i = 0; i < manager->module_count; i++) {
        vulkan_shader_destroy_module(&manager->modules[i]);
    }
    
    // Destroy pipeline cache
    if (manager->pipeline_cache) {
        vkDestroyPipelineCache(manager->device, manager->pipeline_cache, NULL);
        manager->pipeline_cache = VK_NULL_HANDLE;
    }
    
    memset(manager, 0, sizeof(VulkanShaderManager));
    
    LOG_INFO("[VulkanShader] Manager cleaned up");
}

void vulkan_shader_manager_set_debug_callback(
    VulkanShaderManager* manager,
    VulkanShaderDebugCallback callback,
    void* user_data) {
    
    if (!manager) return;
    
    manager->debug_callback = callback ? callback : default_debug_callback;
    manager->debug_user_data = user_data;
}

// ============================================================================
// Shader Module Functions
// ============================================================================

VulkanShaderModule* vulkan_shader_create_module(
    VulkanShaderManager* manager,
    const uint32_t* spirv_code,
    size_t size,
    VkShaderStageFlagBits stage,
    const char* name,
    const char* entry_point,
    VulkanShaderDebugInfo* debug_info) {
    
    if (!manager || !spirv_code || size == 0) {
        VULKAN_SHADER_THROW(manager, VULKAN_SHADER_ERROR_INVALID_CODE, "Invalid shader code");
        return NULL;
    }
    
    if (manager->module_count >= VULKAN_SHADER_MAX_MODULES) {
        VULKAN_SHADER_THROW(manager, VULKAN_SHADER_ERROR_MODULE_LIMIT, "Too many shader modules");
        return NULL;
    }
    
    VulkanShaderModule* module = &manager->modules[manager->module_count++];
    memset(module, 0, sizeof(VulkanShaderModule));
    
    module->device = manager->device;
    module->stage = stage;
    module->entry_point = entry_point ? entry_point : "main";
    module->ref_count = 1;
    module->is_valid = false;
    
    // Copy name
    if (name) {
        strncpy(module->name, name, VULKAN_SHADER_MAX_NAME_LENGTH - 1);
        module->name[VULKAN_SHADER_MAX_NAME_LENGTH - 1] = '\0';
    } else {
        snprintf(module->name, VULKAN_SHADER_MAX_NAME_LENGTH, "shader_%u", manager->module_count - 1);
    }
    
    // Copy debug info
    if (debug_info) {
        module->debug_info = *debug_info;
    }
    
    // Copy SPIR-V code for debugging
    module->spirv_code = (uint32_t*)malloc(size);
    if (module->spirv_code) {
        memcpy(module->spirv_code, spirv_code, size);
        module->spirv_size = size;
    }
    
    // Create Vulkan shader module
    VkShaderModuleCreateInfo create_info = {0};
    create_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    create_info.codeSize = size;
    create_info.pCode = spirv_code;
    
    VkResult result = vkCreateShaderModule(manager->device, &create_info, NULL, &module->module);
    
    if (result != VK_SUCCESS) {
        snprintf(module->last_error, sizeof(module->last_error), 
                 "vkCreateShaderModule failed: %d", result);
        VULKAN_SHADER_THROW(manager, VULKAN_SHADER_ERROR_COMPILATION_FAILED, module->last_error);
        
        FREE(module->spirv_code);
        manager->module_count--;
        return NULL;
    }
    
    module->is_valid = true;
    
    // Set debug name if available
    if (manager->debug_enabled && name) {
        vulkan_shader_set_debug_name(module, name);
    }
    
    LOG_DEBUG("[VulkanShader] Created module '%s' (%zu bytes)", name, size);
    
    return module;
}

VulkanShaderModule* vulkan_shader_create_module_from_file(
    VulkanShaderManager* manager,
    const char* path,
    VkShaderStageFlagBits stage,
    const char* name,
    const char* entry_point) {
    
    if (!path) {
        VULKAN_SHADER_THROW(manager, VULKAN_SHADER_ERROR_FILE_NOT_FOUND, "NULL path");
        return NULL;
    }
    
    // Load SPIR-V file
    size_t size = 0;
    uint32_t* spirv = vulkan_load_spirv_file(path, &size);
    
    if (!spirv || size == 0) {
        VULKAN_SHADER_THROW(manager, VULKAN_SHADER_ERROR_FILE_NOT_FOUND, path);
        return NULL;
    }
    
    // Create debug info
    VulkanShaderDebugInfo debug_info = {0};
    debug_info.file = path;
    debug_info.stage = stage;
    debug_info.shader_name = name ? name : path;
    
    // Create module
    VulkanShaderModule* module = vulkan_shader_create_module(
        manager, spirv, size, stage, name, entry_point, &debug_info);
    
    vulkan_free_spirv(spirv);
    
    return module;
}

VulkanShaderModule* vulkan_shader_get_module(
    VulkanShaderManager* manager,
    const char* name) {
    
    if (!manager || !name) return NULL;
    
    for (uint32_t i = 0; i < manager->module_count; i++) {
        if (strcmp(manager->modules[i].name, name) == 0) {
            return &manager->modules[i];
        }
    }
    
    return NULL;
}

void vulkan_shader_destroy_module(VulkanShaderModule* module) {
    if (!module) return;
    
    module->ref_count--;
    
    if (module->ref_count > 0) {
        return;
    }
    
    if (module->module != VK_NULL_HANDLE) {
        vkDestroyShaderModule(module->device, module->module, NULL);
        module->module = VK_NULL_HANDLE;
    }
    
    FREE(module->spirv_code);
    
    memset(module, 0, sizeof(VulkanShaderModule));
}

bool vulkan_shader_validate_module(
    VulkanShaderModule* module,
    char* error_buffer,
    size_t error_buffer_size) {
    
    if (!module) {
        if (error_buffer) {
            snprintf(error_buffer, error_buffer_size, "NULL module");
        }
        return false;
    }
    
    if (!module->is_valid) {
        if (error_buffer) {
            snprintf(error_buffer, error_buffer_size, "%s", module->last_error);
        }
        return false;
    }
    
    if (module->module == VK_NULL_HANDLE) {
        if (error_buffer) {
            snprintf(error_buffer, error_buffer_size, "Invalid module handle");
        }
        return false;
    }
    
    // Basic SPIR-V validation
    if (module->spirv_code && module->spirv_size > 0) {
        // Check SPIR-V magic number
        if (module->spirv_code[0] != 0x07230203) {
            if (error_buffer) {
                snprintf(error_buffer, error_buffer_size, "Invalid SPIR-V magic number");
            }
            return false;
        }
    }
    
    return true;
}

const char* vulkan_shader_get_error(VulkanShaderModule* module) {
    if (!module) return "NULL module";
    return module->last_error;
}

// ============================================================================
// Pipeline Functions
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

void vulkan_shader_bind_pipeline(VkCommandBuffer command_buffer, VulkanShaderPipeline* pipeline) {
    if (!command_buffer || !pipeline || !pipeline->is_valid) return;
    
    vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline->pipeline);
}

// ============================================================================
// Debug Utilities
// ============================================================================

void vulkan_shader_set_debug_name(VulkanShaderModule* module, const char* name) {
    if (!module || !name) return;
    
    // Would use VK_EXT_debug_utils if available
    // For now, just store the name
    strncpy(module->name, name, VULKAN_SHADER_MAX_NAME_LENGTH - 1);
}

char* vulkan_shader_get_disassembly(VulkanShaderModule* module) {
    if (!module || !module->spirv_code) return NULL;
    
    // Would use spirv-cross or similar for disassembly
    // For now, return a placeholder
    char* disasm = (char*)malloc(1024);
    if (disasm) {
        snprintf(disasm, 1024, 
                 "Shader: %s\nStage: %d\nSize: %zu bytes\nSPIR-V Magic: 0x%08X\n",
                 module->name, module->stage, module->spirv_size,
                 module->spirv_code ? module->spirv_code[0] : 0);
    }
    
    return disasm;
}

void vulkan_shader_print_info(VulkanShaderModule* module) {
    if (!module) {
        printf("NULL shader module\n");
        return;
    }
    
    printf("=== Shader Module Info ===\n");
    printf("Name: %s\n", module->name);
    printf("Stage: %d\n", module->stage);
    printf("Entry: %s\n", module->entry_point);
    printf("Valid: %s\n", module->is_valid ? "yes" : "no");
    printf("Size: %zu bytes\n", module->spirv_size);
    printf("Ref Count: %d\n", module->ref_count);
    
    if (!module->is_valid) {
        printf("Error: %s\n", module->last_error);
    }
    
    printf("========================\n");
}

int vulkan_shader_manager_validate_all(VulkanShaderManager* manager) {
    if (!manager) return 0;
    
    int valid_count = 0;
    
    for (uint32_t i = 0; i < manager->module_count; i++) {
        VulkanShaderModule* module = &manager->modules[i];
        
        if (vulkan_shader_validate_module(module, NULL, 0)) {
            valid_count++;
        } else {
            LOG_ERROR("[VulkanShader] Module '%s' validation failed: %s",
                     module->name, module->last_error);
        }
    }
    
    LOG_INFO("[VulkanShader] Validated %u/%u shaders", valid_count, manager->module_count);
    
    return valid_count;
}
