#include "app/CADApp.h"

#include <android/input.h>
#include <android/log.h>

#include <array>
#include <cmath>
#include <stdexcept>

#include "imgui.h"
#include "imgui_impl_android.h"
#include "imgui_impl_vulkan.h"
#include "utils/math/Mat4.h"

#define LOG_TAG "MobileCAD"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

namespace app
{

CADApp::CADApp(ANativeWindow* window)
{
    m_camera.m_target = {0.0f, 0.75f, 0.0f};
    m_camera.m_yaw = 0.785398f;    // 45 deg
    m_camera.m_pitch = 0.523599f;  // 30 deg
    m_camera.m_distance = 7.0f;

    init(window);
}

CADApp::~CADApp()
{
    cleanup();
}

void CADApp::init(ANativeWindow* window)
{
    LOGI("CADApp::init: starting");
    try
    {
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

        LOGI("Initializing scene geometry (grid, XYZ triad, solid block)");
        initSceneGeometry();
        LOGI("Scene geometry initialized");

        LOGI("Initializing Dear ImGui");
        initImGui(window);
        LOGI("Dear ImGui initialized");

        m_initialized = true;
        LOGI("CADApp::init: done");
    }
    catch (const std::exception& e)
    {
        LOGE("CADApp::init failed: %s", e.what());
        throw;
    }
}

void CADApp::initSwapchain()
{
    auto width = m_vkContext->getWidth();
    auto height = m_vkContext->getHeight();
    m_swapchain = std::make_unique<rendering::Swapchain>(
        m_vkContext.get(), static_cast<uint32_t>(width), static_cast<uint32_t>(height), m_vsync);
}

void CADApp::initRenderPass()
{
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

    VkAttachmentDescription depthAttachment{};
    depthAttachment.format = m_swapchain->getDepthFormat();
    depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference depthAttachmentRef{};
    depthAttachmentRef.attachment = 1;
    depthAttachmentRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorAttachmentRef;
    subpass.pDepthStencilAttachment = &depthAttachmentRef;

    VkSubpassDependency dependency{};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask =
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependency.srcAccessMask = 0;
    dependency.dstStageMask =
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependency.dstAccessMask =
        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    std::array<VkAttachmentDescription, 2> attachments = {colorAttachment, depthAttachment};

    VkRenderPassCreateInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
    renderPassInfo.pAttachments = attachments.data();
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    renderPassInfo.dependencyCount = 1;
    renderPassInfo.pDependencies = &dependency;

    auto device = m_vkContext->getDevice();
    if (vkCreateRenderPass(device, &renderPassInfo, nullptr, &m_renderPass) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to create render pass");
    }
}

struct PushConstantData
{
    utils::math::Mat4f mvp;
    utils::math::Mat4f model;
};

void CADApp::initPipelineLayout()
{
    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(PushConstantData);

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 0;
    pipelineLayoutInfo.pSetLayouts = nullptr;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

    auto device = m_vkContext->getDevice();
    if (vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &m_pipelineLayout) !=
        VK_SUCCESS)
    {
        throw std::runtime_error("Failed to create pipeline layout");
    }
}

void CADApp::initGraphicsPipeline()
{
    auto device = m_vkContext->getDevice();

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

    auto bindingDescription = Vertex::getBindingDescription();
    auto attributeDescriptions = Vertex::getAttributeDescriptions();

    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount = 1;
    vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
    vertexInputInfo.vertexAttributeDescriptionCount =
        static_cast<uint32_t>(attributeDescriptions.size());
    vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions.data();

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    inputAssembly.primitiveRestartEnable = VK_FALSE;

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

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_NONE;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_FALSE;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                          VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachment.blendEnable = VK_FALSE;

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_TRUE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    depthStencil.depthBoundsTestEnable = VK_FALSE;
    depthStencil.stencilTestEnable = VK_FALSE;

    std::vector<VkDynamicState> dynamicStates = {VK_DYNAMIC_STATE_VIEWPORT,
                                                 VK_DYNAMIC_STATE_SCISSOR};

    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
    dynamicState.pDynamicStates = dynamicStates.data();

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = stages;
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = m_pipelineLayout;
    pipelineInfo.renderPass = m_renderPass;
    pipelineInfo.subpass = 0;
    pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;

    // 1. Create solid triangle pipeline (for 3D solid CAD part)
    if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_pipeline) !=
        VK_SUCCESS)
    {
        throw std::runtime_error("Failed to create solid graphics pipeline");
    }

    // 2. Create line pipeline (for CAD reference grid and XYZ origin triad)
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
    if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr,
                                  &m_linePipeline) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to create line graphics pipeline");
    }

    vkDestroyShaderModule(device, fragModule, nullptr);
    vkDestroyShaderModule(device, vertModule, nullptr);
}

void CADApp::initFramebuffers()
{
    auto swapchainImages = m_swapchain->getImages();
    m_swapchainFramebuffers.resize(swapchainImages.size());

    for (size_t i = 0; i < swapchainImages.size(); ++i)
    {
        VkImageView attachments[] = {m_swapchain->getImageView(i),
                                     m_swapchain->getDepthImageView()};

        VkFramebufferCreateInfo framebufferInfo{};
        framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebufferInfo.renderPass = m_renderPass;
        framebufferInfo.attachmentCount = 2;
        framebufferInfo.pAttachments = attachments;
        framebufferInfo.width = m_swapchain->getExtent().width;
        framebufferInfo.height = m_swapchain->getExtent().height;
        framebufferInfo.layers = 1;

        auto device = m_vkContext->getDevice();
        if (vkCreateFramebuffer(device, &framebufferInfo, nullptr, &m_swapchainFramebuffers[i]) !=
            VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create framebuffer");
        }
    }
}

