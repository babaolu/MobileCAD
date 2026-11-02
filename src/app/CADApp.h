#pragma once
#include <memory>
#include <vector>
#include <android/native_window.h>
#include "rendering/VulkanContext.h"
#include "rendering/Swapchain.h"
#include "rendering/Pipeline.h"
#include "rendering/Camera.h"

namespace app {

struct Vertex {
    float position[3];
    float color[3];

    static std::vector<VkVertexInputBindingDescription> getBindingDescriptions();
    static std::vector<VkVertexInputAttributeDescription> getAttributeDescriptions();
};

class CADApp {
public:
    CADApp(ANativeWindow* window);
    ~CADApp();

    // Non-copyable, non-movable
    CADApp(const CADApp&) = delete;
    CADApp& operator=(const CADApp&) = delete;

    // NDK lifecycle integration
    void init(ANativeWindow* window);
    bool drawFrame();   // returns false on device-loss → caller should exit
    void cleanup();
    void onWindowResized(ANativeWindow* window, int32_t width, int32_t height);
    void onTouchEvent(int32_t action, int32_t pointerCount, float x0, float y0, float x1, float y1);

    rendering::Camera& getCamera() { return m_camera; }
    const rendering::Camera& getCamera() const { return m_camera; }

    bool isInitialized() const { return m_initialized; }
    bool isDeviceLost() const { return m_deviceLost; }

    // Diagnostic / main.cpp accessors (const, no side effects)
    rendering::VulkanContext* getVkContextPtr() const { return m_vkContext.get(); }
    rendering::Swapchain*     getSwapchainPtr()  const { return m_swapchain.get(); }
    uint32_t getSwapchainExtentW() const { return m_swapchain ? m_swapchain->getExtent().width : 0; }
    uint32_t getSwapchainExtentH() const { return m_swapchain ? m_swapchain->getExtent().height : 0; }

  private:
    bool m_initialized{false};
    bool m_deviceLost{false};

    std::unique_ptr<rendering::VulkanContext> m_vkContext;
    std::unique_ptr<rendering::Swapchain> m_swapchain;
    rendering::Camera m_camera;

    // Touch gesture tracking
    float m_lastTouchX{0.0f};
    float m_lastTouchY{0.0f};
    float m_lastPinchDist{0.0f};
    float m_lastMidX{0.0f};
    float m_lastMidY{0.0f};
    int32_t m_activePointers{0};

    // Resources
    VkPipelineLayout m_pipelineLayout{VK_NULL_HANDLE};
    VkRenderPass m_renderPass{VK_NULL_HANDLE};
    VkPipeline m_pipeline{VK_NULL_HANDLE};
    std::vector<VkFramebuffer> m_swapchainFramebuffers;

    // Sync per-frame
    uint32_t m_currentFrame{0};
    uint32_t m_drawFrameCount{0};

    // Initialization
    void initSwapchain();
    void initRenderPass();
    void initPipelineLayout();
    void initGraphicsPipeline();
    void initFramebuffers();

    // Cleanup
    void cleanupSwapchain();

    // Rendering
    void recordCommandBuffer(VkCommandBuffer commandBuffer, uint32_t imageIndex);
};

} // namespace app
