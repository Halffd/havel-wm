// Multi-GPU Support Implementation

#include "MultiGPU.h"
#include <Logger.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>
#include <vulkan/vulkan.h>

// Internal GPU manager structure
struct MultiGPUManager {
    GPUInfo gpus[MAX_GPUS];
    uint32_t gpuCount;
    uint32_t activeGPUIndex;
    uint32_t renderGPUIndex;
    uint32_t presentGPUIndex;
    GPUPolicy policy;
    bool initialized;
    
    // Vulkan instance for GPU enumeration
    VkInstance vulkanInstance;
};

// Vendor IDs
#define VENDOR_ID_NVIDIA 0x10DE
#define VENDOR_ID_AMD 0x1002
#define VENDOR_ID_INTEL 0x8086
#define VENDOR_ID_APPLE 0x106B
#define VENDOR_ID_QUALCOMM 0x5143

// Get vendor name from ID
const char* multigpu_get_vendor_name(uint32_t vendor_id) {
    switch (vendor_id) {
        case VENDOR_ID_NVIDIA: return "NVIDIA";
        case VENDOR_ID_AMD: return "AMD";
        case VENDOR_ID_INTEL: return "Intel";
        case VENDOR_ID_APPLE: return "Apple";
        case VENDOR_ID_QUALCOMM: return "Qualcomm";
        default: return "Unknown";
    }
}

// Rate GPU based on policy
static int rate_gpu(const GPUInfo* gpu, GPUPolicy policy) {
    int score = 0;
    
    switch (policy) {
        case GPU_POLICY_DISCRETE:
        case GPU_POLICY_HIGH_PERFORMANCE:
            if (gpu->isDiscrete) score += 1000;
            score += gpu->dedicatedVideoMemory / (1024 * 1024);  // MB
            break;
            
        case GPU_POLICY_INTEGRATED:
        case GPU_POLICY_LOW_POWER:
            if (!gpu->isDiscrete) score += 1000;
            score += gpu->sharedVideoMemory / (1024 * 1024);
            break;
            
        case GPU_POLICY_AUTO:
        default:
            // Auto: prefer discrete with more memory
            if (gpu->isDiscrete) score += 500;
            score += gpu->dedicatedVideoMemory / (1024 * 1024);
            if (gpu->isPrimary) score += 100;
            break;
    }
    
    return score;
}

MultiGPUManager* multigpu_manager_create(void) {
    MultiGPUManager* manager = (MultiGPUManager*)calloc(1, sizeof(MultiGPUManager));
    if (!manager) {
        return NULL;
    }
    
    manager->gpuCount = 0;
    manager->activeGPUIndex = 0;
    manager->renderGPUIndex = 0;
    manager->presentGPUIndex = 0;
    manager->policy = GPU_POLICY_AUTO;
    manager->initialized = false;
    manager->vulkanInstance = VK_NULL_HANDLE;
    
    // Try to enumerate GPUs via Vulkan
    uint32_t api_version;
    VkResult result = vkEnumerateInstanceVersion(&api_version);
    if (result == VK_SUCCESS) {
        // Create Vulkan instance for enumeration
        VkApplicationInfo appInfo = {0};
        appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        appInfo.apiVersion = VK_API_VERSION_1_1;
        
        VkInstanceCreateInfo createInfo = {0};
        createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        createInfo.pApplicationInfo = &appInfo;
        
        result = vkCreateInstance(&createInfo, NULL, &manager->vulkanInstance);
        if (result == VK_SUCCESS) {
            manager->initialized = true;
            LOG_INFO("[MultiGPU] Vulkan instance created for enumeration");
        }
    }
    
    // Enumerate physical devices
    if (manager->vulkanInstance != VK_NULL_HANDLE) {
        uint32_t deviceCount = MAX_GPUS;
        VkPhysicalDevice devices[MAX_GPUS];
        
        result = vkEnumeratePhysicalDevices(manager->vulkanInstance, &deviceCount, devices);
        
        if (result == VK_SUCCESS && deviceCount > 0) {
            manager->gpuCount = deviceCount > MAX_GPUS ? MAX_GPUS : deviceCount;
            
            for (uint32_t i = 0; i < manager->gpuCount; i++) {
                VkPhysicalDeviceProperties props;
                vkGetPhysicalDeviceProperties(devices[i], &props);
                
                GPUInfo* gpu = &manager->gpus[i];
                gpu->index = i;
                strncpy(gpu->name, props.deviceName, sizeof(gpu->name) - 1);
                gpu->vendorId = props.vendorID;
                gpu->deviceId = props.deviceID;
                gpu->isDiscrete = (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU);
                gpu->isPrimary = (i == 0);
                gpu->isActive = true;
                
                strncpy(gpu->vendor, multigpu_get_vendor_name(props.vendorID), 
                       sizeof(gpu->vendor) - 1);
                
                // Estimate memory (Vulkan doesn't directly expose this)
                if (gpu->isDiscrete) {
                    gpu->dedicatedVideoMemory = 4ULL * 1024 * 1024 * 1024;  // Assume 4GB minimum
                    gpu->sharedVideoMemory = 0;
                } else {
                    gpu->dedicatedVideoMemory = 0;
                    gpu->sharedVideoMemory = 2ULL * 1024 * 1024 * 1024;  // Assume 2GB shared
                }
                
                LOG_INFO("[MultiGPU] GPU %u: %s (%s, %s)",
                        i, gpu->name, gpu->vendor,
                        gpu->isDiscrete ? "discrete" : "integrated");
            }
            
            // Auto-select active GPU
            multigpu_auto_select(manager);
        }
    }
    
    // If no GPUs found via Vulkan, create dummy entry
    if (manager->gpuCount == 0) {
        LOG_WARN("[MultiGPU] No GPUs found, creating dummy entry");
        
        GPUInfo* gpu = &manager->gpus[0];
        gpu->index = 0;
        strncpy(gpu->name, "Software Renderer", sizeof(gpu->name) - 1);
        strncpy(gpu->vendor, "Unknown", sizeof(gpu->vendor) - 1);
        gpu->vendorId = 0;
        gpu->deviceId = 0;
        gpu->isDiscrete = false;
        gpu->isPrimary = true;
        gpu->isActive = true;
        gpu->dedicatedVideoMemory = 0;
        gpu->sharedVideoMemory = 512 * 1024 * 1024;  // 512MB
        
        manager->gpuCount = 1;
        manager->activeGPUIndex = 0;
        manager->renderGPUIndex = 0;
        manager->presentGPUIndex = 0;
    }
    
    LOG_INFO("[MultiGPU] Manager created with %u GPU(s)", manager->gpuCount);
    return manager;
}

