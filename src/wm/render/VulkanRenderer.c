// Vulkan Renderer Implementation

#include "VulkanRenderer.h"
#include <Logger.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

// Validation layers
static const char* g_validationLayers[] = {
    "VK_LAYER_KHRONOS_validation"
};
static const uint32_t g_validationLayerCount = 1;

// Device extensions
static const char* g_deviceExtensions[] = {
    VK_KHR_SWAPCHAIN_EXTENSION_NAME
};
static const uint32_t g_deviceExtensionCount = 1;

// Debug messenger callback
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
#ifdef VK_USE_PLATFORM_XLIB_KHR
        VK_KHR_XLIB_SURFACE_EXTENSION_NAME,
#endif
    };
    
    *extensionCount = sizeof(extensions) / sizeof(extensions[0]);
    return extensions;
}

// Rate device suitability
static int rate_device_suitability(VkPhysicalDevice device) {
    VkPhysicalDeviceProperties deviceProperties;
    VkPhysicalDeviceFeatures deviceFeatures;
    vkGetPhysicalDeviceProperties(device, &deviceProperties);
    vkGetPhysicalDeviceFeatures(device, &deviceFeatures);
    
    int score = 0;
    
    // Discrete GPUs have a significant performance advantage
    if (deviceProperties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
        score += 1000;
    }
    
    // Maximum texture size
    score += deviceProperties.limits.maxImageDimension2D;
    
    // Must support required features
    if (!deviceFeatures.geometryShader) {
        return 0;
    }
    
    return score;
}

// Check device extension support
static bool check_device_extension_support(VkPhysicalDevice device) {
    uint32_t extensionCount;
    vkEnumerateDeviceExtensionProperties(device, NULL, &extensionCount, NULL);
    
    VkExtensionProperties* availableExtensions = 
        (VkExtensionProperties*)malloc(extensionCount * sizeof(VkExtensionProperties));
    vkEnumerateDeviceExtensionProperties(device, NULL, &extensionCount, availableExtensions);
    
    for (uint32_t i = 0; i < g_deviceExtensionCount; i++) {
        bool found = false;
        for (uint32_t j = 0; j < extensionCount; j++) {
            if (strcmp(g_deviceExtensions[i], availableExtensions[j].extensionName) == 0) {
                found = true;
                break;
            }
        }
        if (!found) {
            free(availableExtensions);
            return false;
        }
    }
    
    free(availableExtensions);
    return true;
}

// Find queue families
static void find_queue_families(VkPhysicalDevice device,
                                uint32_t* graphicsFamily,
                                uint32_t* presentFamily) {
    uint32_t queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, NULL);
    
    VkQueueFamilyProperties* queueFamilies = 
        (VkQueueFamilyProperties*)malloc(queueFamilyCount * sizeof(VkQueueFamilyProperties));
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, queueFamilies);
    
    *graphicsFamily = UINT32_MAX;
    *presentFamily = UINT32_MAX;
    
    for (uint32_t i = 0; i < queueFamilyCount; i++) {
        if (queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            *graphicsFamily = i;
        }
        
        // For simplicity, use same family for present if it supports graphics
        if (*graphicsFamily != UINT32_MAX) {
            *presentFamily = *graphicsFamily;
            break;
        }
    }
    
    free(queueFamilies);
}

