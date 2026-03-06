// Vulkan Renderer Implementation - C only

#include "VulkanRendererBridge.h"
#include <vulkan/vulkan.h>
#include <Logger.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

// Internal structures (not exposed in header)
struct VulkanRendererInternal {
    VkInstance instance;
    VkDebugUtilsMessengerEXT debugMessenger;
    VkPhysicalDevice physicalDevice;
    VkDevice device;
    uint32_t graphicsFamilyIndex;
    uint32_t presentFamilyIndex;
    VkQueue graphicsQueue;
    VkQueue presentQueue;
    VkSwapchainKHR swapchain;
    VkFormat swapchainFormat;
    VkExtent2D swapchainExtent;
    VkImage* swapchainImages;
    VkImageView* swapchainImageViews;
    uint32_t swapchainImageCount;
    VkRenderPass renderPass;
    VkPipelineLayout pipelineLayout;
    VkFramebuffer* framebuffers;
    VkCommandPool commandPool;
    VkCommandBuffer* commandBuffers;
    VkSemaphore* imageAvailableSemaphores;
    VkSemaphore* renderFinishedSemaphores;
    VkFence* inFlightFences;
    uint32_t currentFrame;
    bool framebufferResized;
    VulkanRendererConfig config;
    float clearColor[4];
};

struct VulkanTextureInternal {
    VkImage image;
    VkImageView view;
    VkSampler sampler;
    VkDeviceMemory memory;
    uint32_t width;
    uint32_t height;
    VkFormat format;
};

// Validation layers
static const char* g_validationLayers[] = {
    "VK_LAYER_KHRONOS_validation"
};

// Device extensions
static const char* g_deviceExtensions[] = {
    VK_KHR_SWAPCHAIN_EXTENSION_NAME
};
static const uint32_t g_deviceExtensionCount = 1;

// Debug callback
static VKAPI_ATTR VkBool32 VKAPI_CALL vulkan_debug_callback(
    VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
    VkDebugUtilsMessageTypeFlagsEXT messageType,
    const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
    void* pUserData) {
    (void)messageType;
    (void)pUserData;
    if (messageSeverity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
        LOG_ERROR("[Vulkan] %s", pCallbackData->pMessage);
    }
    return VK_FALSE;
}

// Check validation layer support
static bool check_validation_layer_support(void) {
    uint32_t layerCount;
    vkEnumerateInstanceLayerProperties(&layerCount, NULL);
    VkLayerProperties* availableLayers = 
        (VkLayerProperties*)malloc(layerCount * sizeof(VkLayerProperties));
    vkEnumerateInstanceLayerProperties(&layerCount, availableLayers);
    
    bool found = false;
    for (uint32_t i = 0; i < layerCount; i++) {
        if (strcmp(g_validationLayers[0], availableLayers[i].layerName) == 0) {
            found = true;
            break;
        }
    }
    free(availableLayers);
    return found;
}

// Get required extensions
static const char** get_required_extensions(uint32_t* extensionCount) {
    static const char* extensions[] = {
        VK_KHR_SURFACE_EXTENSION_NAME,
#ifdef VK_USE_PLATFORM_WAYLAND_KHR
        VK_KHR_WAYLAND_SURFACE_EXTENSION_NAME,
#endif
    };
    *extensionCount = sizeof(extensions) / sizeof(extensions[0]);
    return extensions;
}

// Rate device suitability
static int rate_device_suitability(VkPhysicalDevice device) {
    VkPhysicalDeviceProperties props;
    VkPhysicalDeviceFeatures features;
    vkGetPhysicalDeviceProperties(device, &props);
    vkGetPhysicalDeviceFeatures(device, &features);
    
    int score = 0;
    if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
        score += 1000;
    }
    score += props.limits.maxImageDimension2D;
    if (!features.geometryShader) return 0;
    return score;
}

// Check device extension support
static bool check_device_extension_support(VkPhysicalDevice device) {
    uint32_t extensionCount;
    vkEnumerateDeviceExtensionProperties(device, NULL, &extensionCount, NULL);
    VkExtensionProperties* available = 
        (VkExtensionProperties*)malloc(extensionCount * sizeof(VkExtensionProperties));
    vkEnumerateDeviceExtensionProperties(device, NULL, &extensionCount, available);
    
    for (uint32_t i = 0; i < g_deviceExtensionCount; i++) {
        bool found = false;
        for (uint32_t j = 0; j < extensionCount; j++) {
            if (strcmp(g_deviceExtensions[i], available[j].extensionName) == 0) {
                found = true;
                break;
            }
        }
        if (!found) { free(available); return false; }
    }
    free(available);
    return true;
}