void multigpu_manager_destroy(MultiGPUManager* manager) {
    if (!manager) return;
    
    if (manager->vulkanInstance != VK_NULL_HANDLE) {
        vkDestroyInstance(manager->vulkanInstance, NULL);
    }
    
    LOG_INFO("[MultiGPU] Manager destroyed");
    free(manager);
}

uint32_t multigpu_enumerate_gpus(MultiGPUManager* manager, GPUInfo** gpus) {
    if (!manager || !gpus) return 0;
    *gpus = manager->gpus;
    return manager->gpuCount;
}

GPUInfo* multigpu_get_gpu(MultiGPUManager* manager, uint32_t index) {
    if (!manager || index >= manager->gpuCount) return NULL;
    return &manager->gpus[index];
}

GPUInfo* multigpu_get_active_gpu(MultiGPUManager* manager) {
    if (!manager) return NULL;
    return &manager->gpus[manager->activeGPUIndex];
}

bool multigpu_set_active_gpu(MultiGPUManager* manager, uint32_t index) {
    if (!manager || index >= manager->gpuCount) return false;
    
    manager->activeGPUIndex = index;
    LOG_INFO("[MultiGPU] Active GPU set to %u: %s", index, manager->gpus[index].name);
    return true;
}

void multigpu_set_policy(MultiGPUManager* manager, GPUPolicy policy) {
    if (!manager) return;
    manager->policy = policy;
    
    // Re-auto-select with new policy
    multigpu_auto_select(manager);
    
    LOG_INFO("[MultiGPU] Policy set to %d", policy);
}

GPUPolicy multigpu_get_policy(MultiGPUManager* manager) {
    return manager ? manager->policy : GPU_POLICY_AUTO;
}

int multigpu_auto_select(MultiGPUManager* manager) {
    if (!manager || manager->gpuCount == 0) return -1;
    
    int bestIndex = 0;
    int bestScore = -1;
    
    for (uint32_t i = 0; i < manager->gpuCount; i++) {
        int score = rate_gpu(&manager->gpus[i], manager->policy);
        if (score > bestScore) {
            bestScore = score;
            bestIndex = i;
        }
    }
    
    manager->activeGPUIndex = bestIndex;
    manager->renderGPUIndex = bestIndex;
    manager->presentGPUIndex = bestIndex;
    
    LOG_INFO("[MultiGPU] Auto-selected GPU %d: %s (score: %d)", 
             bestIndex, manager->gpus[bestIndex].name, bestScore);
    
    return bestIndex;
}

bool multigpu_is_available(MultiGPUManager* manager) {
    return manager && manager->gpuCount > 1;
}

uint32_t multigpu_get_count(MultiGPUManager* manager) {
    return manager ? manager->gpuCount : 0;
}

GPUInfo* multigpu_get_render_gpu(MultiGPUManager* manager) {
    if (!manager) return NULL;
    return &manager->gpus[manager->renderGPUIndex];
}

GPUInfo* multigpu_get_present_gpu(MultiGPUManager* manager) {
    if (!manager) return NULL;
    return &manager->gpus[manager->presentGPUIndex];
}

bool multigpu_transfer_buffer(MultiGPUManager* manager, 
                              void* buffer, 
                              uint32_t src_gpu, 
                              uint32_t dst_gpu,
                              size_t size) {
    if (!manager || !buffer || src_gpu >= manager->gpuCount || 
        dst_gpu >= manager->gpuCount || size == 0) {
        return false;
    }
    
    if (src_gpu == dst_gpu) {
        return true;  // No transfer needed
    }
    
    // In a full implementation, would use:
    // - VK_KHR_external_memory for cross-GPU memory
    // - DMA-BUF for Linux
    // - PCIe peer-to-peer transfers
    
    LOG_DEBUG("[MultiGPU] Buffer transfer %zu bytes: GPU%u -> GPU%u",
             size, src_gpu, dst_gpu);
    
    // For now, just return success (buffer is assumed CPU-accessible)
    return true;
}

void multigpu_get_stats(MultiGPUManager* manager, MultiGPUStats* stats) {
    if (!manager || !stats) return;
    
    stats->totalGPUs = manager->gpuCount;
    stats->activeGPU = manager->activeGPUIndex;
    stats->renderGPU = manager->renderGPUIndex;
    stats->presentGPU = manager->presentGPUIndex;
    stats->policy = manager->policy;
    stats->isMultiGPU = manager->gpuCount > 1;
    
    stats->totalDedicatedMemory = 0;
    stats->totalSharedMemory = 0;
    
    for (uint32_t i = 0; i < manager->gpuCount; i++) {
        stats->totalDedicatedMemory += manager->gpus[i].dedicatedVideoMemory;
        stats->totalSharedMemory += manager->gpus[i].sharedVideoMemory;
    }
}