void CADApp::initSceneGeometry()
{
    auto device = m_vkContext->getDevice();
    auto physDevice = m_vkContext->getPhysicalDevice();

    // 1. Grid & XYZ Axes (m_gridMesh, LINE_LIST)
    std::vector<Vertex> gridVertices;
    const float gridMin = -5.0f;
    const float gridMax = 5.0f;
    const float step = 0.5f;
    const float slateGray[3] = {0.32f, 0.38f, 0.48f};
    const float red[3] = {0.95f, 0.25f, 0.25f};
    const float green[3] = {0.25f, 0.90f, 0.35f};
    const float blue[3] = {0.25f, 0.55f, 1.0f};

    // Lines parallel to Z (varying Z at constant X)
    for (float x = gridMin; x <= gridMax + 0.001f; x += step)
    {
        if (std::abs(x) < 0.001f)
        {
            // Negative Z: slate gray
            gridVertices.push_back({{0.0f, 0.0f, gridMin},
                                    {0.0f, 0.0f, 0.0f},
                                    {slateGray[0], slateGray[1], slateGray[2]}});
            gridVertices.push_back({{0.0f, 0.0f, 0.0f},
                                    {0.0f, 0.0f, 0.0f},
                                    {slateGray[0], slateGray[1], slateGray[2]}});
            // Positive Z axis (+Z): bright CAD Blue
            gridVertices.push_back(
                {{0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, {blue[0], blue[1], blue[2]}});
            gridVertices.push_back(
                {{0.0f, 0.0f, gridMax}, {0.0f, 0.0f, 0.0f}, {blue[0], blue[1], blue[2]}});
        }
        else
        {
            gridVertices.push_back({{x, 0.0f, gridMin},
                                    {0.0f, 0.0f, 0.0f},
                                    {slateGray[0], slateGray[1], slateGray[2]}});
            gridVertices.push_back({{x, 0.0f, gridMax},
                                    {0.0f, 0.0f, 0.0f},
                                    {slateGray[0], slateGray[1], slateGray[2]}});
        }
    }

    // Lines parallel to X (varying X at constant Z)
    for (float z = gridMin; z <= gridMax + 0.001f; z += step)
    {
        if (std::abs(z) < 0.001f)
        {
            // Negative X: slate gray
            gridVertices.push_back({{gridMin, 0.0f, 0.0f},
                                    {0.0f, 0.0f, 0.0f},
                                    {slateGray[0], slateGray[1], slateGray[2]}});
            gridVertices.push_back({{0.0f, 0.0f, 0.0f},
                                    {0.0f, 0.0f, 0.0f},
                                    {slateGray[0], slateGray[1], slateGray[2]}});
            // Positive X axis (+X): bright CAD Red
            gridVertices.push_back(
                {{0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, {red[0], red[1], red[2]}});
            gridVertices.push_back(
                {{gridMax, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, {red[0], red[1], red[2]}});
        }
        else
        {
            gridVertices.push_back({{gridMin, 0.0f, z},
                                    {0.0f, 0.0f, 0.0f},
                                    {slateGray[0], slateGray[1], slateGray[2]}});
            gridVertices.push_back({{gridMax, 0.0f, z},
                                    {0.0f, 0.0f, 0.0f},
                                    {slateGray[0], slateGray[1], slateGray[2]}});
        }
    }

    // Vertical +Y axis (0 to +2.5, bright CAD Green)
    gridVertices.push_back(
        {{0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, {green[0], green[1], green[2]}});
    gridVertices.push_back(
        {{0.0f, 2.5f, 0.0f}, {0.0f, 0.0f, 0.0f}, {green[0], green[1], green[2]}});

    m_gridMesh = std::make_unique<rendering::MeshBuffer>(device, physDevice, gridVertices);

    m_gridMesh = std::make_unique<rendering::MeshBuffer>(device, physDevice, gridVertices);

    // 2. Initial multi-object scene setup: create Part_1
    m_objects.clear();
    SceneObject initialObj;
    initialObj.name = "Part_1";
    initialObj.primitiveType = PrimitiveType::Box;
    m_objects.push_back(std::move(initialObj));
    m_selectedIndex = 0;
    rebuildObjectGeometry(0);
}

void CADApp::rebuildObjectGeometry(size_t index)
{
    if (index >= m_objects.size())
        return;

    auto device = m_vkContext->getDevice();
    auto physDevice = m_vkContext->getPhysicalDevice();

    vkDeviceWaitIdle(device);

    auto& obj = m_objects[index];
    geometry::Primitives::MeshData data;
    if (obj.params.csgMode == 0)
    {
        switch (obj.primitiveType)
        {
        case PrimitiveType::Box:
            data = geometry::Primitives::createBox(obj.params.width, obj.params.height,
                                                   obj.params.depth);
            break;
        case PrimitiveType::Cylinder:
            data = geometry::Primitives::createCylinder(obj.params.radius, obj.params.height,
                                                        static_cast<uint32_t>(obj.params.segments));
            break;
        case PrimitiveType::Sphere:
            data = geometry::Primitives::createSphere(
                obj.params.radius, static_cast<uint32_t>(obj.params.segments * 3 / 4),
                static_cast<uint32_t>(obj.params.segments));
            break;
        case PrimitiveType::Extrude:
            data = geometry::Primitives::createExtrudedPolygon(obj.sketchPoints, obj.params.height);
            break;
        }
    }
    else if (obj.params.csgMode == 1)
    {
        data = geometry::Primitives::createCsgDifference(obj.params.width, obj.params.height,
                                                         obj.params.depth, obj.params.holeRadius,
                                                         obj.params.segments);
    }
    else if (obj.params.csgMode == 2)
    {
        data = geometry::Primitives::createCsgUnion(obj.params.width, obj.params.height,
                                                    obj.params.depth, obj.params.radius,
                                                    obj.params.segments);
    }
    else if (obj.params.csgMode == 3)
    {
        data = geometry::Primitives::createCsgIntersection(obj.params.width, obj.params.height,
                                                           obj.params.depth, obj.params.radius,
                                                           obj.params.segments);
    }
    else if (obj.params.csgMode == 4)
    {
        data = geometry::Primitives::createHollowCylinder(obj.params.radius, obj.params.holeRadius,
                                                          obj.params.height, obj.params.segments);
    }

    obj.cpuVertices = data.first;
    obj.cpuIndices = data.second;
    obj.updateAABB();

    auto wireLines = geometry::Primitives::createWireframeLines(data.first, data.second);

    obj.solidMesh =
        std::make_unique<rendering::MeshBuffer>(device, physDevice, data.first, data.second);
    obj.wireframeMesh = std::make_unique<rendering::MeshBuffer>(device, physDevice, wireLines);

    auto selectedWireLines = wireLines;
    for (auto& v : selectedWireLines)
    {
        v.color[0] = 1.0f;
        v.color[1] = 0.78f;
        v.color[2] = 0.10f;
    }
    obj.selectedWireframeMesh =
        std::make_unique<rendering::MeshBuffer>(device, physDevice, selectedWireLines);
}

void CADApp::rebuildGeometry()
{
    for (size_t i = 0; i < m_objects.size(); ++i)
    {
        rebuildObjectGeometry(i);
    }
}

void CADApp::rebuildSketchMesh()
{
    if (m_sketchPoints.empty())
    {
        m_sketchLineMesh.reset();
        return;
    }

    auto device = m_vkContext->getDevice();
    auto physDevice = m_vkContext->getPhysicalDevice();
    vkDeviceWaitIdle(device);

    std::vector<rendering::Vertex> lineVerts;
    size_t n = m_sketchPoints.size();

    // 1. Draw small cross marker at each sketch vertex
    const float crossColor[3] = {1.0f, 1.0f, 1.0f};
    const float r = 0.04f;
    for (const auto& pt : m_sketchPoints)
    {
        lineVerts.push_back({{pt.x - r, 0.006f, pt.z},
                             {0.0f, 0.0f, 0.0f},
                             {crossColor[0], crossColor[1], crossColor[2]}});
        lineVerts.push_back({{pt.x + r, 0.006f, pt.z},
                             {0.0f, 0.0f, 0.0f},
                             {crossColor[0], crossColor[1], crossColor[2]}});
        lineVerts.push_back({{pt.x, 0.006f, pt.z - r},
                             {0.0f, 0.0f, 0.0f},
                             {crossColor[0], crossColor[1], crossColor[2]}});
        lineVerts.push_back({{pt.x, 0.006f, pt.z + r},
                             {0.0f, 0.0f, 0.0f},
                             {crossColor[0], crossColor[1], crossColor[2]}});
    }

    // 2. Line segments connecting consecutive points in bright Cyan
    const float cyan[3] = {0.0f, 0.95f, 1.0f};
    for (size_t i = 0; i + 1 < n; ++i)
    {
        lineVerts.push_back({{m_sketchPoints[i].x, 0.006f, m_sketchPoints[i].z},
                             {0.0f, 0.0f, 0.0f},
                             {cyan[0], cyan[1], cyan[2]}});
        lineVerts.push_back({{m_sketchPoints[i + 1].x, 0.006f, m_sketchPoints[i + 1].z},
                             {0.0f, 0.0f, 0.0f},
                             {cyan[0], cyan[1], cyan[2]}});
    }

    // 3. Closing segment in Yellow/Green if >= 3 points
    if (n >= 3)
    {
        const float yellowGreen[3] = {1.0f, 0.90f, 0.20f};
        lineVerts.push_back({{m_sketchPoints[n - 1].x, 0.006f, m_sketchPoints[n - 1].z},
                             {0.0f, 0.0f, 0.0f},
                             {yellowGreen[0], yellowGreen[1], yellowGreen[2]}});
        lineVerts.push_back({{m_sketchPoints[0].x, 0.006f, m_sketchPoints[0].z},
                             {0.0f, 0.0f, 0.0f},
                             {yellowGreen[0], yellowGreen[1], yellowGreen[2]}});
    }

    m_sketchLineMesh = std::make_unique<rendering::MeshBuffer>(device, physDevice, lineVerts);
}

void CADApp::undoSketchPoint()
{
    if (!m_sketchPoints.empty())
    {
        m_sketchPoints.pop_back();
        rebuildSketchMesh();
    }
}

void CADApp::clearSketch()
{
    m_sketchPoints.clear();
    m_sketchLineMesh.reset();
}

void CADApp::extrudeSketchToPart()
{
    if (m_sketchPoints.size() < 3 || !m_vkContext)
        return;

    SceneObject newObj;
    newObj.name = "Extrude_" + std::to_string(m_objects.size() + 1);
    newObj.primitiveType = PrimitiveType::Extrude;
    newObj.params.height = m_extrudeHeight;
    newObj.sketchPoints = m_sketchPoints;
    newObj.position = {0.0f, 0.0f, 0.0f};
    newObj.rotationDeg = {0.0f, 0.0f, 0.0f};

    m_objects.push_back(std::move(newObj));
    m_selectedIndex = static_cast<int>(m_objects.size() - 1);
    rebuildObjectGeometry(static_cast<size_t>(m_selectedIndex));

    m_sketchPoints.clear();
    m_sketchLineMesh.reset();
    m_sketchMode = false;

    LOGI("Extruded sketch profile to 3D Part: %s", m_objects[m_selectedIndex].name.c_str());
}

void CADApp::setPrimitive(PrimitiveType type)
{
    if (m_selectedIndex >= 0 && m_selectedIndex < static_cast<int>(m_objects.size()))
    {
        m_objects[m_selectedIndex].params.csgMode = 0;
        m_objects[m_selectedIndex].primitiveType = type;
        rebuildObjectGeometry(static_cast<size_t>(m_selectedIndex));
    }
}

bool CADApp::exportSTL()
{
    if (m_objects.empty())
    {
        m_exportStatus = "Export error: No geometry";
        return false;
    }

    std::vector<Vertex> combinedVertices;
    std::vector<uint32_t> combinedIndices;

    for (const auto& obj : m_objects)
    {
        if (obj.cpuVertices.empty() || obj.cpuIndices.size() < 3)
            continue;

        uint32_t baseIndex = static_cast<uint32_t>(combinedVertices.size());
        auto model = obj.getModelMatrix();
        auto normalMatrix = model.inverted().transposed();

        for (const auto& v : obj.cpuVertices)
        {
            Vertex tv = v;
            utils::math::Vec3f pos{v.position[0], v.position[1], v.position[2]};
            auto tPos = model * pos;
            tv.position[0] = tPos.x;
            tv.position[1] = tPos.y;
            tv.position[2] = tPos.z;

            utils::math::Vec3f norm{v.normal[0], v.normal[1], v.normal[2]};
            auto tNorm = normalMatrix.transformDirection(norm).normalized();
            tv.normal[0] = tNorm.x;
            tv.normal[1] = tNorm.y;
            tv.normal[2] = tNorm.z;

            combinedVertices.push_back(tv);
        }

        for (uint32_t idx : obj.cpuIndices)
        {
            combinedIndices.push_back(baseIndex + idx);
        }
    }

    if (combinedVertices.empty() || combinedIndices.size() < 3)
    {
        m_exportStatus = "Export error: Empty assembly";
        return false;
    }

    uint32_t triCount = static_cast<uint32_t>(combinedIndices.size() / 3);
    const std::string downloadPath = "/sdcard/Download/mobilecad_part.stl";

    std::remove(downloadPath.c_str());
    bool ok = io::MeshExporter::exportBinarySTL(downloadPath, combinedVertices, combinedIndices);
    if (ok)
    {
        char buf[256];
        snprintf(buf, sizeof(buf), "Exported %u tris (%zu parts) -> %s", triCount, m_objects.size(),
                 downloadPath.c_str());
        m_exportStatus = buf;
        LOGI("%s", buf);
        return true;
    }

    LOGI("Primary export to %s failed, falling back to internal storage: %s", downloadPath.c_str(),
         m_exportPath.c_str());

    std::remove(m_exportPath.c_str());
    ok = io::MeshExporter::exportBinarySTL(m_exportPath, combinedVertices, combinedIndices);
    if (ok)
    {
        char buf[256];
        snprintf(buf, sizeof(buf), "Exported %u tris (%zu parts) -> %s", triCount, m_objects.size(),
                 m_exportPath.c_str());
        m_exportStatus = buf;
        LOGI("%s", buf);
        return true;
    }

    m_exportStatus = "Export failed (check permissions)";
    LOGE("Failed to export STL to both %s and %s", downloadPath.c_str(), m_exportPath.c_str());
    return false;
}

void CADApp::initImGui(ANativeWindow* window)
{
    auto device = m_vkContext->getDevice();

    VkDescriptorPoolSize poolSizes[] = {{VK_DESCRIPTOR_TYPE_SAMPLER, 16},
                                        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 16},
                                        {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 16},
                                        {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 16},
                                        {VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 16},
                                        {VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 16},
                                        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 16},
                                        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 16},
                                        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 16},
                                        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 16},
                                        {VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 16}};

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    poolInfo.maxSets = 16 * sizeof(poolSizes) / sizeof(poolSizes[0]);
    poolInfo.poolSizeCount = static_cast<uint32_t>(sizeof(poolSizes) / sizeof(poolSizes[0]));
    poolInfo.pPoolSizes = poolSizes;

    if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &m_imguiDescriptorPool) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to create ImGui descriptor pool");
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename = nullptr;  // Disable ini storage on Android

    ImGui::StyleColorsDark();

    // High-DPI mobile touch scaling
    ImGuiStyle& style = ImGui::GetStyle();
    style.ScaleAllSizes(2.6f);
    io.FontGlobalScale = 2.6f;
    style.ScrollbarSize = 36.0f;
    style.TouchExtraPadding = ImVec2(8.0f, 8.0f);

    ImGui_ImplAndroid_Init(window);

    ImGui_ImplVulkan_InitInfo initInfo{};
    initInfo.ApiVersion = VK_API_VERSION_1_3;
    initInfo.Instance = m_vkContext->getInstance();
    initInfo.PhysicalDevice = m_vkContext->getPhysicalDevice();
    initInfo.Device = m_vkContext->getDevice();
    initInfo.QueueFamily = m_vkContext->getGraphicsQueueFamily();
    initInfo.Queue = m_vkContext->getGraphicsQueue();
    initInfo.DescriptorPool = m_imguiDescriptorPool;
    initInfo.MinImageCount = m_swapchain->getImageCount();
    initInfo.ImageCount = m_swapchain->getImageCount();
    initInfo.PipelineInfoMain.RenderPass = m_renderPass;
    initInfo.PipelineInfoMain.Subpass = 0;
    initInfo.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;

    if (!ImGui_ImplVulkan_Init(&initInfo))
    {
        throw std::runtime_error("Failed to initialize ImGui Vulkan backend");
    }
}