// Find queue families
static void find_queue_families(VkPhysicalDevice device,
                                uint32_t* graphicsFamily, uint32_t* presentFamily) {
    uint32_t queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, NULL);
    VkQueueFamilyProperties* families = 
        (VkQueueFamilyProperties*)malloc(queueFamilyCount * sizeof(VkQueueFamilyProperties));
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, families);
    
    *graphicsFamily = UINT32_MAX;
    *presentFamily = UINT32_MAX;
    
    for (uint32_t i = 0; i < queueFamilyCount; i++) {
        if (families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            *graphicsFamily = i;
            *presentFamily = i;  // Use same family for simplicity
            break;
        }
    }
    free(families);
}

// Create instance
static VkResult create_instance(struct VulkanRendererInternal* renderer,
                                const VulkanRendererConfig* config) {
    VkApplicationInfo appInfo = {0};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "Havel WM";
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName = "Havel Vulkan";
    appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion = VK_API_VERSION_1_2;
    
    VkInstanceCreateInfo createInfo = {0};
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &appInfo;
    
    uint32_t extensionCount;
    const char** extensions = get_required_extensions(&extensionCount);
    createInfo.enabledExtensionCount = extensionCount;
    createInfo.ppEnabledExtensionNames = extensions;
    
    if (config->enableValidation) {
        if (!check_validation_layer_support()) {
            LOG_ERROR("[Vulkan] Validation layers not available");
            return VK_ERROR_LAYER_NOT_PRESENT;
        }
        createInfo.enabledLayerCount = 1;
        createInfo.ppEnabledLayerNames = g_validationLayers;
        
        VkDebugUtilsMessengerCreateInfoEXT debugCreateInfo = {0};
        debugCreateInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
        debugCreateInfo.messageSeverity = 
            VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        debugCreateInfo.messageType = 
            VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT;
        debugCreateInfo.pfnUserCallback = vulkan_debug_callback;
        createInfo.pNext = &debugCreateInfo;
    } else {
        createInfo.enabledLayerCount = 0;
    }
    
    VkResult result = vkCreateInstance(&createInfo, NULL, &renderer->instance);
    if (result != VK_SUCCESS) {
        LOG_ERROR("[Vulkan] Failed to create instance: %d", result);
        return result;
    }
    LOG_INFO("[Vulkan] Instance created");
    return VK_SUCCESS;
}

