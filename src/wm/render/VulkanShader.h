// Vulkan Shader System with Debugging and Exception Handling

#pragma once

#include <vulkan/vulkan.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// Configuration
// ============================================================================

#define VULKAN_SHADER_MAX_MODULES 64
#define VULKAN_SHADER_MAX_NAME_LENGTH 256
#define VULKAN_SHADER_ENABLE_VALIDATION 1
#define VULKAN_SHADER_ENABLE_DEBUG_MARKERS 1

// ============================================================================
// Error Codes
// ============================================================================

typedef enum {
    VULKAN_SHADER_OK = 0,
    VULKAN_SHADER_ERROR_INVALID_DEVICE = -1,
    VULKAN_SHADER_ERROR_INVALID_CODE = -2,
    VULKAN_SHADER_ERROR_COMPILATION_FAILED = -3,
    VULKAN_SHADER_ERROR_LINK_FAILED = -4,
    VULKAN_SHADER_ERROR_OUT_OF_MEMORY = -5,
    VULKAN_SHADER_ERROR_FILE_NOT_FOUND = -6,
    VULKAN_SHADER_ERROR_INVALID_STAGE = -7,
    VULKAN_SHADER_ERROR_MODULE_LIMIT = -8,
    VULKAN_SHADER_ERROR_DEBUG_FAILED = -9
} VulkanShaderError;

// ============================================================================
// Debug Information
// ============================================================================

typedef struct {
    const char* file;
    int line;
    const char* function;
    const char* shader_name;
    VkShaderStageFlagBits stage;
} VulkanShaderDebugInfo;

// Debug callback
typedef void (*VulkanShaderDebugCallback)(
    VulkanShaderError error,
    const char* message,
    const VulkanShaderDebugInfo* debug_info,
    void* user_data);

// ============================================================================
// Shader Module
// ============================================================================

typedef struct {
    VkShaderModule module;
    VkDevice device;
    char name[VULKAN_SHADER_MAX_NAME_LENGTH];
    VkShaderStageFlagBits stage;
    const char* entry_point;
    
    // Debug info
    VulkanShaderDebugInfo debug_info;
    uint32_t* spirv_code;
    size_t spirv_size;
    
    // Reference counting
    int ref_count;
    
    // Validity flag
    bool is_valid;
    char last_error[512];
} VulkanShaderModule;

// ============================================================================
// Shader Pipeline
// ============================================================================

typedef struct {
    VulkanShaderModule* vertex;
    VulkanShaderModule* fragment;
    VulkanShaderModule* geometry;  // Optional
    VulkanShaderModule* tess_control;  // Optional
    VulkanShaderModule* tess_evaluation;  // Optional
    
    VkPipeline pipeline;
    VkPipelineLayout layout;
    VkRenderPass render_pass;
    
    bool is_valid;
    char last_error[512];
} VulkanShaderPipeline;

// ============================================================================
// Shader Manager
// ============================================================================

typedef struct {
    VkDevice device;
    VkPhysicalDevice physical_device;
    
    VulkanShaderModule modules[VULKAN_SHADER_MAX_MODULES];
    uint32_t module_count;
    
    VulkanShaderDebugCallback debug_callback;
    void* debug_user_data;
    
    bool debug_enabled;
    bool validation_enabled;
    
    // Pipeline cache
    VkPipelineCache pipeline_cache;
} VulkanShaderManager;

// ============================================================================
// Manager Functions
// ============================================================================

/**
 * Initialize shader manager
 */
VulkanShaderError vulkan_shader_manager_init(
    VulkanShaderManager* manager,
    VkDevice device,
    VkPhysicalDevice physical_device,
    bool enable_debug,
    bool enable_validation);

/**
 * Cleanup shader manager
 */
void vulkan_shader_manager_cleanup(VulkanShaderManager* manager);

/**
 * Set debug callback
 */
void vulkan_shader_manager_set_debug_callback(
    VulkanShaderManager* manager,
    VulkanShaderDebugCallback callback,
    void* user_data);

// ============================================================================
// Shader Module Functions
// ============================================================================

/**
 * Create shader module from SPIR-V code with debug info
 */
VulkanShaderModule* vulkan_shader_create_module(
    VulkanShaderManager* manager,
    const uint32_t* spirv_code,
    size_t size,
    VkShaderStageFlagBits stage,
    const char* name,
    const char* entry_point,
    VulkanShaderDebugInfo* debug_info);

/**
 * Create shader module from file
 */