void CADApp::renderCompassGizmo()
{
    float screenW = static_cast<float>(getSwapchainExtentW());
    if (screenW <= 0.0f)
        return;

    ImVec2 center(screenW - 115.0f, 190.0f);
    float radius = 75.0f;

    ImDrawList* drawList = ImGui::GetBackgroundDrawList();
    if (!drawList)
        return;

    // Subtle translucent dark circle backing behind the compass
    drawList->AddCircleFilled(center, radius + 15.0f, IM_COL32(14, 20, 32, 175));
    drawList->AddCircle(center, radius + 15.0f, IM_COL32(70, 95, 130, 150), 32, 1.5f);
    drawList->AddCircleFilled(center, 3.5f, IM_COL32(220, 220, 220, 230));

    auto viewMat = m_camera.getViewMatrix();

    struct AxisInfo
    {
        const char* label;
        utils::math::Vec3f worldDir;
        utils::math::Vec3f viewDir;
        ImU32 color;
        ImU32 textColor;
        float tipRadius;
        float lineThick;
        bool isPositive;
    };

    std::vector<AxisInfo> axes = {
        {"+X",
         {1.0f, 0.0f, 0.0f},
         {},
         IM_COL32(245, 60, 60, 255),
         IM_COL32(255, 255, 255, 255),
         11.0f,
         2.8f,
         true},
        {"-X",
         {-1.0f, 0.0f, 0.0f},
         {},
         IM_COL32(140, 40, 40, 160),
         IM_COL32(220, 180, 180, 200),
         8.5f,
         1.5f,
         false},
        {"+Y",
         {0.0f, 1.0f, 0.0f},
         {},
         IM_COL32(50, 225, 70, 255),
         IM_COL32(255, 255, 255, 255),
         11.0f,
         2.8f,
         true},
        {"-Y",
         {0.0f, -1.0f, 0.0f},
         {},
         IM_COL32(35, 130, 45, 160),
         IM_COL32(180, 220, 180, 200),
         8.5f,
         1.5f,
         false},
        {"+Z",
         {0.0f, 0.0f, 1.0f},
         {},
         IM_COL32(65, 145, 255, 255),
         IM_COL32(255, 255, 255, 255),
         11.0f,
         2.8f,
         true},
        {"-Z",
         {0.0f, 0.0f, -1.0f},
         {},
         IM_COL32(35, 80, 160, 160),
         IM_COL32(180, 200, 230, 200),
         8.5f,
         1.5f,
         false},
    };

    for (auto& a : axes)
    {
        a.viewDir = viewMat.transformDirection(a.worldDir);
    }

    std::sort(axes.begin(), axes.end(),
              [](const AxisInfo& a, const AxisInfo& b) { return a.viewDir.z < b.viewDir.z; });

    for (const auto& a : axes)
    {
        ImVec2 tipPos(center.x + a.viewDir.x * radius, center.y - a.viewDir.y * radius);

        drawList->AddLine(center, tipPos, a.color, a.lineThick);
        drawList->AddCircleFilled(tipPos, a.tipRadius, a.color);
        drawList->AddCircle(tipPos, a.tipRadius, IM_COL32(255, 255, 255, a.isPositive ? 200 : 100),
                            16, 1.2f);

        const char* labelText = a.isPositive ? (a.label + 1) : a.label;
        ImVec2 txtSize = ImGui::CalcTextSize(labelText);
        drawList->AddText(ImVec2(tipPos.x - txtSize.x * 0.5f, tipPos.y - txtSize.y * 0.5f),
                          a.textColor, labelText);
    }
}

void CADApp::openNumpad(const char* title, float* target, float minVal, float maxVal,
                        float defaultVal, const char* format, std::function<void()> onChanged)
{
    m_numpad.title = title ? title : "Value";
    m_numpad.targetValue = target;
    m_numpad.minVal = minVal;
    m_numpad.maxVal = maxVal;
    m_numpad.defaultVal = defaultVal;
    m_numpad.onChanged = std::move(onChanged);

    char buf[64];
    if (target)
    {
        snprintf(buf, sizeof(buf), format, *target);
        m_numpad.inputBuffer = buf;
    }
    else
    {
        m_numpad.inputBuffer.clear();
    }
    m_numpadNeedOpen = true;
}

