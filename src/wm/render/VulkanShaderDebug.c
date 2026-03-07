// Vulkan Shader Debug Utilities and Exception Handling

#include "VulkanShader.h"
#include <utils/Common.h>
#include <Logger.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

// ============================================================================
// Default Debug Callback
// ============================================================================

void default_debug_callback(
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
        
        if (debug_info->stage != 0) {
            const char* stage_str = "Unknown";
            switch (debug_info->stage) {
                case VK_SHADER_STAGE_VERTEX_BIT: stage_str = "Vertex"; break;
                case VK_SHADER_STAGE_FRAGMENT_BIT: stage_str = "Fragment"; break;
                case VK_SHADER_STAGE_GEOMETRY_BIT: stage_str = "Geometry"; break;
                case VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT: stage_str = "Tess Control"; break;
                case VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT: stage_str = "Tess Eval"; break;
                case VK_SHADER_STAGE_COMPUTE_BIT: stage_str = "Compute"; break;
                default: break;
            }
            LOG_ERROR("  Stage: %s", stage_str);
        }
    } else {
        LOG_ERROR("[VulkanShader] %s: %s", error_str, message);
    }
}

// ============================================================================
// Debug Name Setting
// ============================================================================

void vulkan_shader_set_debug_name(VulkanShaderModule* module, const char* name) {
    if (!module || !name) return;
    
    // Store the name in the module
    strncpy(module->name, name, VULKAN_SHADER_MAX_NAME_LENGTH - 1);
    module->name[VULKAN_SHADER_MAX_NAME_LENGTH - 1] = '\0';
    
    // Would use VK_EXT_debug_utils here if available:
    // VkDebugUtilsObjectNameInfoEXT name_info = {0};
    // name_info.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
    // name_info.objectType = VK_OBJECT_TYPE_SHADER_MODULE;
    // name_info.objectHandle = (uint64_t)module->module;
    // name_info.pObjectName = name;
    // vkSetDebugUtilsObjectNameEXT(device, &name_info);
}

// ============================================================================
// Shader Disassembly
// ============================================================================

char* vulkan_shader_get_disassembly(VulkanShaderModule* module) {
    if (!module || !module->spirv_code) return NULL;
    
    // Allocate buffer for disassembly
    size_t buffer_size = 2048;
    char* disasm = (char*)malloc(buffer_size);
    
    if (!disasm) return NULL;
    
    // Header information
    int offset = 0;
    offset += snprintf(disasm + offset, buffer_size - offset,
                      "=== Shader Disassembly ===\n");
    offset += snprintf(disasm + offset, buffer_size - offset,
                      "Name: %s\n", module->name);
    offset += snprintf(disasm + offset, buffer_size - offset,
                      "Stage: %d\n", module->stage);
    offset += snprintf(disasm + offset, buffer_size - offset,
                      "Entry Point: %s\n", module->entry_point);
    offset += snprintf(disasm + offset, buffer_size - offset,
                      "Size: %zu bytes (%zu words)\n", 
                      module->spirv_size, module->spirv_size / 4);
    offset += snprintf(disasm + offset, buffer_size - offset,
                      "SPIR-V Version: %d.%d\n",
                      VK_SPIRV_VERSION_MAJOR(module->spirv_code[1]),
                      VK_SPIRV_VERSION_MINOR(module->spirv_code[1]));
    offset += snprintf(disasm + offset, buffer_size - offset,
                      "Generator: 0x%08X\n", module->spirv_code[2]);
    offset += snprintf(disasm + offset, buffer_size - offset,
                      "=========================\n");
    
    // Would use spirv-cross or SPIRV-Tools for full disassembly here
    // For now, show first few instructions
    offset += snprintf(disasm + offset, buffer_size - offset,
                      "\nFirst 16 words (hex):\n");
    
    for (int i = 0; i < 16 && i < (int)(module->spirv_size / 4); i++) {
        offset += snprintf(disasm + offset, buffer_size - offset,
                          "  [%02d] 0x%08X\n", i, module->spirv_code[i]);
    }
    
    return disasm;
}

// ============================================================================
// Shader Info Printing
// ============================================================================