// Setup debug messenger
static VkResult setup_debug_messenger(struct VulkanRendererInternal* renderer) {
    if (!renderer->config.enableValidation) return VK_SUCCESS;
    
    VkDebugUtilsMessengerCreateInfoEXT createInfo = {0};
    createInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    createInfo.messageSeverity = 
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    createInfo.messageType = 
        VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT;
    createInfo.pfnUserCallback = vulkan_debug_callback;
    
    PFN_vkCreateDebugUtilsMessengerEXT func = 
        (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(
            renderer->instance, "vkCreateDebugUtilsMessengerEXT");
    if (!func) {
        LOG_WARN("[Vulkan] Debug messenger not available");
        return VK_SUCCESS;
    }
    
    VkResult result = func(renderer->instance, &createInfo, NULL, &renderer->debugMessenger);
    if (result != VK_SUCCESS) {
        LOG_WARN("[Vulkan] Failed to create debug messenger: %d", result);
    }
    return result;
}

// Pick physical device
static VkResult pick_physical_device(struct VulkanRendererInternal* renderer) {
    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(renderer->instance, &deviceCount, NULL);
    if (deviceCount == 0) {
        LOG_ERROR("[Vulkan] No GPUs with Vulkan support");
        return VK_ERROR_INCOMPATIBLE_DRIVER;
    }
    
    VkPhysicalDevice* devices = 
        (VkPhysicalDevice*)malloc(deviceCount * sizeof(VkPhysicalDevice));
    vkEnumeratePhysicalDevices(renderer->instance, &deviceCount, devices);
    
    int bestScore = 0;
    VkPhysicalDevice bestDevice = VK_NULL_HANDLE;
    
    for (uint32_t i = 0; i < deviceCount; i++) {
        int score = rate_device_suitability(devices[i]);
        if (score > bestScore && check_device_extension_support(devices[i])) {
            bestScore = score;
            bestDevice = devices[i];
        }
    }
    free(devices);
    
    if (bestDevice == VK_NULL_HANDLE) {
        LOG_ERROR("[Vulkan] No suitable GPU found");
        return VK_ERROR_INCOMPATIBLE_DRIVER;
    }
    
    renderer->physicalDevice = bestDevice;
    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(bestDevice, &props);
    LOG_INFO("[Vulkan] Selected GPU: %s (score: %d)", props.deviceName, bestScore);
    return VK_SUCCESS;
}

// Create logical device
static VkResult create_logical_device(struct VulkanRendererInternal* renderer) {
    uint32_t graphicsFamily, presentFamily;
    find_queue_families(renderer->physicalDevice, &graphicsFamily, &presentFamily);
    
    if (graphicsFamily == UINT32_MAX) {
        LOG_ERROR("[Vulkan] No graphics queue family");
        return VK_ERROR_INCOMPATIBLE_DRIVER;
    }
    
    renderer->graphicsFamilyIndex = graphicsFamily;
    renderer->presentFamilyIndex = presentFamily;
    
    float queuePriority = 1.0f;
    VkDeviceQueueCreateInfo queueCreateInfo = {0};
    queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queueCreateInfo.queueFamilyIndex = graphicsFamily;
    queueCreateInfo.queueCount = 1;
    queueCreateInfo.pQueuePriorities = &queuePriority;
    
    VkDeviceCreateInfo createInfo = {0};
    createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    createInfo.queueCreateInfoCount = 1;
    createInfo.pQueueCreateInfos = &queueCreateInfo;
    createInfo.enabledExtensionCount = 1;
    createInfo.ppEnabledExtensionNames = g_deviceExtensions;
    
    VkResult result = vkCreateDevice(renderer->physicalDevice, &createInfo, NULL, &renderer->device);
    if (result != VK_SUCCESS) {
        LOG_ERROR("[Vulkan] Failed to create logical device: %d", result);
        return result;
    }
    
    vkGetDeviceQueue(renderer->device, graphicsFamily, 0, &renderer->graphicsQueue);
    vkGetDeviceQueue(renderer->device, presentFamily, 0, &renderer->presentQueue);
    LOG_INFO("[Vulkan] Logical device created");
    return VK_SUCCESS;
}

// Create swapchain
static VkResult create_swapchain(struct VulkanRendererInternal* renderer,
                                 uint32_t width, uint32_t height) {
    VkExtent2D extent = {width, height};
    uint32_t imageCount = renderer->config.desiredImageCount;
    if (imageCount < 2) imageCount = 2;
    if (imageCount > 8) imageCount = 8;
    
    VkSwapchainCreateInfoKHR createInfo = {0};
    createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    createInfo.minImageCount = imageCount;
    createInfo.imageFormat = VK_FORMAT_B8G8R8A8_SRGB;
    createInfo.imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    createInfo.imageExtent = extent;
    createInfo.imageArrayLayers = 1;
    createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    createInfo.preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
    createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    createInfo.presentMode = renderer->config.enableVSync ? 
        VK_PRESENT_MODE_FIFO_KHR : VK_PRESENT_MODE_MAILBOX_KHR;
    createInfo.clipped = VK_TRUE;
    
    VkResult result = vkCreateSwapchainKHR(renderer->device, &createInfo, NULL, &renderer->swapchain);
    if (result != VK_SUCCESS) {
        LOG_ERROR("[Vulkan] Failed to create swapchain: %d", result);
        return result;
    }
    
    renderer->swapchainFormat = createInfo.imageFormat;
    renderer->swapchainExtent = extent;
    
    vkGetSwapchainImagesKHR(renderer->device, renderer->swapchain, &imageCount, NULL);
    renderer->swapchainImages = (VkImage*)malloc(imageCount * sizeof(VkImage));
    vkGetSwapchainImagesKHR(renderer->device, renderer->swapchain, &imageCount, renderer->swapchainImages);
    renderer->swapchainImageCount = imageCount;
    
    LOG_INFO("[Vulkan] Swapchain created: %dx%d, %d images", extent.width, extent.height, imageCount);
    return VK_SUCCESS;
}

// Create image views
static VkResult create_image_views(struct VulkanRendererInternal* renderer) {
    renderer->swapchainImageViews = 
        (VkImageView*)malloc(renderer->swapchainImageCount * sizeof(VkImageView));
    
    for (uint32_t i = 0; i < renderer->swapchainImageCount; i++) {
        VkImageViewCreateInfo createInfo = {0};
        createInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        createInfo.image = renderer->swapchainImages[i];
        createInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        createInfo.format = renderer->swapchainFormat;
        createInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        createInfo.subresourceRange.baseMipLevel = 0;
        createInfo.subresourceRange.levelCount = 1;
        createInfo.subresourceRange.baseArrayLayer = 0;
        createInfo.subresourceRange.layerCount = 1;
        
        VkResult result = vkCreateImageView(renderer->device, &createInfo, NULL, 
                                           &renderer->swapchainImageViews[i]);
        if (result != VK_SUCCESS) {
            LOG_ERROR("[Vulkan] Failed to create image view: %d", result);
            return result;
        }
    }
    LOG_INFO("[Vulkan] Image views created");
    return VK_SUCCESS;
}

// Create render pass
static VkResult create_render_pass(struct VulkanRendererInternal* renderer) {
    VkAttachmentDescription colorAttachment = {0};
    colorAttachment.format = renderer->swapchainFormat;
    colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    
    VkAttachmentReference colorAttachmentRef = {0};
    colorAttachmentRef.attachment = 0;
    colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    
    VkSubpassDescription subpass = {0};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorAttachmentRef;
    
    VkSubpassDependency dependency = {0};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.srcAccessMask = 0;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    
    VkRenderPassCreateInfo createInfo = {0};
    createInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    createInfo.attachmentCount = 1;
    createInfo.pAttachments = &colorAttachment;
    createInfo.subpassCount = 1;
    createInfo.pSubpasses = &subpass;
    createInfo.dependencyCount = 1;
    createInfo.pDependencies = &dependency;
    
    VkResult result = vkCreateRenderPass(renderer->device, &createInfo, NULL, &renderer->renderPass);
    if (result != VK_SUCCESS) {
        LOG_ERROR("[Vulkan] Failed to create render pass: %d", result);
        return result;
    }
    LOG_INFO("[Vulkan] Render pass created");
    return VK_SUCCESS;
}

// Create graphics pipeline (placeholder)
static VkResult create_graphics_pipeline(struct VulkanRendererInternal* renderer) {
    VkPipelineLayoutCreateInfo createInfo = {0};
    createInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    
    VkResult result = vkCreatePipelineLayout(renderer->device, &createInfo, NULL, 
                                            &renderer->pipelineLayout);
    if (result != VK_SUCCESS) {
        LOG_ERROR("[Vulkan] Failed to create pipeline layout: %d", result);
        return result;
    }
    LOG_INFO("[Vulkan] Pipeline layout created");
    return VK_SUCCESS;
}

// Create framebuffers
static VkResult create_framebuffers(struct VulkanRendererInternal* renderer) {
    renderer->framebuffers = 
        (VkFramebuffer*)malloc(renderer->swapchainImageCount * sizeof(VkFramebuffer));
    
    for (uint32_t i = 0; i < renderer->swapchainImageCount; i++) {
        VkFramebufferCreateInfo createInfo = {0};
        createInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        createInfo.renderPass = renderer->renderPass;
        createInfo.attachmentCount = 1;
        createInfo.pAttachments = &renderer->swapchainImageViews[i];
        createInfo.width = renderer->swapchainExtent.width;
        createInfo.height = renderer->swapchainExtent.height;
        createInfo.layers = 1;
        
        VkResult result = vkCreateFramebuffer(renderer->device, &createInfo, NULL, 
                                             &renderer->framebuffers[i]);
        if (result != VK_SUCCESS) {
            LOG_ERROR("[Vulkan] Failed to create framebuffer: %d", result);
            return result;
        }
    }
    LOG_INFO("[Vulkan] Framebuffers created");
    return VK_SUCCESS;
}

// Create command pool
static VkResult create_command_pool(struct VulkanRendererInternal* renderer) {
    VkCommandPoolCreateInfo createInfo = {0};
    createInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    createInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    createInfo.queueFamilyIndex = renderer->graphicsFamilyIndex;
    
    VkResult result = vkCreateCommandPool(renderer->device, &createInfo, NULL, 
                                         &renderer->commandPool);
    if (result != VK_SUCCESS) {
        LOG_ERROR("[Vulkan] Failed to create command pool: %d", result);
        return result;
    }
    LOG_INFO("[Vulkan] Command pool created");
    return VK_SUCCESS;
}

// Create command buffers
static VkResult create_command_buffers(struct VulkanRendererInternal* renderer) {
    renderer->commandBuffers = 
        (VkCommandBuffer*)malloc(renderer->swapchainImageCount * sizeof(VkCommandBuffer));
    
    VkCommandBufferAllocateInfo allocInfo = {0};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = renderer->commandPool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = renderer->swapchainImageCount;
    
    VkResult result = vkAllocateCommandBuffers(renderer->device, &allocInfo, renderer->commandBuffers);
    if (result != VK_SUCCESS) {
        LOG_ERROR("[Vulkan] Failed to create command buffers: %d", result);
        return result;
    }
    LOG_INFO("[Vulkan] Command buffers created");
    return VK_SUCCESS;
}

// Create sync objects
static VkResult create_sync_objects(struct VulkanRendererInternal* renderer) {
    renderer->imageAvailableSemaphores = 
        (VkSemaphore*)malloc(renderer->swapchainImageCount * sizeof(VkSemaphore));
    renderer->renderFinishedSemaphores = 
        (VkSemaphore*)malloc(renderer->swapchainImageCount * sizeof(VkSemaphore));
    renderer->inFlightFences = 
        (VkFence*)malloc(renderer->swapchainImageCount * sizeof(VkFence));
    
    VkSemaphoreCreateInfo semaphoreInfo = {0};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    
    VkFenceCreateInfo fenceInfo = {0};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    
    for (size_t i = 0; i < renderer->swapchainImageCount; i++) {
        if (vkCreateSemaphore(renderer->device, &semaphoreInfo, NULL, 
                             &renderer->imageAvailableSemaphores[i]) != VK_SUCCESS ||
            vkCreateSemaphore(renderer->device, &semaphoreInfo, NULL, 
                             &renderer->renderFinishedSemaphores[i]) != VK_SUCCESS ||
            vkCreateFence(renderer->device, &fenceInfo, NULL, 
                         &renderer->inFlightFences[i]) != VK_SUCCESS) {
            LOG_ERROR("[Vulkan] Failed to create sync objects");
            return VK_ERROR_INITIALIZATION_FAILED;
        }
    }
    LOG_INFO("[Vulkan] Synchronization objects created");
    return VK_SUCCESS;
}

// ============================================================================
// Public API Implementation
// ============================================================================

VulkanRenderer* vulkan_renderer_create(const VulkanRendererConfig* config) {
    if (!config) {
        LOG_ERROR("[Vulkan] Invalid config");
        return NULL;
    }
    
    struct VulkanRendererInternal* renderer = 
        (struct VulkanRendererInternal*)calloc(1, sizeof(struct VulkanRendererInternal));
    if (!renderer) {
        LOG_ERROR("[Vulkan] Failed to allocate renderer");
        return NULL;
    }
    
    renderer->config = *config;
    renderer->clearColor[0] = 0.0f;
    renderer->clearColor[1] = 0.0f;
    renderer->clearColor[2] = 0.0f;
    renderer->clearColor[3] = 1.0f;
    
    LOG_INFO("[Vulkan] Creating renderer...");
    
    VkResult result;
    
    result = create_instance(renderer, config);
    if (result != VK_SUCCESS) { free(renderer); return NULL; }
    
    result = setup_debug_messenger(renderer);
    if (result != VK_SUCCESS) { vulkan_renderer_destroy((VulkanRenderer*)renderer); return NULL; }
    
    result = pick_physical_device(renderer);
    if (result != VK_SUCCESS) { vulkan_renderer_destroy((VulkanRenderer*)renderer); return NULL; }
    
    result = create_logical_device(renderer);
    if (result != VK_SUCCESS) { vulkan_renderer_destroy((VulkanRenderer*)renderer); return NULL; }
    
    result = create_swapchain(renderer, 1920, 1080);
    if (result != VK_SUCCESS) { vulkan_renderer_destroy((VulkanRenderer*)renderer); return NULL; }
    
    result = create_image_views(renderer);
    if (result != VK_SUCCESS) { vulkan_renderer_destroy((VulkanRenderer*)renderer); return NULL; }
    
    result = create_render_pass(renderer);
    if (result != VK_SUCCESS) { vulkan_renderer_destroy((VulkanRenderer*)renderer); return NULL; }
    
    result = create_graphics_pipeline(renderer);
    if (result != VK_SUCCESS) { vulkan_renderer_destroy((VulkanRenderer*)renderer); return NULL; }
    
    result = create_framebuffers(renderer);
    if (result != VK_SUCCESS) { vulkan_renderer_destroy((VulkanRenderer*)renderer); return NULL; }
    
    result = create_command_pool(renderer);
    if (result != VK_SUCCESS) { vulkan_renderer_destroy((VulkanRenderer*)renderer); return NULL; }
    
    result = create_command_buffers(renderer);
    if (result != VK_SUCCESS) { vulkan_renderer_destroy((VulkanRenderer*)renderer); return NULL; }
    
    result = create_sync_objects(renderer);
    if (result != VK_SUCCESS) { vulkan_renderer_destroy((VulkanRenderer*)renderer); return NULL; }
    
    renderer->currentFrame = 0;
    renderer->framebufferResized = false;
    
    LOG_INFO("[Vulkan] Renderer created successfully");
    return (VulkanRenderer*)renderer;
}

void vulkan_renderer_destroy(VulkanRenderer* renderer_ptr) {
    struct VulkanRendererInternal* renderer = (struct VulkanRendererInternal*)renderer_ptr;
    if (!renderer) return;
    
    vulkan_renderer_wait_idle(renderer_ptr);
    
    if (renderer->device != VK_NULL_HANDLE) {
        for (size_t i = 0; i < renderer->swapchainImageCount; i++) {
            if (renderer->imageAvailableSemaphores[i]) {
                vkDestroySemaphore(renderer->device, renderer->imageAvailableSemaphores[i], NULL);
            }
            if (renderer->renderFinishedSemaphores[i]) {
                vkDestroySemaphore(renderer->device, renderer->renderFinishedSemaphores[i], NULL);
            }
            if (renderer->inFlightFences[i]) {
                vkDestroyFence(renderer->device, renderer->inFlightFences[i], NULL);
            }
        }
        free(renderer->imageAvailableSemaphores);
        free(renderer->renderFinishedSemaphores);
        free(renderer->inFlightFences);
        
        if (renderer->commandPool) {
            vkDestroyCommandPool(renderer->device, renderer->commandPool, NULL);
        }
        free(renderer->commandBuffers);
        
        if (renderer->framebuffers) {
            for (uint32_t i = 0; i < renderer->swapchainImageCount; i++) {
                vkDestroyFramebuffer(renderer->device, renderer->framebuffers[i], NULL);
            }
            free(renderer->framebuffers);
        }
        
        if (renderer->pipelineLayout) {
            vkDestroyPipelineLayout(renderer->device, renderer->pipelineLayout, NULL);
        }
        
        if (renderer->renderPass) {
            vkDestroyRenderPass(renderer->device, renderer->renderPass, NULL);
        }
        
        if (renderer->swapchainImageViews) {
            for (uint32_t i = 0; i < renderer->swapchainImageCount; i++) {
                vkDestroyImageView(renderer->device, renderer->swapchainImageViews[i], NULL);
            }
            free(renderer->swapchainImageViews);
        }
        
        if (renderer->swapchain) {
            vkDestroySwapchainKHR(renderer->device, renderer->swapchain, NULL);
        }
        free(renderer->swapchainImages);
        
        vkDestroyDevice(renderer->device, NULL);
    }
    
    if (renderer->debugMessenger) {
        PFN_vkDestroyDebugUtilsMessengerEXT func = 
            (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(
                renderer->instance, "vkDestroyDebugUtilsMessengerEXT");
        if (func) func(renderer->instance, renderer->debugMessenger, NULL);
    }
    
    if (renderer->instance) {
        vkDestroyInstance(renderer->instance, NULL);
    }
    
    memset(renderer, 0, sizeof(struct VulkanRendererInternal));
    free(renderer);
    LOG_INFO("[Vulkan] Renderer destroyed");
}

bool vulkan_renderer_begin_frame(VulkanRenderer* renderer_ptr) {
    struct VulkanRendererInternal* renderer = (struct VulkanRendererInternal*)renderer_ptr;
    if (!renderer || !renderer->device) return false;
    
    vkWaitForFences(renderer->device, 1, &renderer->inFlightFences[renderer->currentFrame], 
                   VK_TRUE, UINT64_MAX);
    
    uint32_t imageIndex;
    VkResult result = vkAcquireNextImageKHR(renderer->device, renderer->swapchain, 
                                           UINT64_MAX,
                                           renderer->imageAvailableSemaphores[renderer->currentFrame],
                                           VK_NULL_HANDLE,
                                           &imageIndex);
    
    if (result == VK_ERROR_OUT_OF_DATE_KHR) {
        vulkan_renderer_resize(renderer_ptr, 1920, 1080);
        return false;
    } else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
        LOG_ERROR("[Vulkan] Failed to acquire image: %d", result);
        return false;
    }
    
    vkResetCommandBuffer(renderer->commandBuffers[imageIndex], 0);
    
    VkCommandBufferBeginInfo beginInfo = {0};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    
    if (vkBeginCommandBuffer(renderer->commandBuffers[imageIndex], &beginInfo) != VK_SUCCESS) {
        LOG_ERROR("[Vulkan] Failed to begin command buffer");
        return false;
    }
    
    return true;
}

bool vulkan_renderer_end_frame(VulkanRenderer* renderer_ptr) {
    struct VulkanRendererInternal* renderer = (struct VulkanRendererInternal*)renderer_ptr;
    if (!renderer || !renderer->device) return false;
    
    uint32_t imageIndex = renderer->currentFrame;
    
    if (vkEndCommandBuffer(renderer->commandBuffers[imageIndex]) != VK_SUCCESS) {
        LOG_ERROR("[Vulkan] Failed to end command buffer");
        return false;
    }
    
    VkSubmitInfo submitInfo = {0};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    
    VkSemaphore waitSemaphores[] = {renderer->imageAvailableSemaphores[renderer->currentFrame]};
    VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = waitSemaphores;
    submitInfo.pWaitDstStageMask = waitStages;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &renderer->commandBuffers[imageIndex];
    
    VkSemaphore signalSemaphores[] = {renderer->renderFinishedSemaphores[renderer->currentFrame]};
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = signalSemaphores;
    
    vkResetFences(renderer->device, 1, &renderer->inFlightFences[renderer->currentFrame]);
    
    if (vkQueueSubmit(renderer->graphicsQueue, 1, &submitInfo, 
                     renderer->inFlightFences[renderer->currentFrame]) != VK_SUCCESS) {
        LOG_ERROR("[Vulkan] Failed to submit command buffer");
        return false;
    }
    
    VkPresentInfoKHR presentInfo = {0};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = signalSemaphores;
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &renderer->swapchain;
    presentInfo.pImageIndices = &imageIndex;
    
    VkResult result = vkQueuePresentKHR(renderer->presentQueue, &presentInfo);
    
    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR || 
        renderer->framebufferResized) {
        renderer->framebufferResized = false;
        vulkan_renderer_resize(renderer_ptr, 1920, 1080);
    } else if (result != VK_SUCCESS) {
        LOG_ERROR("[Vulkan] Failed to present: %d", result);
        return false;
    }
    
    renderer->currentFrame = (renderer->currentFrame + 1) % renderer->swapchainImageCount;
    return true;
}

