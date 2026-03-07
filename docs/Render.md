# Vulkan Rendering System

## Overview

Havel WM features a comprehensive Vulkan-based rendering system with support for:
- Hardware-accelerated 2D/3D rendering
- Shader-based effects and post-processing
- Multi-GPU support
- Damage tracking for efficient updates
- Overlay rendering for UI elements

## Architecture

```
┌─────────────────────────────────────────────────────────┐
│              CompositorRenderer                         │
│  - Manages outputs and scene graph                      │
│  - Coordinates rendering across GPUs                    │
│  - Handles damage tracking                              │
└────────────────────┬────────────────────────────────────┘
                     │
┌────────────────────▼────────────────────────────────────┐
│              VulkanShaderManager                        │
│  - Shader module lifecycle                              │
│  - Pipeline creation                                    │
│  - Descriptor management                                │
└────────────────────┬────────────────────────────────────┘
                     │
┌────────────────────▼────────────────────────────────────┐
│           VulkanRenderer (Core)                         │
│  - Device/instance management                           │
│  - Swapchain handling                                   │
│  - Command buffer recording                             │
└─────────────────────────────────────────────────────────┘
```

## Components

### 1. VulkanRenderer

Core Vulkan rendering functionality:

```c
// Initialize renderer
VulkanRenderer* vulkan_renderer_create(const VulkanRendererConfig* config);
void vulkan_renderer_destroy(VulkanRenderer* renderer);

// Frame rendering
bool vulkan_renderer_begin_frame(VulkanRenderer* renderer, int width, int height);
bool vulkan_renderer_end_frame(VulkanRenderer* renderer);

// Clear color
void vulkan_renderer_set_clear_color(VulkanRenderer* renderer, 
                                     float r, float g, float b, float a);
```

**Features:**
- Vulkan 1.4 support with 1.2/1.3 fallback
- Automatic GPU selection
- VSync control
- Frame timing statistics
- Validation layer support

### 2. VulkanShader System

Complete shader management with debugging:

```c
// Initialize shader manager
VulkanShaderError vulkan_shader_manager_init(
    VulkanShaderManager* manager,
    VkDevice device,
    VkPhysicalDevice physical_device,
    bool enable_debug,
    bool enable_validation);

// Load shader from file
VulkanShaderModule* vulkan_shader_create_module_from_file(
    VulkanShaderManager* manager,
    const char* path,
    VkShaderStageFlagBits stage,
    const char* name,
    const char* entry_point);

// Create graphics pipeline
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
```

**Features:**
- File-based SPIR-V loading
- Shader validation
- Reference counting
- Debug callbacks
- Exception-style error handling

### 3. Error Handling

Use exception-style macros for robust error handling:

```c
// Try-catch style
VULKAN_SHADER_TRY(vulkan_shader_create_pipeline(...));

// Assert with error reporting
VULKAN_SHADER_ASSERT(module != NULL, &manager, 
                    VULKAN_SHADER_ERROR_INVALID_CODE, "NULL module");

// Throw error with callback
VULKAN_SHADER_THROW(&manager, VULKAN_SHADER_ERROR_OUT_OF_MEMORY, 
                   "Allocation failed");
```

**Error Codes:**
- `VULKAN_SHADER_OK` - Success
- `VULKAN_SHADER_ERROR_INVALID_DEVICE` - Invalid Vulkan device
- `VULKAN_SHADER_ERROR_INVALID_CODE` - Invalid shader code
- `VULKAN_SHADER_ERROR_COMPILATION_FAILED` - Shader compilation failed
- `VULKAN_SHADER_ERROR_LINK_FAILED` - Pipeline linking failed
- `VULKAN_SHADER_ERROR_OUT_OF_MEMORY` - Memory allocation failed
- `VULKAN_SHADER_ERROR_FILE_NOT_FOUND` - Shader file not found
- `VULKAN_SHADER_ERROR_INVALID_STAGE` - Invalid shader stage
- `VULKAN_SHADER_ERROR_MODULE_LIMIT` - Too many shader modules
- `VULKAN_SHADER_ERROR_DEBUG_FAILED` - Debug operation failed

### 4. Debug Utilities

Comprehensive debugging support:

```c
// Set debug callback
void vulkan_shader_manager_set_debug_callback(
    VulkanShaderManager* manager,
    VulkanShaderDebugCallback callback,
    void* user_data);

// Get shader disassembly
char* vulkan_shader_get_disassembly(VulkanShaderModule* module);

// Print shader info
void vulkan_shader_print_info(VulkanShaderModule* module);

// Validate all shaders
int vulkan_shader_manager_validate_all(VulkanShaderManager* manager);
```

**Debug Features:**
- Source location tracking (file, line, function)
- Human-readable shader names
- Custom error logging callbacks
- SPIR-V disassembly
- RenderDoc/Nsight integration via debug names

### 5. Built-in Shaders

Pre-built shader pipelines for common operations:

```c
// Colored quad (for solid color rectangles)
VulkanShaderError vulkan_shader_create_quad_pipeline(
    VulkanShaderManager* manager,
    VkRenderPass render_pass,
    VulkanShaderPipeline* out_pipeline);

// Textured quad (for window textures)
VulkanShaderError vulkan_shader_create_textured_pipeline(
    VulkanShaderManager* manager,
    VkRenderPass render_pass,
    VkDescriptorSetLayout texture_descriptor_layout,
    VulkanShaderPipeline* out_pipeline);

// Kawase blur (for post-processing effects)
VulkanShaderError vulkan_shader_create_blur_pipeline(
    VulkanShaderManager* manager,
    VkRenderPass render_pass,
    VkDescriptorSetLayout texture_descriptor_layout,
    int blur_pass,
    float blur_strength,
    VulkanShaderPipeline* out_pipeline);
```

### 6. Multi-GPU Support

Handle systems with multiple GPUs:

```c
// Create GPU manager
MultiGPUManager* multigpu_manager_create(void);
void multigpu_manager_cleanup(MultiGPUManager* manager);

// Get GPU info
uint32_t multigpu_enumerate_gpus(MultiGPUManager* manager, GPUInfo** gpus);
GPUInfo* multigpu_get_gpu(MultiGPUManager* manager, uint32_t index);

// Set selection policy
void multigpu_set_policy(MultiGPUManager* manager, GPUPolicy policy);

// Transfer buffers between GPUs
bool multigpu_transfer_buffer(MultiGPUManager* manager,
                              void* buffer,
                              uint32_t src_gpu,
                              uint32_t dst_gpu,
                              size_t size);
```

**GPU Policies:**
- `GPU_POLICY_AUTO` - Auto-select best GPU (default)
- `GPU_POLICY_DISCRETE` - Prefer discrete GPU
- `GPU_POLICY_INTEGRATED` - Prefer integrated GPU
- `GPU_POLICY_LOW_POWER` - Prefer low-power GPU
- `GPU_POLICY_HIGH_PERFORMANCE` - Prefer high-performance GPU

### 7. Damage Tracking

Efficient partial screen updates:

```c
// Create damage tracker
DamageTracker* damage_tracker_create(int screen_width, int screen_height);
void damage_tracker_destroy(DamageTracker* tracker);

// Add damage regions
void damage_tracker_add_damage(DamageTracker* tracker, 
                               int x, int y, int width, int height);
void damage_tracker_add_full_damage(DamageTracker* tracker);

// Get damaged regions
const DamageRegion* damage_tracker_get_regions(DamageTracker* tracker,
                                               uint32_t* out_count);

// Clear after rendering
void damage_tracker_clear(DamageTracker* tracker);
```

**Features:**
- Region merging for efficiency
- Configurable max age (forced full redraw)
- Statistics tracking
- Intersection/union operations

## Usage Example

