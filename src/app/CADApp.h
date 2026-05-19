#pragma once
#include <memory>
#include <vector>
#include <android/native_window.h>
#include "rendering/VulkanContext.h"
#include "rendering/Swapchain.h"
#include "rendering/Pipeline.h"

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
    void drawFrame();
    void cleanup();
    void onWindowResized(ANativeWindow* window, int32_t width, int32_t height);

    bool isInitialized() const { return m_initialized; }

private:
    bool m_initialized{false};
    bool m_pendingResize{false};

    std::unique_ptr<rendering::VulkanContext> m_vkContext;
    std::unique_ptr<rendering::Swapchain> m_swapchain;

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
