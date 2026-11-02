#pragma once
#include <vulkan/vulkan.hpp>
#include <memory>
#include "VulkanContext.h"

namespace rendering {

class Swapchain {
public:
    Swapchain(VulkanContext* context, uint32_t width, uint32_t height);
    ~Swapchain();

    Swapchain(const Swapchain&) = delete;
    Swapchain& operator=(const Swapchain&) = delete;

    VkSwapchainKHR getSwapchain() const { return m_swapchain; }
    const std::vector<VkImage>& getImages() const { return m_images; }
    VkImageView getImageView(size_t index) const { return m_imageViews[index]; }
    VkFormat getImageFormat() const { return m_imageFormat; }
    VkExtent2D getExtent() const { return m_extent; }
    VkImageView getDepthImageView() const { return m_depthImageView; }
    VkFormat getDepthFormat() const { return VK_FORMAT_D32_SFLOAT; }

private:
    VulkanContext* m_context{nullptr};
    VkSwapchainKHR m_swapchain{VK_NULL_HANDLE};
    VkFormat m_imageFormat{};
    VkExtent2D m_extent{};
    std::vector<VkImage> m_images;
    std::vector<VkImageView> m_imageViews;

    VkImage m_depthImage{VK_NULL_HANDLE};
    VkDeviceMemory m_depthImageMemory{VK_NULL_HANDLE};
    VkImageView m_depthImageView{VK_NULL_HANDLE};
};

} // namespace rendering