void CADApp::renderNumpadPopup()
{
    if (m_numpadNeedOpen)
    {
        ImGui::OpenPopup("Numeric Keypad");
        m_numpadNeedOpen = false;
    }

    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

    if (ImGui::BeginPopupModal("Numeric Keypad", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::Text("Set %s  [Range: %.1f to %.1f]", m_numpad.title.c_str(), m_numpad.minVal,
                    m_numpad.maxVal);
        ImGui::Separator();

        // Large display box showing current typed input string
        ImGui::BeginChild("NumpadDisplay", ImVec2(290.0f, 44.0f), true,
                          ImGuiWindowFlags_NoScrollbar);
        ImGui::SetWindowFontScale(1.3f);
        ImGui::TextUnformatted(m_numpad.inputBuffer.empty() ? "0" : m_numpad.inputBuffer.c_str());
        ImGui::SetWindowFontScale(1.0f);
        ImGui::EndChild();

        ImGui::Spacing();

        // Keypad grid
        float btnW = 64.0f;
        float btnH = 46.0f;
        ImVec2 btnSize(btnW, btnH);

        auto appendChar = [&](char c)
        {
            if (c == '.')
            {
                if (m_numpad.inputBuffer.find('.') == std::string::npos)
                {
                    if (m_numpad.inputBuffer.empty())
                        m_numpad.inputBuffer = "0.";
                    else
                        m_numpad.inputBuffer += '.';
                }
            }
            else
            {
                if (m_numpad.inputBuffer == "0")
                    m_numpad.inputBuffer.clear();
                m_numpad.inputBuffer += c;
            }
        };

        // Row 1: 7, 8, 9, Backspace
        if (ImGui::Button("7", btnSize))
            appendChar('7');
        ImGui::SameLine();
        if (ImGui::Button("8", btnSize))
            appendChar('8');
        ImGui::SameLine();
        if (ImGui::Button("9", btnSize))
            appendChar('9');
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.50f, 0.25f, 0.25f, 1.0f));
        if (ImGui::Button("<-", btnSize))
        {
            if (!m_numpad.inputBuffer.empty())
                m_numpad.inputBuffer.pop_back();
        }
        ImGui::PopStyleColor();

        // Row 2: 4, 5, 6, Clear
        if (ImGui::Button("4", btnSize))
            appendChar('4');
        ImGui::SameLine();
        if (ImGui::Button("5", btnSize))
            appendChar('5');
        ImGui::SameLine();
        if (ImGui::Button("6", btnSize))
            appendChar('6');
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.50f, 0.25f, 0.25f, 1.0f));
        if (ImGui::Button("C", btnSize))
        {
            m_numpad.inputBuffer.clear();
        }
        ImGui::PopStyleColor();

        // Row 3: 1, 2, 3, +/-
        if (ImGui::Button("1", btnSize))
            appendChar('1');
        ImGui::SameLine();
        if (ImGui::Button("2", btnSize))
            appendChar('2');
        ImGui::SameLine();
        if (ImGui::Button("3", btnSize))
            appendChar('3');
        ImGui::SameLine();
        if (ImGui::Button("+/-", btnSize))
        {
            if (!m_numpad.inputBuffer.empty() && m_numpad.inputBuffer[0] == '-')
                m_numpad.inputBuffer.erase(0, 1);
            else if (!m_numpad.inputBuffer.empty() && m_numpad.inputBuffer != "0")
                m_numpad.inputBuffer.insert(0, "-");
        }

        // Row 4: 0, ., Reset
        float zeroW = btnW * 2.0f + ImGui::GetStyle().ItemSpacing.x;
        if (ImGui::Button("0", ImVec2(zeroW, btnH)))
            appendChar('0');
        ImGui::SameLine();
        if (ImGui::Button(".", btnSize))
            appendChar('.');
        ImGui::SameLine();
        if (ImGui::Button("Rst", btnSize))
        {
            char buf[32];
            snprintf(buf, sizeof(buf), "%.2f", m_numpad.defaultVal);
            m_numpad.inputBuffer = buf;
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // Bottom action buttons: Apply and Cancel
        float actionBtnW =
            (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;

        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.65f, 0.28f, 1.0f));
        if (ImGui::Button("Apply", ImVec2(actionBtnW, 44.0f)))
        {
            if (m_numpad.targetValue)
            {
                try
                {
                    if (!m_numpad.inputBuffer.empty())
                    {
                        float val = std::stof(m_numpad.inputBuffer);
                        if (val < m_numpad.minVal)
                            val = m_numpad.minVal;
                        if (val > m_numpad.maxVal)
                            val = m_numpad.maxVal;
                        *m_numpad.targetValue = val;
                        if (m_numpad.onChanged)
                            m_numpad.onChanged();
                    }
                }
                catch (...)
                {
                }
            }
            ImGui::CloseCurrentPopup();
        }
        ImGui::PopStyleColor();

        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(actionBtnW, 44.0f)))
        {
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }
}

bool CADApp::drawNumericControl(const char* label, float* value, float step, float minVal,
                                float maxVal, const char* format, std::function<void()> onChanged)
{
    bool changed = false;
    ImGui::PushID(label);

    float btnW = 34.0f;
    float valW = 76.0f;
    float spacing = ImGui::GetStyle().ItemSpacing.x;
    float controlW = btnW * 2.0f + valW + spacing * 2.0f;

    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);

    float targetX = ImGui::GetWindowContentRegionMax().x - controlW;
    if (ImGui::GetCursorPosX() < targetX)
    {
        ImGui::SameLine(targetX);
    }
    else
    {
        ImGui::SameLine();
    }

    // [-] hold-repeat stepper
    ImGui::PushItemFlag(ImGuiItemFlags_ButtonRepeat, true);
    if (ImGui::Button("-##dec", ImVec2(btnW, 0.0f)))
    {
        *value -= step;
        if (*value < minVal)
            *value = minVal;
        changed = true;
    }
    ImGui::PopItemFlag();

    ImGui::SameLine();

    // [ value ] Direct-type button
    char valBuf[32];
    snprintf(valBuf, sizeof(valBuf), format, *value);
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.22f, 0.30f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.25f, 0.35f, 0.50f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.12f, 0.16f, 0.22f, 1.0f));
    if (ImGui::Button(valBuf, ImVec2(valW, 0.0f)))
    {
        openNumpad(label, value, minVal, maxVal, *value, format, onChanged);
    }
    ImGui::PopStyleColor(3);

    ImGui::SameLine();

    // [+] hold-repeat stepper
    ImGui::PushItemFlag(ImGuiItemFlags_ButtonRepeat, true);
    if (ImGui::Button("+##inc", ImVec2(btnW, 0.0f)))
    {
        *value += step;
        if (*value > maxVal)
            *value = maxVal;
        changed = true;
    }
    ImGui::PopItemFlag();

    ImGui::PopID();

    if (changed && onChanged)
    {
        onChanged();
    }
    return changed;
}

bool CADApp::drawVec3Control(const char* label, utils::math::Vec3f* vec, float step, float minVal,
                             float maxVal, float resetVal, std::function<void()> onChanged)
{
    bool changed = false;
    ImGui::PushID(label);

    ImGui::AlignTextToFramePadding();
    ImGui::Text("%s", label);
    ImGui::SameLine();
    if (ImGui::SmallButton("Reset"))
    {
        vec->x = resetVal;
        vec->y = resetVal;
        vec->z = resetVal;
        changed = true;
    }

    if (ImGui::BeginTable("##vec3tbl", 3, ImGuiTableFlags_SizingStretchSame))
    {
        struct AxisInfo
        {
            const char* name;
            float* val;
            ImVec4 col;
        };
        AxisInfo axes[3] = {
            {"X", &vec->x, ImVec4(1.0f, 0.40f, 0.40f, 1.0f)},
            {"Y", &vec->y, ImVec4(0.40f, 1.0f, 0.40f, 1.0f)},
            {"Z", &vec->z, ImVec4(0.40f, 0.65f, 1.0f, 1.0f)},
        };

        const char* fmt = (maxVal > 100.0f) ? "%.0f" : "%.2f";

        for (int i = 0; i < 3; ++i)
        {
            ImGui::TableNextColumn();
            ImGui::PushID(axes[i].name);

            // Colored axis letter
            ImGui::PushStyleColor(ImGuiCol_Text, axes[i].col);
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(axes[i].name);
            ImGui::PopStyleColor();

            ImGui::SameLine(0.0f, 2.0f);

            float colAvail = ImGui::GetContentRegionAvail().x;
            float btnW = 20.0f;
            float valW = colAvail - (btnW * 2.0f + 6.0f);
            if (valW < 36.0f)
                valW = 36.0f;

            // [-] button
            ImGui::PushItemFlag(ImGuiItemFlags_ButtonRepeat, true);
            if (ImGui::Button("-##dec", ImVec2(btnW, 0.0f)))
            {
                *axes[i].val -= step;
                if (*axes[i].val < minVal)
                    *axes[i].val = minVal;
                changed = true;
            }
            ImGui::PopItemFlag();

            ImGui::SameLine(0.0f, 2.0f);

            // [ val ] direct-type button
            char valBuf[32];
            snprintf(valBuf, sizeof(valBuf), fmt, *axes[i].val);
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.22f, 0.30f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.25f, 0.35f, 0.50f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.12f, 0.16f, 0.22f, 1.0f));
            if (ImGui::Button(valBuf, ImVec2(valW, 0.0f)))
            {
                std::string fullTitle = std::string(label) + " " + axes[i].name;
                openNumpad(fullTitle.c_str(), axes[i].val, minVal, maxVal, resetVal, fmt,
                           onChanged);
            }
            ImGui::PopStyleColor(3);

            ImGui::SameLine(0.0f, 2.0f);

            // [+] button
            ImGui::PushItemFlag(ImGuiItemFlags_ButtonRepeat, true);
            if (ImGui::Button("+##inc", ImVec2(btnW, 0.0f)))
            {
                *axes[i].val += step;
                if (*axes[i].val > maxVal)
                    *axes[i].val = maxVal;
                changed = true;
            }
            ImGui::PopItemFlag();

            ImGui::PopID();
        }
        ImGui::EndTable();
    }

    ImGui::PopID();

    if (changed && onChanged)
    {
        onChanged();
    }
    return changed;
}