```c
#include <wm/render/VulkanRenderer.h>
#include <wm/render/VulkanShader.h>
#include <wm/render/DamageTracker.h>

int main(void) {
    // Initialize Vulkan renderer
    VulkanRendererConfig config = {
        .useVulkan = true,
        .enableVSync = true,
        .enableDamageTracking = true,
        .targetFrameRate = 0,  // Match display
        .maxFrameLatency = 1
    };
    
    VulkanRenderer* renderer = vulkan_renderer_create(&config);
    if (!renderer) {
        fprintf(stderr, "Failed to create Vulkan renderer\n");
        return 1;
    }
    
    // Initialize shader manager
    VulkanShaderManager shader_mgr;
    vulkan_shader_manager_init(&shader_mgr, 
                              vulkan_renderer_get_device(renderer),
                              vulkan_renderer_get_physical_device(renderer),
                              true,   // Enable debug
                              true);  // Enable validation
    
    // Set debug callback
    vulkan_shader_manager_set_debug_callback(&shader_mgr, 
                                            my_debug_callback, NULL);
    
    // Create damage tracker
    DamageTracker* damage = damage_tracker_create(1920, 1080);
    
    // Main rendering loop
    bool running = true;
    while (running) {
        // Begin frame
        if (!vulkan_renderer_begin_frame(renderer, 1920, 1080)) {
            continue;
        }
        
        // Add damage from window updates
        damage_tracker_add_damage(damage, x, y, w, h);
        
        // Get damaged regions
        uint32_t count;
        const DamageRegion* regions = damage_tracker_get_regions(damage, &count);
        
        // Render only damaged regions
        for (uint32_t i = 0; i < count; i++) {
            const DamageRegion* region = &regions[i];
            
            // Set scissor to damage region
            // Render scene...
        }
        
        // End frame
        vulkan_renderer_end_frame(renderer);
        
        // Clear damage
        damage_tracker_clear(damage);
    }
    
    // Cleanup
    damage_tracker_destroy(damage);
    vulkan_shader_manager_cleanup(&shader_mgr);
    vulkan_renderer_destroy(renderer);
    
    return 0;
}
```

## File Structure

```
src/wm/render/
├── VulkanRenderer.c          # Core Vulkan rendering
├── VulkanRenderer.h          # Renderer header
├── VulkanRendererBridge.h    # C API bridge
├── VulkanShader.h            # Shader system header
├── VulkanShaderModule.c      # Shader module management
├── VulkanShaderDebug.c       # Debug utilities
├── VulkanShaderPipeline.c    # Pipeline creation
├── VulkanShaderBuiltin.c     # Built-in shaders
├── VulkanSceneCompositor.c   # Scene graph rendering
├── VulkanOverlayRenderer.c   # Overlay/UI rendering
├── VulkanDmaBuf.c            # DMA-BUF import
├── MultiGPU.c                # Multi-GPU support
├── DamageTracker.c           # Damage tracking
├── GLES2Renderer.c           # GLES2 fallback
└── Common.h                  # Shared utilities
```

## Shader File Format

Shaders are loaded from SPIR-V files:

```
/usr/share/havel-wm/shaders/
├── quad.vert.spv         # Quad vertex shader
├── quad.frag.spv         # Quad fragment shader
├── textured.vert.spv     # Textured quad vertex
├── textured.frag.spv     # Textured quad fragment
├── blur.vert.spv         # Blur vertex shader
└── blur.frag.spv         # Blur fragment shader
```

## Performance Considerations

1. **Damage Tracking** - Only redraw changed regions
2. **Pipeline Cache** - Reuse compiled pipelines
3. **Descriptor Pools** - Pre-allocate descriptor sets
4. **Command Buffers** - Record once, submit multiple times
5. **Multi-GPU** - Offload work to discrete GPU when available
6. **VSync** - Enable to prevent tearing (disable for lowest latency)

## Debugging

Enable validation layers for detailed error reporting:

```c
VulkanShaderManager shader_mgr;
vulkan_shader_manager_init(&shader_mgr, device, physical_device,
                          true,   // Enable debug
                          true);  // Enable validation
```

Use RenderDoc or Nsight for GPU debugging - shader names are automatically set for easy identification.

## Troubleshooting

**Problem:** Black screen
- Check Vulkan instance creation
- Verify swapchain creation
- Ensure render pass is configured correctly
- Check validation layer output

**Problem:** Shader compilation errors
- Verify SPIR-V files exist
- Check shader stage flags match
- Ensure entry point name is correct
- Use `vulkan_shader_get_disassembly()` to inspect

**Problem:** Poor performance
- Enable damage tracking
- Check VSync setting
- Verify multi-GPU policy
- Use pipeline cache
- Reduce overdraw

## References

- [Vulkan Specification](https://www.khronos.org/registry/vulkan/specs/)
- [SPIR-V Specification](https://www.khronos.org/registry/SPIR-V/specs/)
- [RenderDoc Documentation](https://renderdoc.org/docs/)
- [Nsight Graphics](https://developer.nvidia.com/nsight-graphics)
