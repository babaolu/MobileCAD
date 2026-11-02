#pragma once
#include <vulkan/vulkan.hpp>
#include <vector>
#include <string>
#include <android/native_window.h>

namespace rendering {

class VulkanContext {
public:
    VulkanContext(ANativeWindow* window);
    ~VulkanContext();

    // Non-copyable, non-movable
    VulkanContext(const VulkanContext&) = delete;
    VulkanContext& operator=(const VulkanContext&) = delete;

    // Getters
    VkInstance getInstance() const { return m_instance; }
    VkDevice getDevice() const { return m_device; }
    VkPhysicalDevice getPhysicalDevice() const { return m_physicalDevice; }
    VkSurfaceKHR getSurface() const { return m_surface; }
    VkCommandPool getCommandPool() const { return m_commandPool; }
    VkQueue getGraphicsQueue() const { return m_graphicsQueue; }
    VkQueue getPresentQueue() const { return m_presentQueue; }
    uint32_t getWidth() const { return m_width; }
    uint32_t getHeight() const { return m_height; }
    uint32_t getGraphicsQueueFamily() const { return m_graphicsFamily; }
    void setDimensions(uint32_t w, uint32_t h) { m_width = w; m_height = h; }


    // Sync objects (ring buffer for multiple frames in flight)
    VkCommandBuffer getCmdBuffer(uint32_t index) const;
    VkSemaphore getWaitSemaphore(uint32_t frame) const;
    VkSemaphore getSignalSemaphore(uint32_t frame) const;
    VkFence getFence(uint32_t frame) const;
    uint32_t getMaxFrames() const { return kMaxFrames; }

    void waitIdle() const;

private:
    VkInstance m_instance{VK_NULL_HANDLE};
    VkPhysicalDevice m_physicalDevice{VK_NULL_HANDLE};
    VkDevice m_device{VK_NULL_HANDLE};
    VkSurfaceKHR m_surface{VK_NULL_HANDLE};
    VkCommandPool m_commandPool{VK_NULL_HANDLE};
    VkQueue m_graphicsQueue{VK_NULL_HANDLE};
    VkQueue m_presentQueue{VK_NULL_HANDLE};

    VkDebugUtilsMessengerEXT m_debugMessenger{VK_NULL_HANDLE};

    uint32_t m_width{0};
    uint32_t m_height{0};
    uint32_t m_graphicsFamily{UINT32_MAX};

    // Ring buffer for frame sync. NDK NativeActivity has no vsync-aware GLFW-style swapchain.
    // Larger depth increases submission pipelining; some Adreno drivers (vulkan.adreno.so)
    // are known to crash on vkQueueSubmit when the submission queue exceeds 4 deep.
    // Use 2 to stay safely under that threshold.
    static constexpr uint32_t kMaxFrames = 2;
    std::vector<VkCommandBuffer> m_cmdBuffers;
    VkSemaphore m_waitSemaphores[kMaxFrames];
    VkSemaphore m_signalSemaphores[kMaxFrames];
    VkFence m_fences[kMaxFrames];

    void createInstance();
    void createSurface(ANativeWindow* window);
    void pickPhysicalDevice();
    void createLogicalDevice();
    void createCommandPool();
    void createSyncObjects();
    void setupDebugMessenger();
    static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
        VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
        VkDebugUtilsMessageTypeFlagsEXT messageType,
        const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
        void* pUserData);
};

} // namespace rendering
