#include "rendering/VulkanContext.h"
#include <android/native_window.h>

// Manually define Android surface types since vulkan.hpp uses VULKAN_H_ guard
// and can't be combined with vulkan_android.h after it.
#ifndef VK_KHR_ANDROID_SURFACE_EXTENSION_NAME
#define VK_KHR_ANDROID_SURFACE_EXTENSION_NAME "VK_KHR_android_surface"
#endif

#ifndef VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR
#define VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR static_cast<VkStructureType>(1000007000)
#endif

struct AndroidSurfaceCreateInfo {
    VkStructureType         sType;
    const void*             pNext;
    uint32_t                flags;
    ANativeWindow*          window;
};

#include <stdexcept>
#include <android/log.h>

#define LOG_TAG "MobileCAD"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

namespace rendering {

namespace {
#ifdef NDEBUG
constexpr bool ENABLE_VALIDATION_LAYERS = false;
#else
constexpr bool ENABLE_VALIDATION_LAYERS = true;
#endif
} // namespace

VulkanContext::VulkanContext(ANativeWindow* window) {
    m_width = static_cast<uint32_t>(ANativeWindow_getWidth(window));
    m_height = static_cast<uint32_t>(ANativeWindow_getHeight(window));
    createInstance();
    createSurface(window);
    pickPhysicalDevice();
    createLogicalDevice();
    createCommandPool();
    createSyncObjects();
    if constexpr (ENABLE_VALIDATION_LAYERS) {
        /* Debug messenger needs Vulkan debug utils extension, not on all Android */
        setupDebugMessenger();
    }
}

VulkanContext::~VulkanContext() {
    if (m_device != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(m_device);
    }

    if (m_device != VK_NULL_HANDLE) {
        for (uint32_t i = 0; i < kMaxFrames; i++) {
            if (m_fences[i] != VK_NULL_HANDLE)
                vkDestroyFence(m_device, m_fences[i], nullptr);
            if (m_waitSemaphores[i] != VK_NULL_HANDLE)
                vkDestroySemaphore(m_device, m_waitSemaphores[i], nullptr);
            if (m_signalSemaphores[i] != VK_NULL_HANDLE)
                vkDestroySemaphore(m_device, m_signalSemaphores[i], nullptr);
        }

        if (m_commandPool != VK_NULL_HANDLE)
            vkDestroyCommandPool(m_device, m_commandPool, nullptr);

        vkDestroyDevice(m_device, nullptr);
    }

    if (m_debugMessenger != VK_NULL_HANDLE) {
        auto func = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
            vkGetInstanceProcAddr(m_instance, "vkDestroyDebugUtilsMessengerEXT"));
        if (func) {
            func(m_instance, m_debugMessenger, nullptr);
        }
    }

    if (m_instance != VK_NULL_HANDLE) {
        vkDestroyInstance(m_instance, nullptr);
    }
}

void VulkanContext::createInstance() {
    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "MobileCAD";
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName = "No Engine";
    appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion = VK_API_VERSION_1_2;

    VkInstanceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &appInfo;

    std::vector<const char*> extensions = {
        VK_KHR_SURFACE_EXTENSION_NAME,
        VK_KHR_ANDROID_SURFACE_EXTENSION_NAME,
    };

#if defined(VK_EXT_DEBUG_UTILS_EXTENSION_NAME) && !defined(NDEBUG)
    static std::vector<const char*> extraLayers;
    static bool layersChecked = false;
    if (!layersChecked) {
        uint32_t count = 0;
        vkEnumerateInstanceLayerProperties(&count, nullptr);
        std::vector<VkLayerProperties> layers(count);
        vkEnumerateInstanceLayerProperties(&count, layers.data());
        for (const auto& layer : layers) {
            if (strcmp(layer.layerName, "VK_LAYER_KHRONOS_validation") == 0) {
                extraLayers.push_back("VK_LAYER_KHRONOS_validation");
                extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
                break;
            }
        }
        layersChecked = true;
    }
    if (!extraLayers.empty()) {
        createInfo.enabledLayerCount = 1;
        createInfo.ppEnabledLayerNames = extraLayers.data();
    }
#endif

    createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    createInfo.ppEnabledExtensionNames = extensions.data();

    VkResult result = vkCreateInstance(&createInfo, nullptr, &m_instance);
    if (result != VK_SUCCESS) {
        LOGE("Failed to create Vulkan instance: %d", result);
        throw std::runtime_error("Failed to create Vulkan instance");
    }
}