void CADApp::renderImGui()
{
    renderCompassGizmo();

    if (!m_showMenu)
    {
        // Sleek floating [ + Open CAD Menu ] button below Android status bar
        ImGui::SetNextWindowPos(ImVec2(24.0f, 110.0f), ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(0.85f);
        ImGuiWindowFlags toggleFlags =
            ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
            ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
            ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMove;
        if (ImGui::Begin("##CADMenuToggle", nullptr, toggleFlags))
        {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.45f, 0.75f, 0.95f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.25f, 0.55f, 0.88f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.12f, 0.35f, 0.60f, 1.0f));
            if (ImGui::Button("[ + Open CAD Menu ]", ImVec2(360.0f, 75.0f)))
            {
                m_showMenu = true;
            }
            ImGui::PopStyleColor(3);
            ImGui::End();
        }
    }
    else
    {
        float screenW = ImGui::GetIO().DisplaySize.x;
        float screenH = ImGui::GetIO().DisplaySize.y;
        float fullW = screenW - 40.0f;
        float compactW = 520.0f;

        ImGui::SetNextWindowPos(ImVec2(20.0f, 110.0f), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(fullW, 0.0f), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSizeConstraints(ImVec2(450.0f, 220.0f),
                                            ImVec2(screenW - 20.0f, screenH - 130.0f));
        ImGui::SetNextWindowBgAlpha(0.92f);
        if (ImGui::Begin("MobileCAD Studio", &m_showMenu, ImGuiWindowFlags_HorizontalScrollbar))
        {
            ImVec2 curPos = ImGui::GetWindowPos();
            if (curPos.y < 100.0f)
            {
                ImGui::SetWindowPos(ImVec2(curPos.x, 110.0f));
            }

            // 1. Performance & Stats
            float fps = ImGui::GetIO().Framerate;
            uint32_t solidVerts = 0;
            uint32_t solidTris = 0;
            for (const auto& obj : m_objects)
            {
                solidVerts += static_cast<uint32_t>(obj.cpuVertices.size());
                solidTris += static_cast<uint32_t>(obj.cpuIndices.size() / 3);
            }
            uint32_t totalVerts = solidVerts;
            if (m_gridMesh && m_showGrid)
            {
                totalVerts += m_gridMesh->getVertexCount();
            }
            for (const auto& obj : m_objects)
            {
                if (obj.wireframeMesh && m_showWireframe)
                {
                    totalVerts += obj.wireframeMesh->getVertexCount();
                }
            }

            ImGui::TextColored(ImVec4(0.35f, 0.85f, 1.0f, 1.0f), "Vulkan 1.4 Native CAD");
            ImGui::SameLine();

            float rightEdge = ImGui::GetWindowContentRegionMax().x;
            float hideBtnW = 75.0f;
            float toggleBtnW = m_fullWidth ? 115.0f : 140.0f;
            float spacing = ImGui::GetStyle().ItemSpacing.x;
            float targetX = rightEdge - (hideBtnW + toggleBtnW + spacing);

            if (ImGui::GetCursorPosX() < targetX)
            {
                ImGui::SameLine(targetX);
            }

            if (ImGui::SmallButton(m_fullWidth ? "Compact" : "Full Width"))
            {
                m_fullWidth = !m_fullWidth;
                ImGui::SetWindowSize(ImVec2(m_fullWidth ? fullW : compactW, 0.0f));
            }

            ImGui::SameLine();
            if (ImGui::SmallButton("Hide"))
            {
                m_showMenu = false;
            }

            ImGui::Text("FPS: %.1f (%.2f ms/f) | %s", fps, 1000.0f / (fps > 0.0f ? fps : 60.0f),
                        m_vsync ? "120Hz VSync" : "Uncapped Benchmark");
            ImGui::Text("Assembly: %zu parts | %u tris (Total: %u)", m_objects.size(), solidTris,
                        totalVerts);
            ImGui::Separator();

            // 2. View Presets (4 clean side-by-side buttons + Reset)
            ImGui::AlignTextToFramePadding();
            ImGui::Text("Camera Presets:");
            ImGui::SameLine();
            if (ImGui::SmallButton("Reset"))
            {
                m_camera.resetView();
            }

            float availW = ImGui::GetContentRegionAvail().x;
            spacing = ImGui::GetStyle().ItemSpacing.x;
            float presetBtnW = (availW - spacing * 3.0f) * 0.25f;

            if (ImGui::Button("Iso", ImVec2(presetBtnW, 0.0f)))
            {
                m_camera.setViewIso();
            }
            ImGui::SameLine();
            if (ImGui::Button("Top", ImVec2(presetBtnW, 0.0f)))
            {
                m_camera.setViewTop();
            }
            ImGui::SameLine();
            if (ImGui::Button("Front", ImVec2(presetBtnW, 0.0f)))
            {
                m_camera.setViewFront();
            }
            ImGui::SameLine();
            if (ImGui::Button("Right", ImVec2(presetBtnW, 0.0f)))
            {
                m_camera.setViewRight();
            }

            // 3. Scene Assembly Section
            if (ImGui::CollapsingHeader("Scene Assembly", ImGuiTreeNodeFlags_DefaultOpen))
            {
                if (ImGui::Button("+ Add Part"))
                {
                    SceneObject newObj;
                    newObj.name = "Part_" + std::to_string(m_objects.size() + 1);
                    newObj.position = {static_cast<float>(m_objects.size()) * 1.5f, 0.0f, 0.0f};
                    m_objects.push_back(std::move(newObj));
                    m_selectedIndex = static_cast<int>(m_objects.size() - 1);
                    rebuildObjectGeometry(static_cast<size_t>(m_selectedIndex));
                }
                ImGui::SameLine();
                if (ImGui::Button("Duplicate"))
                {
                    if (m_selectedIndex >= 0 &&
                        m_selectedIndex < static_cast<int>(m_objects.size()))
                    {
                        const auto& src = m_objects[m_selectedIndex];
                        SceneObject dup;
                        dup.name = src.name + "_copy";
                        dup.primitiveType = src.primitiveType;
                        dup.params = src.params;
                        dup.sketchPoints = src.sketchPoints;
                        dup.position = src.position + utils::math::Vec3f(0.8f, 0.0f, 0.8f);
                        dup.rotationDeg = src.rotationDeg;
                        dup.scale = src.scale;
                        m_objects.push_back(std::move(dup));
                        m_selectedIndex = static_cast<int>(m_objects.size() - 1);
                        rebuildObjectGeometry(static_cast<size_t>(m_selectedIndex));
                    }
                }
                ImGui::SameLine();
                if (ImGui::Button("Delete"))
                {
                    if (m_selectedIndex >= 0 &&
                        m_selectedIndex < static_cast<int>(m_objects.size()))
                    {
                        m_objects.erase(m_objects.begin() + m_selectedIndex);
                        if (m_objects.empty())
                        {
                            m_selectedIndex = -1;
                        }
                        else if (m_selectedIndex >= static_cast<int>(m_objects.size()))
                        {
                            m_selectedIndex = static_cast<int>(m_objects.size()) - 1;
                        }
                    }
                }

                if (!m_objects.empty())
                {
                    const char* previewName = (m_selectedIndex >= 0 &&
                                               m_selectedIndex < static_cast<int>(m_objects.size()))
                                                  ? m_objects[m_selectedIndex].name.c_str()
                                                  : "(None selected)";
                    if (ImGui::BeginCombo("Select Part", previewName))
                    {
                        for (int i = 0; i < static_cast<int>(m_objects.size()); ++i)
                        {
                            bool isSelected = (m_selectedIndex == i);
                            if (ImGui::Selectable(m_objects[i].name.c_str(), isSelected))
                            {
                                m_selectedIndex = i;
                            }
                            if (isSelected)
                            {
                                ImGui::SetItemDefaultFocus();
                            }
                        }
                        ImGui::EndCombo();
                    }
                }
                else
                {
                    ImGui::TextDisabled("(Scene is empty — tap [+ Add Part] or 2D Sketch)");
                }
            }

            // 4. 2D Sketch & Extrude Section
            if (ImGui::CollapsingHeader("2D Sketch & Extrude"))
            {
                ImGui::Checkbox("2D Sketch Mode (Tap Ground Plane)", &m_sketchMode);
                ImGui::SameLine();
                if (ImGui::Button("Snap View to Top"))
                {
                    m_camera.setViewTop();
                }

                ImGui::Checkbox("Snap to 0.25 Grid", &m_snapToGrid);

                ImGui::Text("Sketch Points: %zu", m_sketchPoints.size());

                if (ImGui::Button("Undo Point") && !m_sketchPoints.empty())
                {
                    undoSketchPoint();
                }
                ImGui::SameLine();
                if (ImGui::Button("Clear Sketch") && !m_sketchPoints.empty())
                {
                    clearSketch();
                }

                drawNumericControl("Extrude Height", &m_extrudeHeight, 0.05f, 0.2f, 4.0f, "%.2f");

                bool canExtrude = (m_sketchPoints.size() >= 3);
                if (!canExtrude)
                {
                    ImGui::BeginDisabled();
                }
                if (ImGui::Button("Extrude Profile to 3D Part", ImVec2(-1.0f, 0.0f)))
                {
                    extrudeSketchToPart();
                }
                if (!canExtrude)
                {
                    ImGui::EndDisabled();
                }
            }

            // 5. Transform Inspector & Parametric Modeling for Selected Part
            if (m_selectedIndex >= 0 && m_selectedIndex < static_cast<int>(m_objects.size()))
            {
                auto& obj = m_objects[m_selectedIndex];

                if (ImGui::CollapsingHeader("Transform Inspector"))
                {
                    drawVec3Control("Position", &obj.position, 0.05f, -5.0f, 5.0f, 0.0f);
                    drawVec3Control("Rotate (deg)", &obj.rotationDeg, 1.0f, -180.0f, 180.0f, 0.0f);
                    drawVec3Control("Scale", &obj.scale, 0.05f, 0.05f, 10.0f, 1.0f);
                }

                if (ImGui::CollapsingHeader("Parameters & CSG"))
                {
                    bool changed = false;
                    auto onGeomChanged = [this]()
                    {
                        if (m_selectedIndex >= 0 &&
                            m_selectedIndex < static_cast<int>(m_objects.size()))
                            rebuildObjectGeometry(static_cast<size_t>(m_selectedIndex));
                    };

                    const char* csgItems[] = {"Solid Primitive", "Subtract (Drill Hole)",
                                              "Union (Boss Hub)", "Intersect (Box ^ Sphere)",
                                              "Hollow Cylinder Pipe"};
                    if (ImGui::Combo("CSG Op", &obj.params.csgMode, csgItems,
                                     IM_ARRAYSIZE(csgItems)))
                    {
                        changed = true;
                    }

                    if (obj.params.csgMode == 0)
                    {
                        if (obj.primitiveType == PrimitiveType::Extrude)
                        {
                            ImGui::Text("Extruded Solid Part (%zu sketch points)",
                                        obj.sketchPoints.size());
                            changed |= drawNumericControl("Extrude Height", &obj.params.height,
                                                          0.05f, 0.2f, 4.0f, "%.2f", onGeomChanged);
                        }
                        else
                        {
                            ImGui::Text("Primitive:");
                            if (ImGui::RadioButton("Box", obj.primitiveType == PrimitiveType::Box))
                            {
                                obj.primitiveType = PrimitiveType::Box;
                                changed = true;
                            }
                            ImGui::SameLine();
                            if (ImGui::RadioButton("Cylinder",
                                                   obj.primitiveType == PrimitiveType::Cylinder))
                            {
                                obj.primitiveType = PrimitiveType::Cylinder;
                                changed = true;
                            }
                            ImGui::SameLine();
                            if (ImGui::RadioButton("Sphere",
                                                   obj.primitiveType == PrimitiveType::Sphere))
                            {
                                obj.primitiveType = PrimitiveType::Sphere;
                                changed = true;
                            }
                        }
                    }

                    // Parametric Steppers with direct numpad
                    if (obj.primitiveType == PrimitiveType::Box || obj.params.csgMode > 0)
                    {
                        changed |= drawNumericControl("Width", &obj.params.width, 0.05f, 0.2f, 4.0f,
                                                      "%.2f", onGeomChanged);
                        changed |= drawNumericControl("Height", &obj.params.height, 0.05f, 0.2f,
                                                      4.0f, "%.2f", onGeomChanged);
                        changed |= drawNumericControl("Depth", &obj.params.depth, 0.05f, 0.2f, 4.0f,
                                                      "%.2f", onGeomChanged);
                    }
                    if (obj.primitiveType == PrimitiveType::Cylinder ||
                        obj.primitiveType == PrimitiveType::Sphere || obj.params.csgMode > 0)
                    {
                        changed |= drawNumericControl("Radius", &obj.params.radius, 0.05f, 0.1f,
                                                      2.0f, "%.2f", onGeomChanged);
                    }
                    if (obj.params.csgMode == 1 || obj.params.csgMode == 4)
                    {
                        changed |= drawNumericControl("Hole Radius", &obj.params.holeRadius, 0.02f,
                                                      0.05f, 1.8f, "%.2f", onGeomChanged);
                    }
                    if (obj.primitiveType == PrimitiveType::Cylinder ||
                        obj.primitiveType == PrimitiveType::Sphere || obj.params.csgMode > 0)
                    {
                        float segs = static_cast<float>(obj.params.segments);
                        if (drawNumericControl("Segments", &segs, 2.0f, 8.0f, 64.0f, "%.0f",
                                               [&obj, &segs, onGeomChanged]()
                                               {
                                                   obj.params.segments = static_cast<int>(segs);
                                                   onGeomChanged();
                                               }))
                        {
                            obj.params.segments = static_cast<int>(segs);
                            changed = true;
                        }
                    }

                    if (changed)
                    {
                        rebuildObjectGeometry(static_cast<size_t>(m_selectedIndex));
                    }
                }
            }

            // 4. Display Layers
            if (ImGui::CollapsingHeader("Display Layers & VSync"))
            {
                ImGui::Checkbox("Grid & Triad", &m_showGrid);
                ImGui::SameLine();
                ImGui::Checkbox("Solid", &m_showSolid);
                ImGui::SameLine();
                ImGui::Checkbox("Wireframe", &m_showWireframe);

                if (ImGui::Checkbox("VSync (120Hz Lock)", &m_vsync))
                {
                    m_vsyncChanged = true;
                }
            }

            // 5. Watertight Binary STL Export
            if (ImGui::CollapsingHeader("Binary STL Export"))
            {
                if (ImGui::Button("Export STL Model", ImVec2(-1.0f, 0.0f)))
                {
                    exportSTL();
                }
                if (!m_exportStatus.empty())
                {
                    ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "%s",
                                       m_exportStatus.c_str());
                }
                ImGui::TextDisabled("Target: /sdcard/Download/mobilecad_part.stl");
            }

            // 6. Graceful Exit
            ImGui::Separator();
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.75f, 0.18f, 0.18f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.88f, 0.25f, 0.25f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.60f, 0.12f, 0.12f, 1.0f));
            if (ImGui::Button("Exit App", ImVec2(-1.0f, 0.0f)))
            {
                ImGui::OpenPopup("Confirm Exit");
            }
            ImGui::PopStyleColor(3);

            ImVec2 center = ImGui::GetMainViewport()->GetCenter();
            ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
            if (ImGui::BeginPopupModal("Confirm Exit", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
            {
                ImGui::Text("Exit MobileCAD and return to Android?");
                ImGui::Separator();
                if (ImGui::Button("Yes, Exit", ImVec2(180, 0)))
                {
                    ImGui::CloseCurrentPopup();
                    if (m_exitCallback)
                    {
                        m_exitCallback();
                    }
                }
                ImGui::SameLine();
                if (ImGui::Button("Cancel", ImVec2(180, 0)))
                {
                    ImGui::CloseCurrentPopup();
                }
                ImGui::EndPopup();
            }
        }
        ImGui::End();
    }

    renderNumpadPopup();
}

