#include "app/CADApp.h"
#include <stdexcept>
#include <android/log.h>

#define LOG_TAG "MobileCAD"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

namespace app {

static const uint32_t kMaxVertexCount = 3;
static Vertex g_vertices[kMaxVertexCount];
static unsigned short g_indices[3];

CADApp::CADApp(ANativeWindow* window) {
    // Store initial window state
    g_vertices[0] = {{-0.5f, -0.5f, 0.0f}, {1.0f, 0.0f, 0.0f}};
    g_vertices[1] = {{ 0.5f, -0.5f, 0.0f}, {0.0f, 1.0f, 0.0f}};
    g_vertices[2] = {{ 0.0f,  0.5f, 0.0f}, {0.0f, 0.0f, 1.0f}};
    g_indices[0] = 0; g_indices[1] = 1; g_indices[2] = 2;

    init(window);
}

CADApp::~CADApp() {
    cleanup();
}

void CADApp::init(ANativeWindow* window) {
    LOGI("CADApp::init: starting");
    try {
        LOGI("Creating VulkanContext");
        m_vkContext = std::make_unique<rendering::VulkanContext>(window);
        LOGI("VulkanContext created");

        LOGI("Initializing swapchain");
        initSwapchain();
        LOGI("Swapchain initialized");

        LOGI("Initializing render pass");
        initRenderPass();
        LOGI("Render pass initialized");

        LOGI("Initializing pipeline layout");
        initPipelineLayout();
        LOGI("Pipeline layout initialized");

        LOGI("Initializing graphics pipeline (shader loading & pipeline creation)");
        initGraphicsPipeline();
        LOGI("Graphics pipeline initialized");

        LOGI("Initializing framebuffers");
        initFramebuffers();
        LOGI("Framebuffers initialized");

        m_initialized = true;
        LOGI("CADApp::init: done");
    } catch (const std::exception& e) {
        LOGE("CADApp::init failed: %s", e.what());
        throw;
    }
}

void CADApp::initSwapchain() {
    auto width = m_vkContext->getWidth();
    auto height = m_vkContext->getHeight();
    m_swapchain = std::make_unique<rendering::Swapchain>(
        m_vkContext.get(),
        static_cast<uint32_t>(width),
        static_cast<uint32_t>(height)
    );
}

void CADApp::initRenderPass() {
    auto format = m_swapchain->getImageFormat();

    VkAttachmentDescription colorAttachment{};
    colorAttachment.format = format;
    colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentReference colorAttachmentRef{};
    colorAttachmentRef.attachment = 0;
    colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorAttachmentRef;

    VkSubpassDependency dependency{};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.srcAccessMask = 0;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = 1;
    renderPassInfo.pAttachments = &colorAttachment;
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    renderPassInfo.dependencyCount = 1;
    renderPassInfo.pDependencies = &dependency;

    auto device = m_vkContext->getDevice();
    if (vkCreateRenderPass(device, &renderPassInfo, nullptr, &m_renderPass) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create render pass");
    }
}

void CADApp::initPipelineLayout() {
    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 0;
    pipelineLayoutInfo.pSetLayouts = nullptr;
    pipelineLayoutInfo.pushConstantRangeCount = 0;

    auto device = m_vkContext->getDevice();
    if (vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &m_pipelineLayout) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create pipeline layout");
    }
}