void VulkanContext::createSurface(ANativeWindow* window) {
    AndroidSurfaceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR;
    createInfo.window = window;

    // Load vkCreateAndroidSurfaceKHR at runtime since vulkan.hpp doesn't expose it
    using PFN_CreateAndroidSurface = VkResult (*)(VkInstance, const AndroidSurfaceCreateInfo*, const VkAllocationCallbacks*, VkSurfaceKHR*);
    auto func = reinterpret_cast<PFN_CreateAndroidSurface>(
        vkGetInstanceProcAddr(m_instance, "vkCreateAndroidSurfaceKHR"));
    if (!func) {
        throw std::runtime_error("Failed to get vkCreateAndroidSurfaceKHR");
    }

    VkResult result = func(m_instance, &createInfo, nullptr, &m_surface);
    if (result != VK_SUCCESS) {
        LOGE("Failed to create Android surface: %d", result);
        throw std::runtime_error("Failed to create Android surface");
    }
}

void VulkanContext::pickPhysicalDevice() {
    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(m_instance, &deviceCount, nullptr);
    if (deviceCount == 0) {
        throw std::runtime_error("Failed to find GPUs with Vulkan support");
    }

    std::vector<VkPhysicalDevice> devices(deviceCount);
    vkEnumeratePhysicalDevices(m_instance, &deviceCount, devices.data());

    // Pick discrete GPU if available (desktop), otherwise integrated (most mobile)
    for (const auto& device : devices) {
        VkPhysicalDeviceProperties props;
        vkGetPhysicalDeviceProperties(device, &props);

        uint32_t queueCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(device, &queueCount, nullptr);
        std::vector<VkQueueFamilyProperties> families(queueCount);
        vkGetPhysicalDeviceQueueFamilyProperties(device, &queueCount, families.data());

        bool hasGraphics = false;
        bool hasPresent = false;
        for (uint32_t i = 0; i < queueCount; i++) {
            VkBool32 presentSupport = false;
            vkGetPhysicalDeviceSurfaceSupportKHR(device, i, m_surface, &presentSupport);
            if (families[i].queueCount > 0 && (families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)) {
                hasGraphics = true;
            }
            if (families[i].queueCount > 0 && presentSupport) {
                hasPresent = true;
            }
        }
        if (hasGraphics && hasPresent) {
            m_physicalDevice = device;
            break;
        }
    }

    if (m_physicalDevice == VK_NULL_HANDLE) {
        throw std::runtime_error("Failed to find suitable GPU");
    }

    // Find combined graphics+present queue family
    uint32_t queueCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(m_physicalDevice, &queueCount, nullptr);
    std::vector<VkQueueFamilyProperties> families(queueCount);
    vkGetPhysicalDeviceQueueFamilyProperties(m_physicalDevice, &queueCount, families.data());

    for (uint32_t i = 0; i < queueCount; i++) {
        VkBool32 presentSupport = false;
        vkGetPhysicalDeviceSurfaceSupportKHR(m_physicalDevice, i, m_surface, &presentSupport);
        if (families[i].queueCount > 0 &&
            (families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) &&
            presentSupport) {
            m_graphicsFamily = i;
            break;
        }
    }

    if (m_graphicsFamily == UINT32_MAX) {
        throw std::runtime_error("Failed to find combined graphics+present queue family");
    }
}

