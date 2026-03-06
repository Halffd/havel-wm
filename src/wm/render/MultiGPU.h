// Multi-GPU Support - Handle systems with multiple GPUs

#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Maximum number of GPUs supported
#define MAX_GPUS 4

// GPU information
typedef struct {
    uint32_t index;
    char name[256];
    char vendor[64];
    uint32_t vendorId;
    uint32_t deviceId;
    uint64_t dedicatedVideoMemory;  // bytes
    uint64_t sharedVideoMemory;     // bytes
    bool isDiscrete;
    bool isPrimary;
    bool isActive;
} GPUInfo;

// GPU selection policy
typedef enum {
    GPU_POLICY_AUTO = 0,      // Auto-select best GPU
    GPU_POLICY_DISCRETE,       // Prefer discrete GPU
    GPU_POLICY_INTEGRATED,     // Prefer integrated GPU
    GPU_POLICY_LOW_POWER,      // Prefer low-power GPU
    GPU_POLICY_HIGH_PERFORMANCE // Prefer high-performance GPU
} GPUPolicy;

// Multi-GPU manager handle
typedef struct MultiGPUManager MultiGPUManager;

// Create/destroy GPU manager
MultiGPUManager* multigpu_manager_create(void);
void multigpu_manager_destroy(MultiGPUManager* manager);

// Enumerate GPUs
uint32_t multigpu_enumerate_gpus(MultiGPUManager* manager, GPUInfo** gpus);

// Get GPU by index
GPUInfo* multigpu_get_gpu(MultiGPUManager* manager, uint32_t index);

// Get active GPU
GPUInfo* multigpu_get_active_gpu(MultiGPUManager* manager);

// Set active GPU
bool multigpu_set_active_gpu(MultiGPUManager* manager, uint32_t index);

// Set GPU selection policy
void multigpu_set_policy(MultiGPUManager* manager, GPUPolicy policy);
GPUPolicy multigpu_get_policy(MultiGPUManager* manager);

// Auto-select best GPU based on policy
int multigpu_auto_select(MultiGPUManager* manager);

// Check if multi-GPU is available
bool multigpu_is_available(MultiGPUManager* manager);

// Get GPU count
uint32_t multigpu_get_count(MultiGPUManager* manager);

// Get recommended GPU for rendering
GPUInfo* multigpu_get_render_gpu(MultiGPUManager* manager);

// Get recommended GPU for presentation
GPUInfo* multigpu_get_present_gpu(MultiGPUManager* manager);

// Transfer buffer between GPUs
bool multigpu_transfer_buffer(MultiGPUManager* manager, 
                              void* buffer, 
                              uint32_t src_gpu, 
                              uint32_t dst_gpu,
                              size_t size);

// Statistics
typedef struct {
    uint32_t totalGPUs;
    uint32_t activeGPU;
    uint32_t renderGPU;
    uint32_t presentGPU;
    GPUPolicy policy;
    bool isMultiGPU;
    uint64_t totalDedicatedMemory;
    uint64_t totalSharedMemory;
} MultiGPUStats;

void multigpu_get_stats(MultiGPUManager* manager, MultiGPUStats* stats);

// Get GPU vendor name
const char* multigpu_get_vendor_name(uint32_t vendor_id);

#ifdef __cplusplus
}
#endif