void CADApp::cleanupSwapchain()
{
    auto device = m_vkContext->getDevice();

    for (auto framebuffer : m_swapchainFramebuffers)
    {
        vkDestroyFramebuffer(device, framebuffer, nullptr);
    }
    m_swapchainFramebuffers.clear();
}

void CADApp::cleanup()
{
    if (!m_initialized)
        return;

    // If device was lost, all Vulkan handles are invalid — attempting vkDestroy* on them
    // can crash inside the driver (see tombstone crash chain in BUILD_FIXES.md §17).
    // Skip all cleanup; OS reclaims the address space on process exit.
    if (m_deviceLost)
    {
        LOGE("cleanup: device lost — skipping vkDestroy calls");
        return;
    }

    auto device = m_vkContext->getDevice();
    if (device != VK_NULL_HANDLE)
    {
        vkDeviceWaitIdle(device);
    }

    m_objects.clear();
    m_sketchLineMesh.reset();
    if (m_gridMesh)
    {
        m_gridMesh.reset();
    }
    if (m_imguiDescriptorPool != VK_NULL_HANDLE)
    {
        ImGui_ImplVulkan_Shutdown();
        ImGui_ImplAndroid_Shutdown();
        ImGui::DestroyContext();
        vkDestroyDescriptorPool(device, m_imguiDescriptorPool, nullptr);
        m_imguiDescriptorPool = VK_NULL_HANDLE;
    }
    if (m_linePipeline != VK_NULL_HANDLE)
    {
        vkDestroyPipeline(device, m_linePipeline, nullptr);
        m_linePipeline = VK_NULL_HANDLE;
    }
    if (m_pipeline != VK_NULL_HANDLE)
    {
        vkDestroyPipeline(device, m_pipeline, nullptr);
        m_pipeline = VK_NULL_HANDLE;
    }
    if (m_pipelineLayout != VK_NULL_HANDLE)
    {
        vkDestroyPipelineLayout(device, m_pipelineLayout, nullptr);
        m_pipelineLayout = VK_NULL_HANDLE;
    }
    if (m_renderPass != VK_NULL_HANDLE)
    {
        vkDestroyRenderPass(device, m_renderPass, nullptr);
        m_renderPass = VK_NULL_HANDLE;
    }

    cleanupSwapchain();
    m_swapchain.reset();
    m_vkContext.reset();

    m_initialized = false;
}