void VulkanContext::createLogicalDevice() {
    float queuePriority = 1.0f;
    VkDeviceQueueCreateInfo queueInfo{};
    queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queueInfo.queueFamilyIndex = m_graphicsFamily;
    queueInfo.queueCount = 1;
    queueInfo.pQueuePriorities = &queuePriority;

    VkPhysicalDeviceFeatures deviceFeatures{};

    VkDeviceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    createInfo.queueCreateInfoCount = 1;
    createInfo.pQueueCreateInfos = &queueInfo;
    createInfo.pEnabledFeatures = &deviceFeatures;

        const char* devExtensions[] = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME
    };
    createInfo.enabledExtensionCount = 1;
    createInfo.ppEnabledExtensionNames = devExtensions;
    createInfo.enabledLayerCount = 0;

    VkResult result = vkCreateDevice(m_physicalDevice, &createInfo, nullptr, &m_device);
    if (result != VK_SUCCESS) {
        throw std::runtime_error("Failed to create logical device");
    }
}

void VulkanContext::createCommandPool() {
    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.queueFamilyIndex = m_graphicsFamily;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

    VkResult result = vkCreateCommandPool(m_device, &poolInfo, nullptr, &m_commandPool);
    if (result != VK_SUCCESS) {
        throw std::runtime_error("Failed to create command pool");
    }
}

void VulkanContext::createSyncObjects() {
    VkSemaphoreCreateInfo semInfo{};
    semInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = 0;

    for (uint32_t i = 0; i < kMaxFrames; i++) {
        if (vkCreateSemaphore(m_device, &semInfo, nullptr, &m_waitSemaphores[i]) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create wait semaphore");
        }
        if (vkCreateSemaphore(m_device, &semInfo, nullptr, &m_signalSemaphores[i]) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create signal semaphore");
        }
        fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        if (vkCreateFence(m_device, &fenceInfo, nullptr, &m_fences[i]) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create fence");
        }
    }

    // Allocate command buffers
    m_cmdBuffers.resize(kMaxFrames);
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = m_commandPool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = kMaxFrames;

    if (vkAllocateCommandBuffers(m_device, &allocInfo, m_cmdBuffers.data()) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate command buffers");
    }
}

VkCommandBuffer VulkanContext::getCmdBuffer(uint32_t index) const {
    return (index < m_cmdBuffers.size()) ? m_cmdBuffers[index] : VK_NULL_HANDLE;
}

VkSemaphore VulkanContext::getWaitSemaphore(uint32_t frame) const {
    return m_waitSemaphores[frame % kMaxFrames];
}

VkSemaphore VulkanContext::getSignalSemaphore(uint32_t frame) const {
    return m_signalSemaphores[frame % kMaxFrames];
}

VkFence VulkanContext::getFence(uint32_t frame) const {
    return m_fences[frame];
}

void VulkanContext::waitIdle() const {
    if (m_device != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(m_device);
    }
}

void VulkanContext::setupDebugMessenger() {
#if !defined(NDEBUG) && defined(VK_EXT_DEBUG_UTILS_EXTENSION_NAME)
    auto func = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
        vkGetInstanceProcAddr(m_instance, "vkCreateDebugUtilsMessengerEXT"));
    if (!func) {
        LOGE("Warning: vkCreateDebugUtilsMessengerEXT not available");
        return;
    }

    VkDebugUtilsMessengerCreateInfoEXT createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    createInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
                                 VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                                 VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    createInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                             VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                             VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    createInfo.pfnUserCallback = debugCallback;
    createInfo.pUserData = nullptr;

    if (func(m_instance, &createInfo, nullptr, &m_debugMessenger) != VK_SUCCESS) {
        throw std::runtime_error("Failed to set up debug messenger");
    }
#else
    (void)0;
#endif
}

VKAPI_ATTR VkBool32 VKAPI_CALL VulkanContext::debugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
    VkDebugUtilsMessageTypeFlagsEXT messageType,
    const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
    void* pUserData) {
    (void)messageSeverity;
    (void)messageType;
    (void)pUserData;
    LOGE("Validation Layer: %s", pCallbackData->pMessage);
    return VK_FALSE;
}

} // namespace rendering