void vulkan_renderer_wait_idle(VulkanRenderer* renderer_ptr) {
    struct VulkanRendererInternal* renderer = (struct VulkanRendererInternal*)renderer_ptr;
    if (!renderer || !renderer->device) return;
    vkDeviceWaitIdle(renderer->device);
}

void vulkan_renderer_resize(VulkanRenderer* renderer_ptr, uint32_t width, uint32_t height) {
    struct VulkanRendererInternal* renderer = (struct VulkanRendererInternal*)renderer_ptr;
    if (!renderer) return;
    
    vulkan_renderer_wait_idle(renderer_ptr);
    
    for (uint32_t i = 0; i < renderer->swapchainImageCount; i++) {
        vkDestroyImageView(renderer->device, renderer->swapchainImageViews[i], NULL);
        vkDestroyFramebuffer(renderer->device, renderer->framebuffers[i], NULL);
    }
    free(renderer->swapchainImageViews);
    free(renderer->framebuffers);
    free(renderer->swapchainImages);
    
    create_swapchain(renderer, width, height);
    create_image_views(renderer);
    create_framebuffers(renderer);
    
    LOG_INFO("[Vulkan] Resized to %dx%d", width, height);
}

void vulkan_renderer_set_clear_color(VulkanRenderer* renderer_ptr,
                                     float r, float g, float b, float a) {
    struct VulkanRendererInternal* renderer = (struct VulkanRendererInternal*)renderer_ptr;
    if (!renderer) return;
    renderer->clearColor[0] = r;
    renderer->clearColor[1] = g;
    renderer->clearColor[2] = b;
    renderer->clearColor[3] = a;
}