void CADApp::onWindowResized(ANativeWindow* /*window*/, int32_t width, int32_t height)
{
    LOGI("onWindowResized ENTRY %dx%d vkCtx=%p swap=%p", width, height, (void*)m_vkContext.get(),
         (void*)m_swapchain.get());
    if (width <= 0 || height <= 0)
        return;
    if (!m_vkContext)
        return;

    m_vkContext->setDimensions(static_cast<uint32_t>(width), static_cast<uint32_t>(height));

    auto device = m_vkContext->getDevice();
    vkDeviceWaitIdle(device);

    cleanupSwapchain();
    m_swapchain.reset();
    initSwapchain();
    initFramebuffers();

    LOGI("onWindowResized: rebuild complete  swap=%dx%d", getSwapchainExtentW(),
         getSwapchainExtentH());
}

void CADApp::onTouchEvent(int32_t action, int32_t pointerCount, float x0, float y0, float x1,
                          float y1)
{
    if (action == AMOTION_EVENT_ACTION_DOWN)
    {
        m_activePointers = 1;
        m_downX = x0;
        m_downY = y0;
        m_maxDragDist = 0.0f;
        m_lastTouchX = x0;
        m_lastTouchY = y0;
        m_lastPinchDist = 0.0f;
        return;
    }

    if (action == AMOTION_EVENT_ACTION_POINTER_DOWN)
    {
        m_activePointers = pointerCount;
        m_lastTouchX = x0;
        m_lastTouchY = y0;
        if (pointerCount >= 2)
        {
            m_lastPinchDist = std::hypot(x1 - x0, y1 - y0);
            m_lastMidX = (x0 + x1) * 0.5f;
            m_lastMidY = (y0 + y1) * 0.5f;
        }
        return;
    }

    if (action == AMOTION_EVENT_ACTION_UP)
    {
        // 3D Ray-Picking / 2D Sketch Tap on clean tap (< 12.0f movement)
        if (m_activePointers == 1 && m_maxDragDist < 12.0f && m_swapchain)
        {
            float screenW = static_cast<float>(getSwapchainExtentW());
            float screenH = static_cast<float>(getSwapchainExtentH());
            if (screenW > 0.0f && screenH > 0.0f)
            {
                float aspect = screenW / screenH;
                auto viewProj = m_camera.getViewProjectionMatrix(aspect);
                auto ray = utils::math::Ray::screenPointToRay(x0, y0, screenW, screenH, viewProj);

                if (m_sketchMode)
                {
                    // Intersect with ground plane Y = 0
                    if (std::abs(ray.dir.y) > 1e-4f)
                    {
                        float t = -ray.origin.y / ray.dir.y;
                        if (t > 0.0f)
                        {
                            float hitX = ray.origin.x + t * ray.dir.x;
                            float hitZ = ray.origin.z + t * ray.dir.z;
                            if (m_snapToGrid)
                            {
                                hitX = std::round(hitX / 0.25f) * 0.25f;
                                hitZ = std::round(hitZ / 0.25f) * 0.25f;
                            }
                            m_sketchPoints.push_back({hitX, 0.005f, hitZ});
                            rebuildSketchMesh();
                            LOGI(
                                "Sketch tap at (%.1f, %.1f) -> placed point (%.2f, 0.005, "
                                "%.2f) "
                                "[total %zu pts]",
                                x0, y0, hitX, hitZ, m_sketchPoints.size());
                        }
                    }
                }
                else
                {
                    int closestIndex = -1;
                    float minHit = 1e30f;

                    for (size_t i = 0; i < m_objects.size(); ++i)
                    {
                        const auto& obj = m_objects[i];
                        auto model = obj.getModelMatrix();
                        auto invModel = model.inverted();

                        utils::math::Ray localRay;
                        localRay.origin = invModel * ray.origin;
                        localRay.dir = invModel.transformDirection(ray.dir).normalized();

                        float tHit = 0.0f;
                        if (utils::math::Ray::intersectAABB(localRay, obj.aabbMin, obj.aabbMax,
                                                            tHit))
                        {
                            auto hitLocal = localRay.origin + localRay.dir * tHit;
                            auto hitWorld = model * hitLocal;
                            float hitDist = (hitWorld - ray.origin).length();

                            if (hitDist < minHit)
                            {
                                minHit = hitDist;
                                closestIndex = static_cast<int>(i);
                            }
                        }
                    }

                    m_selectedIndex = closestIndex;
                    LOGI("Ray-picking tap at (%.1f, %.1f): selected object %d (%s)", x0, y0,
                         m_selectedIndex,
                         (m_selectedIndex >= 0 ? m_objects[m_selectedIndex].name.c_str()
                                               : "None / Background"));
                }
            }
        }
        m_activePointers = 0;
        m_lastPinchDist = 0.0f;
        m_maxDragDist = 0.0f;
        return;
    }

    if (action == AMOTION_EVENT_ACTION_CANCEL)
    {
        m_activePointers = 0;
        m_lastPinchDist = 0.0f;
        m_maxDragDist = 0.0f;
        return;
    }

    if (action == AMOTION_EVENT_ACTION_POINTER_UP)
    {
        m_activePointers = std::max(0, pointerCount - 1);
        m_lastTouchX = x0;
        m_lastTouchY = y0;
        m_lastPinchDist = 0.0f;
        return;
    }

    if (action == AMOTION_EVENT_ACTION_MOVE)
    {
        if (pointerCount == 1)
        {
            float drag = std::hypot(x0 - m_downX, y0 - m_downY);
            m_maxDragDist = std::max(m_maxDragDist, drag);

            float dx = x0 - m_lastTouchX;
            float dy = y0 - m_lastTouchY;
            m_camera.orbit(dx, dy);
            m_lastTouchX = x0;
            m_lastTouchY = y0;
        }
        else if (pointerCount >= 2)
        {
            m_maxDragDist = 999.0f;  // Multi-touch gestures are never single-tap selections
            float dist = std::hypot(x1 - x0, y1 - y0);
            float midX = (x0 + x1) * 0.5f;
            float midY = (y0 + y1) * 0.5f;

            if (m_lastPinchDist > 10.0f && dist > 10.0f)
            {
                m_camera.zoom(m_lastPinchDist / dist);
                m_camera.pan(midX - m_lastMidX, midY - m_lastMidY,
                             static_cast<float>(getSwapchainExtentH()));
            }

            m_lastPinchDist = dist;
            m_lastMidX = midX;
            m_lastMidY = midY;
            m_lastTouchX = x0;
            m_lastTouchY = y0;
        }
    }
}

