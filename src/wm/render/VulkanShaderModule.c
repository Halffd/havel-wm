// Vulkan Shader Module Management

#include "VulkanShader.h"
#include <utils/Common.h>
#include <Logger.h>
#include <stdlib.h>
#include <string.h>

// ============================================================================
// Internal Helper Functions
// ============================================================================

static void shader_log_error(VulkanShaderManager* manager, VulkanShaderError error, 
                             const char* message, const VulkanShaderDebugInfo* debug_info) {
    if (manager && manager->debug_callback) {
        manager->debug_callback(error, message, debug_info, manager->debug_user_data);
    } else {
        default_debug_callback(error, message, debug_info, NULL);
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
// Shader Module Creation
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
        shader_log_error(manager, VULKAN_SHADER_ERROR_INVALID_CODE, 
                        "Invalid shader code", debug_info);
        return NULL;
    }
    
    if (manager->module_count >= VULKAN_SHADER_MAX_MODULES) {
        shader_log_error(manager, VULKAN_SHADER_ERROR_MODULE_LIMIT, 
                        "Too many shader modules", debug_info);
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
        shader_log_error(manager, VULKAN_SHADER_ERROR_COMPILATION_FAILED, 
                        module->last_error, debug_info);
        
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
        shader_log_error(manager, VULKAN_SHADER_ERROR_FILE_NOT_FOUND, "NULL path", NULL);
        return NULL;
    }
    
    // Load SPIR-V file
    size_t size = 0;
    uint32_t* spirv = vulkan_load_spirv_file(path, &size);
    
    if (!spirv || size == 0) {
        shader_log_error(manager, VULKAN_SHADER_ERROR_FILE_NOT_FOUND, path, NULL);
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

// ============================================================================
// Shader Module Access
// ============================================================================

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

// ============================================================================
// Shader Validation
// ============================================================================

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