void CADApp::initGraphicsPipeline() {
    auto device = m_vkContext->getDevice();

    // Load shaders from assets/shaders/
    // APK assets are accessible via AAssetManager, but for NativeActivity
    // the simplest path is to use relative paths from the working directory
    auto vertShaderCode = rendering::Pipeline::loadShader("shaders/simple.vert.spv");
    auto fragShaderCode = rendering::Pipeline::loadShader("shaders/simple.frag.spv");

    VkShaderModule vertModule = rendering::Pipeline::createShaderModule(device, vertShaderCode);
    VkShaderModule fragModule = rendering::Pipeline::createShaderModule(device, fragShaderCode);

    VkPipelineShaderStageCreateInfo vertStageInfo{};
    vertStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertStageInfo.module = vertModule;
    vertStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo fragStageInfo{};
    fragStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    fragStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragStageInfo.module = fragModule;
    fragStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo stages[] = {vertStageInfo, fragStageInfo};

    // Vertex input
    auto bindingDescriptions = Vertex::getBindingDescriptions();
    auto attributeDescriptions = Vertex::getAttributeDescriptions();

    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount = static_cast<uint32_t>(bindingDescriptions.size());
    vertexInputInfo.pVertexBindingDescriptions = bindingDescriptions.data();
    vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescriptions.size());
    vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions.data();

    // Input assembly
    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    // Viewport
    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(m_swapchain->getExtent().width);
    viewport.height = static_cast<float>(m_swapchain->getExtent().height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = m_swapchain->getExtent();

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.pViewports = &viewport;
    viewportState.scissorCount = 1;
    viewportState.pScissors = &scissor;

    // Rasterizer
    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_FALSE;

    // Multisampling
    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    // Color blending
    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                          VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachment.blendEnable = VK_FALSE;

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;

    // Pipeline
    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = stages;
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.layout = m_pipelineLayout;
    pipelineInfo.renderPass = m_renderPass;
    pipelineInfo.subpass = 0;
    pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;

    if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_pipeline) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create graphics pipeline");
    }

    vkDestroyShaderModule(device, fragModule, nullptr);
    vkDestroyShaderModule(device, vertModule, nullptr);
}

void CADApp::initFramebuffers() {
    auto swapchainImages = m_swapchain->getImages();
    m_swapchainFramebuffers.resize(swapchainImages.size());

    for (size_t i = 0; i < swapchainImages.size(); ++i) {
        VkImageView attachments[] = { m_swapchain->getImageView(i) };

        VkFramebufferCreateInfo framebufferInfo{};
        framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebufferInfo.renderPass = m_renderPass;
        framebufferInfo.attachmentCount = 1;
        framebufferInfo.pAttachments = attachments;
        framebufferInfo.width = m_swapchain->getExtent().width;
        framebufferInfo.height = m_swapchain->getExtent().height;
        framebufferInfo.layers = 1;

    // Guard against drawFrame() running while onWindowResized() is mid-rebuild
    if (m_pendingResize)
    {
        LOGI("drawFrame: skipping — resize in progress");
        return;
    }
    if (!m_vkContext || !m_swapchain || !m_pipeline)
    {
        LOGE("drawFrame: null resource — vkContext=%p swapchain=%p pipeline=%p",
             (void*)m_vkContext.get(), (void*)m_swapchain.get(), (void*)m_pipeline);
        return;
    }
    auto device = m_vkContext->getDevice();
        if (vkCreateFramebuffer(device, &framebufferInfo, nullptr, &m_swapchainFramebuffers[i]) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create framebuffer");
        }
    }
}

void CADApp::cleanupSwapchain() {
    auto device = m_vkContext->getDevice();

    for (auto framebuffer : m_swapchainFramebuffers) {
        vkDestroyFramebuffer(device, framebuffer, nullptr);
    }
    m_swapchainFramebuffers.clear();
}

void CADApp::cleanup() {
    if (!m_initialized) return;

    auto device = m_vkContext->getDevice();

    vkDestroyPipeline(device, m_pipeline, nullptr);
    vkDestroyPipelineLayout(device, m_pipelineLayout, nullptr);
    vkDestroyRenderPass(device, m_renderPass, nullptr);

    cleanupSwapchain();
    m_vkContext.reset();

    m_initialized = false;
}

void CADApp::onWindowResized(ANativeWindow* window, int32_t width, int32_t height) {
    if (width <= 0 || height <= 0) return;

    m_pendingResize = true;
    try
    {
        m_vkContext.reset();
        cleanupSwapchain();

        m_vkContext = std::make_unique<rendering::VulkanContext>(window);
        initSwapchain();
        initFramebuffers();
    }
    catch (const std::exception& e)
    {
        LOGE("onWindowResized failed: %s", e.what());
        m_vkContext.reset();
        m_swapchain.reset();
    }
    m_pendingResize = false;
}