void vulkan_renderer_draw_quad(VulkanRenderer* renderer_ptr,
                               float x, float y, float w, float h) {
    (void)renderer_ptr;
    (void)x; (void)y; (void)w; (void)h;
    // Would record draw commands here
}

VulkanTexture* vulkan_renderer_create_texture_from_buffer(VulkanRenderer* renderer_ptr,
                                                          void* wlr_buffer) {
    struct VulkanRendererInternal* renderer = (struct VulkanRendererInternal*)renderer_ptr;
    (void)wlr_buffer;
    
    struct VulkanTextureInternal* texture = 
        (struct VulkanTextureInternal*)calloc(1, sizeof(struct VulkanTextureInternal));
    if (texture) {
        texture->width = 1920;
        texture->height = 1080;
        texture->format = VK_FORMAT_B8G8R8A8_SRGB;
    }
    return (VulkanTexture*)texture;
}

void vulkan_renderer_destroy_texture(VulkanRenderer* renderer_ptr, VulkanTexture* texture_ptr) {
    struct VulkanRendererInternal* renderer = (struct VulkanRendererInternal*)renderer_ptr;
    struct VulkanTextureInternal* texture = (struct VulkanTextureInternal*)texture_ptr;
    if (!renderer || !texture) return;
    
    if (renderer->device != VK_NULL_HANDLE) {
        if (texture->view) vkDestroyImageView(renderer->device, texture->view, NULL);
        if (texture->sampler) vkDestroySampler(renderer->device, texture->sampler, NULL);
        if (texture->memory) vkFreeMemory(renderer->device, texture->memory, NULL);
    }
    free(texture);
}