void vulkan_shader_print_info(VulkanShaderModule* module) {
    if (!module) {
        printf("NULL shader module\n");
        return;
    }
    
    printf("=== Shader Module Info ===\n");
    printf("Name: %s\n", module->name);
    printf("Stage: %d\n", module->stage);
    printf("Entry Point: %s\n", module->entry_point);
    printf("Valid: %s\n", module->is_valid ? "yes" : "no");
    printf("Size: %zu bytes\n", module->spirv_size);
    printf("Reference Count: %d\n", module->ref_count);
    
    if (!module->is_valid) {
        printf("Error: %s\n", module->last_error);
    }
    
    // Debug info
    if (module->debug_info.file) {
        printf("\nDebug Info:\n");
        printf("  File: %s\n", module->debug_info.file);
        printf("  Line: %d\n", module->debug_info.line);
        if (module->debug_info.function) {
            printf("  Function: %s\n", module->debug_info.function);
        }
    }
    
    printf("========================\n");
}

// ============================================================================
// Manager Validation
// ============================================================================

int vulkan_shader_manager_validate_all(VulkanShaderManager* manager) {
    if (!manager) return 0;
    
    int valid_count = 0;
    int invalid_count = 0;
    
    for (uint32_t i = 0; i < manager->module_count; i++) {
        VulkanShaderModule* module = &manager->modules[i];
        char error_buffer[512] = {0};
        
        if (vulkan_shader_validate_module(module, error_buffer, sizeof(error_buffer))) {
            valid_count++;
            LOG_DEBUG("[VulkanShader] Module '%s' validated OK", module->name);
        } else {
            invalid_count++;
            LOG_ERROR("[VulkanShader] Module '%s' validation failed: %s",
                     module->name, error_buffer[0] ? error_buffer : module->last_error);
        }
    }
    
    LOG_INFO("[VulkanShader] Validation complete: %d valid, %d invalid", 
             valid_count, invalid_count);
    
    return valid_count;
}

// ============================================================================
// Exception Handling Helpers
// ============================================================================

void vulkan_shader_throw_error(VulkanShaderManager* manager, 
                               VulkanShaderError error, 
                               const char* message,
                               const VulkanShaderDebugInfo* debug_info) {
    if (manager && manager->debug_callback) {
        manager->debug_callback(error, message, debug_info, manager->debug_user_data);
    } else {
        default_debug_callback(error, message, debug_info, NULL);
    }
}

bool vulkan_shader_check_result(VkResult result, VulkanShaderManager* manager, 
                                const char* operation) {
    if (result == VK_SUCCESS) {
        return true;
    }
    
    char message[512];
    snprintf(message, sizeof(message), "%s failed: %d", operation, result);
    
    vulkan_shader_throw_error(manager, VULKAN_SHADER_ERROR_COMPILATION_FAILED, 
                             message, NULL);
    
    return false;
}

// ============================================================================
// SPIR-V File Loading
// ============================================================================

uint32_t* vulkan_load_spirv_file(const char* path, size_t* out_size) {
    if (!path) return NULL;
    
    FILE* file = fopen(path, "rb");
    if (!file) {
        LOG_ERROR("[VulkanShader] Failed to open SPIR-V file: %s", path);
        return NULL;
    }
    
    // Get file size
    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    fseek(file, 0, SEEK_SET);
    
    if (size <= 0 || size % 4 != 0) {
        LOG_ERROR("[VulkanShader] Invalid SPIR-V file size: %ld", size);
        fclose(file);
        return NULL;
    }
    
    // Allocate buffer
    uint32_t* buffer = (uint32_t*)malloc(size);
    if (!buffer) {
        LOG_ERROR("[VulkanShader] Failed to allocate SPIR-V buffer");
        fclose(file);
        return NULL;
    }
    
    // Read file
    size_t read_size = fread(buffer, 1, size, file);
    fclose(file);
    
    if (read_size != (size_t)size) {
        LOG_ERROR("[VulkanShader] Failed to read SPIR-V file");
        FREE(buffer);
        return NULL;
    }
    
    // Validate SPIR-V magic number
    if (buffer[0] != 0x07230203) {
        LOG_ERROR("[VulkanShader] Invalid SPIR-V magic number: 0x%08X", buffer[0]);
        FREE(buffer);
        return NULL;
    }
    
    if (out_size) {
        *out_size = size;
    }
    
    LOG_DEBUG("[VulkanShader] Loaded SPIR-V file: %s (%zu bytes)", path, size);
    
    return buffer;
}

void vulkan_free_spirv(uint32_t* code) {
    FREE(code);
}