bool CADApp::drawFrame()
{
    if (!m_vkContext || !m_swapchain || !m_pipeline || !m_linePipeline || !m_pipelineLayout ||
        !m_renderPass)
    {
        LOGE(
            "FATAL: drawFrame #%u — null resource (vkCtx=%p swap=%p pipe=%p linePipe=%p "
            "layout=%p "
            "rp=%p)",
            m_drawFrameCount + 1, (void*)m_vkContext.get(), (void*)m_swapchain.get(),
            (void*)m_pipeline, (void*)m_linePipeline, (void*)m_pipelineLayout, (void*)m_renderPass);
        return false;
    }

    auto device = m_vkContext->getDevice();

    if (m_vsyncChanged)
    {
        LOGI("VSync mode changed to %s — recreating swapchain",
             m_vsync ? "120Hz FIFO" : "Uncapped Mailbox");
        vkDeviceWaitIdle(device);
        cleanupSwapchain();
        m_swapchain.reset();
        initSwapchain();
        initFramebuffers();
        m_vsyncChanged = false;
    }

    uint32_t frame = m_currentFrame % m_vkContext->getMaxFrames();

    VkFence fence = m_vkContext->getFence(frame);
    vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX);
    vkResetFences(device, 1, &fence);

    VkSemaphore waitSem = m_vkContext->getWaitSemaphore(frame);
    VkSemaphore signalSem = m_vkContext->getSignalSemaphore(frame);
    VkCommandBuffer cmdBuf = m_vkContext->getCmdBuffer(frame);

    uint32_t imageIndex = 0;
    auto result = vkAcquireNextImageKHR(device, m_swapchain->getSwapchain(), UINT64_MAX, waitSem,
                                        VK_NULL_HANDLE, &imageIndex);

    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR)
    {
        LOGI("swapchain out of date on acquire, recreating");
        cleanupSwapchain();
        m_swapchain.reset();
        initSwapchain();
        initFramebuffers();
        return true;  // swapchain recreated; caller will continue on next frame
    }
    else if (result != VK_SUCCESS)
    {
        LOGE("vkAcquireNextImageKHR failed: %d", result);
        m_deviceLost = true;
        return false;
    }

    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplAndroid_NewFrame();
    ImGui::NewFrame();

    renderImGui();

    ImGui::Render();

    vkResetCommandBuffer(cmdBuf, 0);
    recordCommandBuffer(cmdBuf, imageIndex);

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

    try
    {
        VkResult submitResult =
            vkQueueSubmit(m_vkContext->getGraphicsQueue(), 1, &submitInfo, fence);
        if (submitResult == VK_ERROR_DEVICE_LOST)
        {
            LOGE("drawFrame: vkQueueSubmit VK_ERROR_DEVICE_LOST — GPU was lost, exiting");
            m_deviceLost = true;
            return false;
        }
        if (submitResult != VK_SUCCESS)
        {
            LOGE("FATAL: vkQueueSubmit FAILED (result=%d)", submitResult);
            m_deviceLost = true;
            return false;
        }

        VkPresentInfoKHR presentInfo{};
        presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        presentInfo.waitSemaphoreCount = 1;
        presentInfo.pWaitSemaphores = &signalSem;
        VkSwapchainKHR swapchains[] = {m_swapchain->getSwapchain()};
        presentInfo.swapchainCount = 1;
        presentInfo.pSwapchains = swapchains;
        presentInfo.pImageIndices = &imageIndex;

        result = vkQueuePresentKHR(m_vkContext->getPresentQueue(), &presentInfo);
        if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR)
        {
            LOGI("swapchain out of date on present, recreating (image=%u)", imageIndex);
            cleanupSwapchain();
            m_swapchain.reset();
            initSwapchain();
            initFramebuffers();
            return true;  // caller continues to next drawFrame
        }
        else if (result != VK_SUCCESS)
        {
            LOGE("vkQueuePresentKHR failed: %d", result);
            m_deviceLost = true;
            return false;
        }
    }
    catch (const std::exception& e)
    {
        LOGE("EXCEPTION in drawFrame: %s", e.what());
        return false;
    }
    catch (...)
    {
        LOGE("UNKNOWN EXCEPTION in drawFrame");
        return false;
    }

    m_drawFrameCount++;
    m_currentFrame = (m_currentFrame + 1) % m_vkContext->getMaxFrames();
    return true;
}

void CADApp::recordCommandBuffer(VkCommandBuffer commandBuffer, uint32_t imageIndex)
{
    if (!m_renderPass || !m_swapchainFramebuffers[imageIndex] || !m_swapchain || !m_pipeline ||
        !m_linePipeline || !m_pipelineLayout)
    {
        LOGE(
            "recordCommandBuffer: null resource — rp=%p fb=%p swap=%p pipe=%p linePipe=%p "
            "layout=%p",
            (void*)m_renderPass, (void*)m_swapchainFramebuffers[imageIndex],
            (void*)m_swapchain.get(), (void*)m_pipeline, (void*)m_linePipeline,
            (void*)m_pipelineLayout);
        throw std::runtime_error("recordCommandBuffer: null pipeline resource");
    }

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = 0;
    beginInfo.pInheritanceInfo = nullptr;

    if (vkBeginCommandBuffer(commandBuffer, &beginInfo) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to begin recording command buffer");
    }

    VkRenderPassBeginInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass = m_renderPass;
    renderPassInfo.framebuffer = m_swapchainFramebuffers[imageIndex];
    renderPassInfo.renderArea.offset = {0, 0};
    renderPassInfo.renderArea.extent = m_swapchain->getExtent();

    std::array<VkClearValue, 2> clearValues{};
    clearValues[0].color = {{0.08f, 0.15f, 0.28f, 1.0f}};
    clearValues[1].depthStencil = {1.0f, 0};
    renderPassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
    renderPassInfo.pClearValues = clearValues.data();

    vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

    VkExtent2D extent = m_swapchain->getExtent();
    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(extent.width);
    viewport.height = static_cast<float>(extent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(commandBuffer, 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = extent;
    vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

    float aspect = static_cast<float>(extent.width) / static_cast<float>(extent.height);
    utils::math::Mat4f viewProj = m_camera.getViewProjectionMatrix(aspect);

    // 1. Draw CAD Reference Grid and XYZ Origin Triad (LINE_LIST)
    if (m_showGrid && m_gridMesh && m_gridMesh->getVertexCount() > 0)
    {
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_linePipeline);
        VkBuffer vertexBuffers[] = {m_gridMesh->getVertexBuffer()};
        VkDeviceSize offsets[] = {0};
        vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);
        PushConstantData pcData{viewProj, utils::math::Mat4f::identity()};
        vkCmdPushConstants(commandBuffer, m_pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0,
                           sizeof(PushConstantData), &pcData);
        vkCmdDraw(commandBuffer, m_gridMesh->getVertexCount(), 1, 0, 0);
    }

    // 2. Draw 2D Sketch Profile Lines (LINE_LIST)
    if (m_sketchLineMesh && m_sketchLineMesh->getVertexCount() > 0)
    {
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_linePipeline);
        VkBuffer vertexBuffers[] = {m_sketchLineMesh->getVertexBuffer()};
        VkDeviceSize offsets[] = {0};
        vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);
        PushConstantData pcData{viewProj, utils::math::Mat4f::identity()};
        vkCmdPushConstants(commandBuffer, m_pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0,
                           sizeof(PushConstantData), &pcData);
        vkCmdDraw(commandBuffer, m_sketchLineMesh->getVertexCount(), 1, 0, 0);
    }

    // 3. Draw 3D Solid CAD Parts (TRIANGLE_LIST)
    if (m_showSolid)
    {
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline);
        for (const auto& obj : m_objects)
        {
            if (!obj.solidMesh || obj.solidMesh->getVertexCount() == 0)
                continue;

            auto model = obj.getModelMatrix();
            auto normalMatrix = model.inverted().transposed();
            PushConstantData pcData{viewProj * model, normalMatrix};
            VkBuffer vertexBuffers[] = {obj.solidMesh->getVertexBuffer()};
            VkDeviceSize offsets[] = {0};
            vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);

            if (obj.solidMesh->hasIndices())
            {
                vkCmdBindIndexBuffer(commandBuffer, obj.solidMesh->getIndexBuffer(), 0,
                                     VK_INDEX_TYPE_UINT32);
                vkCmdPushConstants(commandBuffer, m_pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0,
                                   sizeof(PushConstantData), &pcData);
                vkCmdDrawIndexed(commandBuffer, obj.solidMesh->getIndexCount(), 1, 0, 0, 0);
            }
            else
            {
                vkCmdPushConstants(commandBuffer, m_pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0,
                                   sizeof(PushConstantData), &pcData);
                vkCmdDraw(commandBuffer, obj.solidMesh->getVertexCount(), 1, 0, 0);
            }
        }
    }

    // 4. Draw Wireframe Feature Lines & Selected Part Highlighting (LINE_LIST)
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_linePipeline);
    for (size_t i = 0; i < m_objects.size(); ++i)
    {
        const auto& obj = m_objects[i];
        bool isSelected = (static_cast<int>(i) == m_selectedIndex);

        if (!m_showWireframe && !isSelected)
            continue;

        auto* meshToDraw = (isSelected && obj.selectedWireframeMesh)
                               ? obj.selectedWireframeMesh.get()
                               : obj.wireframeMesh.get();

        if (!meshToDraw || meshToDraw->getVertexCount() == 0)
            continue;

        auto model = obj.getModelMatrix();
        auto normalMatrix = model.inverted().transposed();
        PushConstantData pcData{viewProj * model, normalMatrix};
        VkBuffer vertexBuffers[] = {meshToDraw->getVertexBuffer()};
        VkDeviceSize offsets[] = {0};
        vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);
        vkCmdPushConstants(commandBuffer, m_pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0,
                           sizeof(PushConstantData), &pcData);
        vkCmdDraw(commandBuffer, meshToDraw->getVertexCount(), 1, 0, 0);
    }

    // 4. Render Dear ImGui Viewport Overlay
    ImDrawData* drawData = ImGui::GetDrawData();
    if (drawData)
    {
        ImGui_ImplVulkan_RenderDrawData(drawData, commandBuffer);
    }

    vkCmdEndRenderPass(commandBuffer);

    if (vkEndCommandBuffer(commandBuffer) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to record command buffer");
    }
}

}  // namespace app