void vulkan_renderer_bind_texture(VulkanRenderer* renderer_ptr, VulkanTexture* texture_ptr) {
    (void)renderer_ptr;
    (void)texture_ptr;
    // Would bind texture for rendering
}

const char* vulkan_renderer_get_gpu_info(VulkanRenderer* renderer_ptr) {
    struct VulkanRendererInternal* renderer = (struct VulkanRendererInternal*)renderer_ptr;
    static char info[512];
    
    if (!renderer || !renderer->physicalDevice) {
        return "Vulkan not initialized";
    }
    
    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(renderer->physicalDevice, &props);
    
    snprintf(info, sizeof(info), 
             "GPU: %s\nDriver: %u.%u.%u\nVulkan: %u.%u.%u",
             props.deviceName,
             VK_VERSION_MAJOR(props.driverVersion),
             VK_VERSION_MINOR(props.driverVersion),
             VK_VERSION_PATCH(props.driverVersion),
             VK_VERSION_MAJOR(props.apiVersion),
             VK_VERSION_MINOR(props.apiVersion),
             VK_VERSION_PATCH(props.apiVersion));
    
    return info;
}

bool vulkan_renderer_is_available(void) {
    uint32_t extensionCount;
    vkEnumerateInstanceExtensionProperties(NULL, &extensionCount, NULL);
    return extensionCount > 0;
}

void* vulkan_renderer_get_instance(VulkanRenderer* renderer_ptr) {
    struct VulkanRendererInternal* renderer = (struct VulkanRendererInternal*)renderer_ptr;
    return renderer ? (void*)renderer->instance : NULL;
}

void* vulkan_renderer_get_device(VulkanRenderer* renderer_ptr) {
    struct VulkanRendererInternal* renderer = (struct VulkanRendererInternal*)renderer_ptr;
    return renderer ? (void*)renderer->device : NULL;
}

void* vulkan_renderer_get_physical_device(VulkanRenderer* renderer_ptr) {
    struct VulkanRendererInternal* renderer = (struct VulkanRendererInternal*)renderer_ptr;
    return renderer ? (void*)renderer->physicalDevice : NULL;
}

void* vulkan_renderer_get_graphics_queue(VulkanRenderer* renderer_ptr) {
    struct VulkanRendererInternal* renderer = (struct VulkanRendererInternal*)renderer_ptr;
    return renderer ? (void*)renderer->graphicsQueue : NULL;
}