// Create instance
static VkResult create_instance(struct VulkanRenderer* renderer,
                                struct VulkanRendererConfig* config) {
    VkApplicationInfo appInfo = {0};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "Havel WM";
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName = "Havel Vulkan Renderer";
    appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion = VK_API_VERSION_1_2;
    
    VkInstanceCreateInfo createInfo = {0};
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &appInfo;
    
    // Extensions
    uint32_t extensionCount;
    const char** extensions = get_required_extensions(&extensionCount);
    createInfo.enabledExtensionCount = extensionCount;
    createInfo.ppEnabledExtensionNames = extensions;
    
    // Validation layers
    if (config->enableValidation) {
        if (!check_validation_layer_support()) {
            LOG_ERROR("[Vulkan] Validation layers requested but not available");
            return VK_ERROR_LAYER_NOT_PRESENT;
        }
        
        createInfo.enabledLayerCount = g_validationLayerCount;
        createInfo.ppEnabledLayerNames = g_validationLayers;
        
        // Debug messenger
        VkDebugUtilsMessengerCreateInfoEXT debugCreateInfo = {0};
        debugCreateInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
        debugCreateInfo.messageSeverity = 
            VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        debugCreateInfo.messageType = 
            VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        debugCreateInfo.pfnUserCallback = vulkan_debug_callback;
        
        createInfo.pNext = &debugCreateInfo;
    } else {
        createInfo.enabledLayerCount = 0;
        createInfo.pNext = NULL;
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
static VkResult setup_debug_messenger(struct VulkanRenderer* renderer) {
    if (!renderer->config.enableValidation) {
        return VK_SUCCESS;
    }
    
    VkDebugUtilsMessengerCreateInfoEXT createInfo = {0};
    createInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    createInfo.messageSeverity = 
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    createInfo.messageType = 
        VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    createInfo.pfnUserCallback = vulkan_debug_callback;
    
    // Get function pointer
    PFN_vkCreateDebugUtilsMessengerEXT func = 
        (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(
            renderer->instance, "vkCreateDebugUtilsMessengerEXT");
    
    if (func == NULL) {
        LOG_WARN("[Vulkan] Debug messenger not available");
        return VK_SUCCESS;
    }
    
    VkResult result = func(renderer->instance, &createInfo, NULL, &renderer->debugMessenger);
    if (result != VK_SUCCESS) {
        LOG_WARN("[Vulkan] Failed to create debug messenger: %d", result);
        return result;
    }
    
    LOG_INFO("[Vulkan] Debug messenger created");
    return VK_SUCCESS;
}

// Pick physical device
static VkResult pick_physical_device(struct VulkanRenderer* renderer) {
    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(renderer->instance, &deviceCount, NULL);
    
    if (deviceCount == 0) {
        LOG_ERROR("[Vulkan] Failed to find GPUs with Vulkan support");
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
        LOG_ERROR("[Vulkan] Failed to find a suitable GPU");
        return VK_ERROR_INCOMPATIBLE_DRIVER;
    }
    
    renderer->physicalDevice = bestDevice;
    
    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(bestDevice, &props);
    LOG_INFO("[Vulkan] Selected GPU: %s (score: %d)", props.deviceName, bestScore);
    
    return VK_SUCCESS;
}

// Create logical device
static VkResult create_logical_device(struct VulkanRenderer* renderer) {
    uint32_t graphicsFamily, presentFamily;
    find_queue_families(renderer->physicalDevice, &graphicsFamily, &presentFamily);
    
    if (graphicsFamily == UINT32_MAX || presentFamily == UINT32_MAX) {
        LOG_ERROR("[Vulkan] Failed to find required queue families");
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
    createInfo.enabledExtensionCount = g_deviceExtensionCount;
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
static VkResult create_swapchain(struct VulkanRenderer* renderer,
                                 uint32_t width, uint32_t height) {
    // Get surface capabilities (would need actual surface)
    VkSurfaceCapabilitiesKHR capabilities = {0};
    capabilities.currentExtent.width = width;
    capabilities.currentExtent.height = height;
    capabilities.minImageCount = 2;
    capabilities.maxImageCount = 8;
    
    VkExtent2D extent = capabilities.currentExtent;
    
    uint32_t imageCount = renderer->config.desiredImageCount;
    if (imageCount < capabilities.minImageCount) {
        imageCount = capabilities.minImageCount;
    }
    if (imageCount > capabilities.maxImageCount) {
        imageCount = capabilities.maxImageCount;
    }
    
    VkSwapchainCreateInfoKHR createInfo = {0};
    createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    createInfo.surface = VK_NULL_HANDLE;  // Would need actual surface
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
    createInfo.oldSwapchain = VK_NULL_HANDLE;
    
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
    
    LOG_INFO("[Vulkan] Swapchain created: %dx%d, %d images", 
             extent.width, extent.height, imageCount);
    
    return VK_SUCCESS;
}

// Create image views
static VkResult create_image_views(struct VulkanRenderer* renderer) {
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
static VkResult create_render_pass(struct VulkanRenderer* renderer) {
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
static VkResult create_graphics_pipeline(struct VulkanRenderer* renderer) {
    // This would create actual pipeline with shaders
    // For now, create a minimal pipeline layout
    
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
static VkResult create_framebuffers(struct VulkanRenderer* renderer) {
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
static VkResult create_command_pool(struct VulkanRenderer* renderer) {
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
static VkResult create_command_buffers(struct VulkanRenderer* renderer) {
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

// Create synchronization objects
static VkResult create_sync_objects(struct VulkanRenderer* renderer) {
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
            LOG_ERROR("[Vulkan] Failed to create synchronization objects");
            return VK_ERROR_INITIALIZATION_FAILED;
        }
    }
    
    LOG_INFO("[Vulkan] Synchronization objects created");
    return VK_SUCCESS;
}

// Public API implementation
bool vulkan_renderer_init(struct VulkanRenderer* renderer, 
                          struct VulkanRendererConfig* config) {
    if (!renderer || !config) {
        LOG_ERROR("[Vulkan] Invalid parameters");
        return false;
    }
    
    memset(renderer, 0, sizeof(struct VulkanRenderer));
    renderer->config = *config;
    
    LOG_INFO("[Vulkan] Initializing...");
    
    VkResult result;
    
    // Create instance
    result = create_instance(renderer, config);
    if (result != VK_SUCCESS) return false;
    
    // Setup debug messenger
    result = setup_debug_messenger(renderer);
    if (result != VK_SUCCESS) {
        vulkan_renderer_cleanup(renderer);
        return false;
    }
    
    // Pick physical device
    result = pick_physical_device(renderer);
    if (result != VK_SUCCESS) {
        vulkan_renderer_cleanup(renderer);
        return false;
    }
    
    // Create logical device
    result = create_logical_device(renderer);
    if (result != VK_SUCCESS) {
        vulkan_renderer_cleanup(renderer);
        return false;
    }
    
    // Create swapchain (with default size)
    result = create_swapchain(renderer, 1920, 1080);
    if (result != VK_SUCCESS) {
        vulkan_renderer_cleanup(renderer);
        return false;
    }
    
    // Create image views
    result = create_image_views(renderer);
    if (result != VK_SUCCESS) {
        vulkan_renderer_cleanup(renderer);
        return false;
    }
    
    // Create render pass
    result = create_render_pass(renderer);
    if (result != VK_SUCCESS) {
        vulkan_renderer_cleanup(renderer);
        return false;
    }
    
    // Create graphics pipeline
    result = create_graphics_pipeline(renderer);
    if (result != VK_SUCCESS) {
        vulkan_renderer_cleanup(renderer);
        return false;
    }
    
    // Create framebuffers
    result = create_framebuffers(renderer);
    if (result != VK_SUCCESS) {
        vulkan_renderer_cleanup(renderer);
        return false;
    }
    
    // Create command pool
    result = create_command_pool(renderer);
    if (result != VK_SUCCESS) {
        vulkan_renderer_cleanup(renderer);
        return false;
    }
    
    // Create command buffers
    result = create_command_buffers(renderer);
    if (result != VK_SUCCESS) {
        vulkan_renderer_cleanup(renderer);
        return false;
    }
    
    // Create sync objects
    result = create_sync_objects(renderer);
    if (result != VK_SUCCESS) {
        vulkan_renderer_cleanup(renderer);
        return false;
    }
    
    renderer->currentFrame = 0;
    renderer->framebufferResized = false;
    
    LOG_INFO("[Vulkan] Initialization complete");
    return true;
}

void vulkan_renderer_cleanup(struct VulkanRenderer* renderer) {
    if (!renderer) return;
    
    vulkan_renderer_wait_idle(renderer);
    
    // Free synchronization objects
    if (renderer->device != VK_NULL_HANDLE) {
        for (size_t i = 0; i < renderer->swapchainImageCount; i++) {
            if (renderer->imageAvailableSemaphores[i] != VK_NULL_HANDLE) {
                vkDestroySemaphore(renderer->device, renderer->imageAvailableSemaphores[i], NULL);
            }
            if (renderer->renderFinishedSemaphores[i] != VK_NULL_HANDLE) {
                vkDestroySemaphore(renderer->device, renderer->renderFinishedSemaphores[i], NULL);
            }
            if (renderer->inFlightFences[i] != VK_NULL_HANDLE) {
                vkDestroyFence(renderer->device, renderer->inFlightFences[i], NULL);
            }
        }
        
        free(renderer->imageAvailableSemaphores);
        free(renderer->renderFinishedSemaphores);
        free(renderer->inFlightFences);
        
        // Free command buffers and pool
        if (renderer->commandPool != VK_NULL_HANDLE) {
            vkDestroyCommandPool(renderer->device, renderer->commandPool, NULL);
        }
        free(renderer->commandBuffers);
        
        // Free framebuffers
        if (renderer->framebuffers) {
            for (uint32_t i = 0; i < renderer->swapchainImageCount; i++) {
                vkDestroyFramebuffer(renderer->device, renderer->framebuffers[i], NULL);
            }
            free(renderer->framebuffers);
        }
        
        // Free pipeline
        if (renderer->pipelineLayout != VK_NULL_HANDLE) {
            vkDestroyPipelineLayout(renderer->device, renderer->pipelineLayout, NULL);
        }
        
        // Free render pass
        if (renderer->renderPass != VK_NULL_HANDLE) {
            vkDestroyRenderPass(renderer->device, renderer->renderPass, NULL);
        }
        
        // Free image views
        if (renderer->swapchainImageViews) {
            for (uint32_t i = 0; i < renderer->swapchainImageCount; i++) {
                vkDestroyImageView(renderer->device, renderer->swapchainImageViews[i], NULL);
            }
            free(renderer->swapchainImageViews);
        }
        
        // Free swapchain
        if (renderer->swapchain != VK_NULL_HANDLE) {
            vkDestroySwapchainKHR(renderer->device, renderer->swapchain, NULL);
        }
        free(renderer->swapchainImages);
        
        vkDestroyDevice(renderer->device, NULL);
    }
    
    // Free debug messenger
    if (renderer->debugMessenger != VK_NULL_HANDLE) {
        PFN_vkDestroyDebugUtilsMessengerEXT func = 
            (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(
                renderer->instance, "vkDestroyDebugUtilsMessengerEXT");
        if (func) {
            func(renderer->instance, renderer->debugMessenger, NULL);
        }
    }
    
    // Free instance
    if (renderer->instance != VK_NULL_HANDLE) {
        vkDestroyInstance(renderer->instance, NULL);
    }
    
    memset(renderer, 0, sizeof(struct VulkanRenderer));
    LOG_INFO("[Vulkan] Cleanup complete");
}

bool vulkan_renderer_begin_frame(struct VulkanRenderer* renderer) {
    if (!renderer || renderer->device == VK_NULL_HANDLE) return false;
    
    // Wait for previous frame
    vkWaitForFences(renderer->device, 1, &renderer->inFlightFences[renderer->currentFrame], 
                   VK_TRUE, UINT64_MAX);
    
    // Acquire next image
    uint32_t imageIndex;
    VkResult result = vkAcquireNextImageKHR(renderer->device, renderer->swapchain, 
                                           UINT64_MAX,
                                           renderer->imageAvailableSemaphores[renderer->currentFrame],
                                           VK_NULL_HANDLE,
                                           &imageIndex);
    
    if (result == VK_ERROR_OUT_OF_DATE_KHR) {
        vulkan_renderer_resize_swapchain(renderer, 1920, 1080);
        return false;
    } else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
        LOG_ERROR("[Vulkan] Failed to acquire swapchain image: %d", result);
        return false;
    }
    
    // Reset command buffer
    vkResetCommandBuffer(renderer->commandBuffers[imageIndex], 0);
    
    // Begin command buffer recording
    VkCommandBufferBeginInfo beginInfo = {0};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    
    if (vkBeginCommandBuffer(renderer->commandBuffers[imageIndex], &beginInfo) != VK_SUCCESS) {
        LOG_ERROR("[Vulkan] Failed to begin command buffer");
        return false;
    }
    
    return true;
}

bool vulkan_renderer_end_frame(struct VulkanRenderer* renderer) {
    if (!renderer || renderer->device == VK_NULL_HANDLE) return false;
    
    uint32_t imageIndex = renderer->currentFrame;
    
    // End command buffer
    if (vkEndCommandBuffer(renderer->commandBuffers[imageIndex]) != VK_SUCCESS) {
        LOG_ERROR("[Vulkan] Failed to end command buffer");
        return false;
    }
    
    // Submit command buffer
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
    
    // Present
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
        vulkan_renderer_resize_swapchain(renderer, 1920, 1080);
    } else if (result != VK_SUCCESS) {
        LOG_ERROR("[Vulkan] Failed to present: %d", result);
        return false;
    }
    
    renderer->currentFrame = (renderer->currentFrame + 1) % renderer->swapchainImageCount;
    
    return true;
}

void vulkan_renderer_wait_idle(struct VulkanRenderer* renderer) {
    if (!renderer || renderer->device == VK_NULL_HANDLE) return;
    
    vkDeviceWaitIdle(renderer->device);
}

void vulkan_renderer_resize_swapchain(struct VulkanRenderer* renderer,
                                      uint32_t width, uint32_t height) {
    if (!renderer) return;
    
    vulkan_renderer_wait_idle(renderer);
    
    // Cleanup old swapchain resources
    for (uint32_t i = 0; i < renderer->swapchainImageCount; i++) {
        vkDestroyImageView(renderer->device, renderer->swapchainImageViews[i], NULL);
        vkDestroyFramebuffer(renderer->device, renderer->framebuffers[i], NULL);
    }
    free(renderer->swapchainImageViews);
    free(renderer->framebuffers);
    free(renderer->swapchainImages);
    
    // Recreate swapchain
    create_swapchain(renderer, width, height);
    create_image_views(renderer);
    create_framebuffers(renderer);
    
    LOG_INFO("[Vulkan] Swapchain resized to %dx%d", width, height);
}

void vulkan_renderer_set_clear_color(struct VulkanRenderer* renderer,
                                     float r, float g, float b, float a) {
    // Would set clear color in render pass
    (void)renderer;
    (void)r;
    (void)g;
    (void)b;
    (void)a;
}

void vulkan_renderer_draw_quad(struct VulkanRenderer* renderer,
                               float x, float y, float w, float h,
                               VkDescriptorSet descriptorSet) {
    // Would record draw commands
    (void)renderer;
    (void)x;
    (void)y;
    (void)w;
    (void)h;
    (void)descriptorSet;
}

VkResult vulkan_renderer_create_texture(struct VulkanRenderer* renderer,
                                        struct wlr_buffer* buffer,
                                        VkImage* image,
                                        VkImageView* view,
                                        VkSampler* sampler) {
    // Would create texture from wlroots buffer
    (void)renderer;
    (void)buffer;
    (void)image;
    (void)view;
    (void)sampler;
    return VK_SUCCESS;
}

void vulkan_renderer_destroy_texture(struct VulkanRenderer* renderer,
                                     VkImage image,
                                     VkImageView view,
                                     VkSampler sampler) {
    // Would destroy texture
    (void)renderer;
    (void)image;
    (void)view;
    (void)sampler;
}

VkInstance vulkan_renderer_get_instance(struct VulkanRenderer* renderer) {
    return renderer ? renderer->instance : VK_NULL_HANDLE;
}

VkDevice vulkan_renderer_get_device(struct VulkanRenderer* renderer) {
    return renderer ? renderer->device : VK_NULL_HANDLE;
}

VkPhysicalDevice vulkan_renderer_get_physical_device(struct VulkanRenderer* renderer) {
    return renderer ? renderer->physicalDevice : VK_NULL_HANDLE;
}

VkQueue vulkan_renderer_get_graphics_queue(struct VulkanRenderer* renderer) {
    return renderer ? renderer->graphicsQueue : VK_NULL_HANDLE;
}

bool vulkan_renderer_is_available(void) {
    uint32_t extensionCount;
    vkEnumerateInstanceExtensionProperties(NULL, &extensionCount, NULL);
    return extensionCount > 0;
}

const char* vulkan_renderer_get_info(struct VulkanRenderer* renderer) {
    static char info[512];
    if (!renderer || renderer->physicalDevice == VK_NULL_HANDLE) {
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
