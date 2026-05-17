#include "rendering/Swapchain.h"
#include <stdexcept>
#include <iostream>

namespace rendering {

Swapchain::Swapchain(VulkanContext* context, uint32_t width, uint32_t height)
    : m_context(context) {
    auto surface = m_context->getSurface();
    auto physicalDevice = m_context->getPhysicalDevice();
    auto device = m_context->getDevice();

    // Query surface capabilities
    VkSurfaceCapabilitiesKHR capabilities;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice, surface, &capabilities);

    // Query surface formats
    uint32_t formatCount;
    vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &formatCount, nullptr);
    std::vector<VkSurfaceFormatKHR> formats(formatCount);
    vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &formatCount, formats.data());

    // Choose format
    VkSurfaceFormatKHR chosenFormat;
    if (formats.size() == 1 && formats[0].format == VK_FORMAT_UNDEFINED) {
        chosenFormat.format = VK_FORMAT_B8G8R8A8_SRGB;
        chosenFormat.colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    } else {
        chosenFormat = formats[0];
        for (const auto& format : formats) {
            if (format.format == VK_FORMAT_B8G8R8A8_SRGB && format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
                chosenFormat = format;
                break;
            }
        }
    }
    m_imageFormat = chosenFormat.format;

    // Choose present mode
    uint32_t presentModeCount;
    vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, surface, &presentModeCount, nullptr);
    std::vector<VkPresentModeKHR> presentModes(presentModeCount);
    vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, surface, &presentModeCount, presentModes.data());

    VkPresentModeKHR chosenPresentMode = VK_PRESENT_MODE_FIFO_KHR;
    for (const auto& mode : presentModes) {
        if (mode == VK_PRESENT_MODE_MAILBOX_KHR) {
            chosenPresentMode = mode;
            break;
        }
    }

    // Choose extent
    VkExtent2D actualExtent = {
        std::max(capabilities.currentExtent.width, std::min(width, capabilities.maxImageExtent.width)),
        std::max(capabilities.currentExtent.height, std::min(height, capabilities.maxImageExtent.height))
    };
    if (capabilities.currentExtent.width != UINT32_MAX) {
        actualExtent = capabilities.currentExtent;
    }
    m_extent = actualExtent;

    // Choose image count
    uint32_t imageCount = capabilities.minImageCount + 1;
    if (capabilities.maxImageCount > 0 && imageCount > capabilities.maxImageCount) {
        imageCount = capabilities.maxImageCount;
    }

    // Create swapchain
    VkSwapchainCreateInfoKHR createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    createInfo.surface = surface;
    createInfo.minImageCount = imageCount;
    createInfo.imageFormat = chosenFormat.format;
    createInfo.imageColorSpace = chosenFormat.colorSpace;
    createInfo.imageExtent = actualExtent;
    createInfo.imageArrayLayers = 1;
    createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    createInfo.preTransform = capabilities.currentTransform;
    createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR;
    createInfo.presentMode = chosenPresentMode;
    createInfo.clipped = VK_TRUE;
    createInfo.oldSwapchain = VK_NULL_HANDLE;

    if (vkCreateSwapchainKHR(device, &createInfo, nullptr, &m_swapchain) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create swapchain");
    }

    // Get swapchain images
    vkGetSwapchainImagesKHR(device, m_swapchain, &imageCount, nullptr);
    m_images.resize(imageCount);
    vkGetSwapchainImagesKHR(device, m_swapchain, &imageCount, m_images.data());

    // Create image views
    m_imageViews.resize(m_images.size());
    for (size_t i = 0; i < m_images.size(); ++i) {
        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = m_images[i];
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = m_imageFormat;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.baseMipLevel = 0;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount = 1;

        if (vkCreateImageView(device, &viewInfo, nullptr, &m_imageViews[i]) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create image view");
        }
    }
}

Swapchain::~Swapchain() {
    auto device = m_context->getDevice();

    for (auto imageView : m_imageViews) {
        vkDestroyImageView(device, imageView, nullptr);
    }

    if (m_swapchain != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(device, m_swapchain, nullptr);
    }
}

} // namespace rendering