VulkanShaderModule* vulkan_shader_create_module_from_file(
    VulkanShaderManager* manager,
    const char* path,
    VkShaderStageFlagBits stage,
    const char* name,
    const char* entry_point);

/**
 * Get shader module by name
 */
VulkanShaderModule* vulkan_shader_get_module(
    VulkanShaderManager* manager,
    const char* name);

/**
 * Destroy shader module
 */
void vulkan_shader_destroy_module(VulkanShaderModule* module);

/**
 * Validate shader module
 */
bool vulkan_shader_validate_module(
    VulkanShaderModule* module,
    char* error_buffer,
    size_t error_buffer_size);

/**
 * Get shader compilation error
 */
const char* vulkan_shader_get_error(VulkanShaderModule* module);

// ============================================================================
// Pipeline Functions
// ============================================================================

/**
 * Create graphics pipeline
 */
VulkanShaderError vulkan_shader_create_pipeline(
    VulkanShaderManager* manager,
    VulkanShaderModule* vertex_shader,
    VulkanShaderModule* fragment_shader,
    const VkPipelineVertexInputStateCreateInfo* vertex_input,
    const VkPipelineInputAssemblyStateCreateInfo* input_assembly,
    const VkViewport* viewport,
    const VkRect2D* scissor,
    VkRenderPass render_pass,
    VulkanShaderPipeline* out_pipeline);

/**
 * Create pipeline with descriptor set layout
 */
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
    VulkanShaderPipeline* out_pipeline);

/**
 * Destroy pipeline
 */
void vulkan_shader_destroy_pipeline(VulkanShaderPipeline* pipeline);

/**
 * Bind pipeline for rendering
 */
void vulkan_shader_bind_pipeline(
    VkCommandBuffer command_buffer,
    VulkanShaderPipeline* pipeline);

// ============================================================================
// Built-in Shaders
// ============================================================================

/**
 * Create built-in quad shader (colored rectangle)
 */
VulkanShaderError vulkan_shader_create_quad_pipeline(
    VulkanShaderManager* manager,
    VkRenderPass render_pass,
    VulkanShaderPipeline* out_pipeline);

/**
 * Create built-in textured quad shader
 */
VulkanShaderError vulkan_shader_create_textured_pipeline(
    VulkanShaderManager* manager,
    VkRenderPass render_pass,
    VkDescriptorSetLayout texture_descriptor_layout,
    VulkanShaderPipeline* out_pipeline);

/**
 * Create built-in blur shader (Kawase blur)
 */
VulkanShaderError vulkan_shader_create_blur_pipeline(
    VulkanShaderManager* manager,
    VkRenderPass render_pass,
    VkDescriptorSetLayout texture_descriptor_layout,
    int blur_pass,
    float blur_strength,
    VulkanShaderPipeline* out_pipeline);

// ============================================================================
// Debug Utilities
// ============================================================================

/**
 * Set shader module debug name (for renderdoc, nsight, etc.)
 */
void vulkan_shader_set_debug_name(
    VulkanShaderModule* module,
    const char* name);

/**
 * Get shader disassembly (for debugging)
 */
char* vulkan_shader_get_disassembly(VulkanShaderModule* module);

/**
 * Print shader info to stdout
 */
void vulkan_shader_print_info(VulkanShaderModule* module);

/**
 * Validate all shaders in manager
 */
int vulkan_shader_manager_validate_all(VulkanShaderManager* manager);

// ============================================================================
// Exception Handling Macros
// ============================================================================

#define VULKAN_SHADER_TRY(expr) do { \
    VulkanShaderError _err = (expr); \
    if (_err != VULKAN_SHADER_OK) { \
        return _err; \
    } \
} while(0)

#define VULKAN_SHADER_THROW(manager, error, msg) do { \
    if ((manager)->debug_callback) { \
        VulkanShaderDebugInfo info = {0}; \
        (manager)->debug_callback(error, msg, &info, (manager)->debug_user_data); \
    } \
    return error; \
} while(0)

#define VULKAN_SHADER_ASSERT(condition, manager, error, msg) do { \
    if (!(condition)) { \
        VULKAN_SHADER_THROW(manager, error, msg); \
    } \
} while(0)

#define VULKAN_SHADER_CHECK_RESULT(result, manager, msg) do { \
    VkResult _vk_result = (result); \
    if (_vk_result != VK_SUCCESS) { \
        VULKAN_SHADER_THROW(manager, VULKAN_SHADER_ERROR_COMPILATION_FAILED, msg); \
    } \
} while(0)

#ifdef __cplusplus
}
#endif