void CADApp::drawFrame() {
    m_drawFrameCount++;
    LOGI("drawFrame #%u  (vkCtx=%p swap=%p pipe=%p)", m_drawFrameCount,
         (void*)m_vkContext.get(), (void*)m_swapchain.get(), (void*)m_pipeline);
    auto device = m_vkContext->getDevice();
    uint32_t frame = m_currentFrame % m_vkContext->getMaxFrames();

    VkFence fence = m_vkContext->getFence(frame);
    vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX);
    vkResetFences(device, 1, &fence);

    VkSemaphore waitSem = m_vkContext->getWaitSemaphore(frame);
    VkSemaphore signalSem = m_vkContext->getSignalSemaphore(frame);
    VkCommandBuffer cmdBuf = m_vkContext->getCmdBuffer(frame);

    uint32_t imageIndex;
    auto result = vkAcquireNextImageKHR(
        device,
        m_swapchain->getSwapchain(),
        UINT64_MAX,
        waitSem,
        VK_NULL_HANDLE,
        &imageIndex
    );

    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
        LOGI("swapchain out of date, recreating");
        cleanupSwapchain();
        initSwapchain();
        initFramebuffers();
        return;
    } else if (result != VK_SUCCESS) {
        throw std::runtime_error("Failed to acquire swapchain image");
    }

    // Reset and record command buffer
    vkResetCommandBuffer(cmdBuf, 0);
    recordCommandBuffer(cmdBuf, imageIndex);

    // Submit
    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

    VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = &waitSem;
    submitInfo.pWaitDstStageMask = waitStages;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmdBuf;
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = &signalSem;

    if (vkQueueSubmit(m_vkContext->getGraphicsQueue(), 1, &submitInfo, fence) != VK_SUCCESS) {
        throw std::runtime_error("Failed to submit draw command buffer");
    }

    // Present
    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = &signalSem;
    VkSwapchainKHR swapchains[] = {m_swapchain->getSwapchain()};
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = swapchains;
    presentInfo.pImageIndices = &imageIndex;

    result = vkQueuePresentKHR(m_vkContext->getPresentQueue(), &presentInfo);
    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
        cleanupSwapchain();
        initSwapchain();
        initFramebuffers();
    } else if (result != VK_SUCCESS) {
        throw std::runtime_error("Failed to present swapchain image");
    }

    m_currentFrame = (m_currentFrame + 1) % m_vkContext->getMaxFrames();
}

void CADApp::recordCommandBuffer(VkCommandBuffer commandBuffer, uint32_t imageIndex) {
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = 0;
    beginInfo.pInheritanceInfo = nullptr;

    if (vkBeginCommandBuffer(commandBuffer, &beginInfo) != VK_SUCCESS) {
        throw std::runtime_error("Failed to begin recording command buffer");
    }

    VkRenderPassBeginInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass = m_renderPass;
    renderPassInfo.framebuffer = m_swapchainFramebuffers[imageIndex];
    renderPassInfo.renderArea.offset = {0, 0};
    renderPassInfo.renderArea.extent = m_swapchain->getExtent();

    VkClearValue clearColor{};
    clearColor.color = {{0.1f, 0.1f, 0.1f, 1.0f}};
    renderPassInfo.clearValueCount = 1;
    renderPassInfo.pClearValues = &clearColor;

    vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline);
    vkCmdDraw(commandBuffer, kMaxVertexCount, 1, 0, 0);
    vkCmdEndRenderPass(commandBuffer);

    if (vkEndCommandBuffer(commandBuffer) != VK_SUCCESS) {
        throw std::runtime_error("Failed to record command buffer");
    }
}

// Vertex helper methods
std::vector<VkVertexInputBindingDescription> Vertex::getBindingDescriptions() {
    VkVertexInputBindingDescription bindingDescription{};
    bindingDescription.binding = 0;
    bindingDescription.stride = sizeof(Vertex);
    bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    return {bindingDescription};
}

std::vector<VkVertexInputAttributeDescription> Vertex::getAttributeDescriptions() {
    std::vector<VkVertexInputAttributeDescription> attributes(2);

    attributes[0].binding = 0;
    attributes[0].location = 0;
    attributes[0].format = VK_FORMAT_R32G32B32_SFLOAT;
    attributes[0].offset = offsetof(Vertex, position);

    attributes[1].binding = 0;
    attributes[1].location = 1;
    attributes[1].format = VK_FORMAT_R32G32B32_SFLOAT;
    attributes[1].offset = offsetof(Vertex, color);

    return attributes;
}

} // namespace app
