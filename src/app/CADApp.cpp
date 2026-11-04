#include "app/CADApp.h"

#include <android/input.h>
#include <android/log.h>

#include <array>
#include <cmath>
#include <stdexcept>

#include "imgui.h"
#include "imgui_impl_android.h"
#include "imgui_impl_vulkan.h"
#include "io/MeshImporter.h"
#include "io/SceneSerializer.h"
#include "utils/math/Mat4.h"

#define LOG_TAG "MobileCAD"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

namespace app
{

static SurfaceMaterial getPresetMaterial(int preset, const utils::math::Vec3f& currentColor)
{
    SurfaceMaterial mat;
    mat.preset = preset;
    mat.color = currentColor;
    switch (preset)
    {
    case 0:  // Default (Multi-Face / Vertex Colors)
        mat.metallic = 0.15f;
        mat.roughness = 0.50f;
        break;
    case 1:  // Brushed Aluminum
        mat.color = {0.83f, 0.85f, 0.88f};
        mat.metallic = 0.85f;
        mat.roughness = 0.35f;
        break;
    case 2:  // Polished Brass / Gold
        mat.color = {0.95f, 0.78f, 0.35f};
        mat.metallic = 0.90f;
        mat.roughness = 0.20f;
        break;
    case 3:  // Anodized Blue
        mat.color = {0.15f, 0.45f, 0.90f};
        mat.metallic = 0.70f;
        mat.roughness = 0.30f;
        break;
    case 4:  // Stainless Steel
        mat.color = {0.75f, 0.76f, 0.78f};
        mat.metallic = 0.92f;
        mat.roughness = 0.25f;
        break;
    case 5:  // Matte Red ABS
        mat.color = {0.88f, 0.18f, 0.18f};
        mat.metallic = 0.05f;
        mat.roughness = 0.70f;
        break;
    case 6:  // Polished Copper
        mat.color = {0.92f, 0.55f, 0.40f};
        mat.metallic = 0.95f;
        mat.roughness = 0.18f;
        break;
    case 7:  // Titanium Gunmetal
        mat.color = {0.45f, 0.48f, 0.52f};
        mat.metallic = 0.80f;
        mat.roughness = 0.38f;
        break;
    case 8:  // Custom Color
    default:
        break;
    }
    return mat;
}

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
    utils::math::Mat4f mvp{};
    float normalRow0[4]{1.0f, 0.0f, 0.0f, 1.0f};
    float normalRow1[4]{0.0f, 1.0f, 0.0f, 1.0f};
    float normalRow2[4]{0.0f, 0.0f, 1.0f, 1.0f};
    float pbrParams[4]{0.0f, 1.0f, 0.0f, 1.0f};
};
static_assert(sizeof(PushConstantData) == 128, "PushConstantData must be 128 bytes");

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

    // 1. Create solid triangle pipeline (for opaque +ve 3D solid CAD parts)
    if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_pipeline) !=
        VK_SUCCESS)
    {
        throw std::runtime_error("Failed to create solid graphics pipeline");
    }

    // 2. Create alpha-blended ghost triangle pipeline (for transparent -ve cutters)
    colorBlendAttachment.blendEnable = VK_TRUE;
    colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
    colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_FALSE;

    if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr,
                                  &m_ghostPipeline) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to create ghost graphics pipeline");
    }

    // Reset color blend & depth write for line pipeline
    colorBlendAttachment.blendEnable = VK_FALSE;
    depthStencil.depthWriteEnable = VK_TRUE;

    // 3. Create line pipeline (for CAD reference grid, XYZ triad, and feature wireframes)
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

    // 2. Initial multi-object scene setup: create Part_1
    m_objects.clear();
    SceneObject initialObj;
    initialObj.name = "Part_1";
    initialObj.primitiveType = PrimitiveType::Box;
    initialObj.isSelected = true;
    initialObj.isNegative = false;
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
            obj.localManifold = manifold::Manifold::Cube(
                                    {obj.params.width, obj.params.height, obj.params.depth}, true)
                                    .Translate({0.0, obj.params.height * 0.5, 0.0});
            break;
        case PrimitiveType::Cylinder:
            data = geometry::Primitives::createCylinder(obj.params.radius, obj.params.height,
                                                        static_cast<uint32_t>(obj.params.segments));
            obj.localManifold =
                manifold::Manifold::Cylinder(obj.params.height, obj.params.radius,
                                             obj.params.radius, obj.params.segments, false)
                    .Rotate(-90.0, 0.0, 0.0);
            break;
        case PrimitiveType::Sphere:
            data = geometry::Primitives::createSphere(
                obj.params.radius, static_cast<uint32_t>(obj.params.segments * 3 / 4),
                static_cast<uint32_t>(obj.params.segments));
            obj.localManifold = manifold::Manifold::Sphere(obj.params.radius, obj.params.segments)
                                    .Translate({0.0, obj.params.radius, 0.0});
            break;
        case PrimitiveType::Extrude:
            data = geometry::Primitives::createExtrudedPolygon(
                obj.sketchPoints, obj.params.height, obj.params.bevelWidth,
                obj.params.bevelSegments, &obj.localManifold);
            if (obj.localManifold.IsEmpty() && obj.sketchPoints.size() >= 3)
            {
                manifold::SimplePolygon poly;
                poly.reserve(obj.sketchPoints.size());
                for (const auto& pt : obj.sketchPoints)
                {
                    poly.push_back({pt.x, -pt.z});
                }
                manifold::Polygons polys = {poly};
                obj.localManifold =
                    manifold::Manifold::Extrude(polys, obj.params.height).Rotate(-90.0, 0.0, 0.0);
            }
            break;
        case PrimitiveType::Revolve:
            if (obj.sketchPoints.size() < 3)
            {
                obj.sketchPoints = {{-0.4f, 0.0f, -0.6f},
                                    {0.4f, 0.0f, -0.6f},
                                    {0.4f, 0.0f, 0.6f},
                                    {-0.4f, 0.0f, 0.6f}};
            }
            data = geometry::Primitives::createRevolvedPolygon(
                obj.sketchPoints, obj.params.revolveAngle, obj.params.revolveSegments,
                obj.params.revolveOffset, &obj.localManifold);
            break;
        }
    }
    else if (obj.params.csgMode == 1)
    {
        data = geometry::Primitives::createCsgDifference(obj.params.width, obj.params.height,
                                                         obj.params.depth, obj.params.holeRadius,
                                                         obj.params.segments);
        manifold::Manifold box =
            manifold::Manifold::Cube({obj.params.width, obj.params.height, obj.params.depth}, true)
                .Translate({0.0, obj.params.height * 0.5, 0.0});
        manifold::Manifold hole =
            manifold::Manifold::Cylinder(obj.params.height * 1.5, obj.params.holeRadius,
                                         obj.params.holeRadius, obj.params.segments, true)
                .Rotate(-90.0, 0.0, 0.0)
                .Translate({0.0, obj.params.height * 0.5, 0.0});
        obj.localManifold = box - hole;
    }
    else if (obj.params.csgMode == 2)
    {
        data = geometry::Primitives::createCsgUnion(obj.params.width, obj.params.height,
                                                    obj.params.depth, obj.params.radius,
                                                    obj.params.segments);
        float baseH = obj.params.height * 0.4f;
        float bossH = obj.params.height * 0.8f;
        manifold::Manifold base =
            manifold::Manifold::Cube({obj.params.width, baseH, obj.params.depth}, true)
                .Translate({0.0, baseH * 0.5, 0.0});
        manifold::Manifold boss =
            manifold::Manifold::Cylinder(bossH, obj.params.radius, obj.params.radius,
                                         obj.params.segments, false)
                .Rotate(-90.0, 0.0, 0.0)
                .Translate({0.0, baseH * 0.5, 0.0});
        obj.localManifold = base + boss;
    }
    else if (obj.params.csgMode == 3)
    {
        data = geometry::Primitives::createCsgIntersection(obj.params.width, obj.params.height,
                                                           obj.params.depth, obj.params.radius,
                                                           obj.params.segments);
        manifold::Manifold box =
            manifold::Manifold::Cube({obj.params.width, obj.params.height, obj.params.depth}, true)
                .Translate({0.0, obj.params.height * 0.5, 0.0});
        manifold::Manifold sphere =
            manifold::Manifold::Sphere(obj.params.radius, obj.params.segments)
                .Translate({0.0, obj.params.height * 0.5, 0.0});
        obj.localManifold = box ^ sphere;
    }
    else if (obj.params.csgMode == 4)
    {
        data = geometry::Primitives::createHollowCylinder(obj.params.radius, obj.params.holeRadius,
                                                          obj.params.height, obj.params.segments);
        manifold::Manifold outer =
            manifold::Manifold::Cylinder(obj.params.height, obj.params.radius, obj.params.radius,
                                         obj.params.segments, false)
                .Rotate(-90.0, 0.0, 0.0);
        manifold::Manifold inner =
            manifold::Manifold::Cylinder(obj.params.height * 1.5, obj.params.holeRadius,
                                         obj.params.holeRadius, obj.params.segments, true)
                .Rotate(-90.0, 0.0, 0.0)
                .Translate({0.0, obj.params.height * 0.5, 0.0});
        obj.localManifold = outer - inner;
    }
    else if (obj.params.csgMode == -1)
    {
        if (obj.localManifold.NumVert() > 0)
        {
            data = geometry::Primitives::fromManifold(obj.localManifold);
        }
        else
        {
            data = {obj.cpuVertices, obj.cpuIndices};
        }
    }

    if (obj.isNegative)
    {
        for (auto& v : data.first)
        {
            v.color[0] = 0.95f;
            v.color[1] = 0.35f;
            v.color[2] = 0.30f;
        }
    }
    else
    {
        if (obj.materialPreset > 0)
        {
            SurfaceMaterial sm = getPresetMaterial(obj.materialPreset, obj.materialColor);
            for (auto& v : data.first)
            {
                v.color[0] = sm.color.x;
                v.color[1] = sm.color.y;
                v.color[2] = sm.color.z;
                v.pbr[0] = sm.metallic;
                v.pbr[1] = sm.roughness;
            }
        }

        for (size_t sid = 0; sid < obj.surfaceMaterials.size(); ++sid)
        {
            const auto& sm = obj.surfaceMaterials[sid];
            if (sm.preset > 0)
            {
                for (auto& v : data.first)
                {
                    if (static_cast<int>(std::round(v.pbr[2])) == static_cast<int>(sid))
                    {
                        v.color[0] = sm.color.x;
                        v.color[1] = sm.color.y;
                        v.color[2] = sm.color.z;
                        v.pbr[0] = sm.metallic;
                        v.pbr[1] = sm.roughness;
                    }
                }
            }
        }
    }

    obj.cpuVertices = data.first;
    obj.cpuIndices = data.second;
    obj.ensureSurfaceMaterials();
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

void CADApp::applySurfaceMaterial(SceneObject& obj, int targetSurfaceId, const SurfaceMaterial& mat)
{
    obj.ensureSurfaceMaterials();

    if (targetSurfaceId < 0)
    {
        obj.materialPreset = mat.preset;
        obj.materialColor = mat.color;
        obj.metallic = mat.metallic;
        obj.roughness = mat.roughness;

        for (auto& sm : obj.surfaceMaterials)
        {
            sm = mat;
        }

        for (auto& v : obj.cpuVertices)
        {
            if (mat.preset != 0)
            {
                v.color[0] = mat.color.x;
                v.color[1] = mat.color.y;
                v.color[2] = mat.color.z;
            }
            v.pbr[0] = mat.metallic;
            v.pbr[1] = mat.roughness;
        }
    }
    else
    {
        if (targetSurfaceId < static_cast<int>(obj.surfaceMaterials.size()))
        {
            obj.surfaceMaterials[targetSurfaceId] = mat;
        }

        for (auto& v : obj.cpuVertices)
        {
            int sid = static_cast<int>(std::round(v.pbr[2]));
            if (sid == targetSurfaceId)
            {
                if (mat.preset != 0)
                {
                    v.color[0] = mat.color.x;
                    v.color[1] = mat.color.y;
                    v.color[2] = mat.color.z;
                }
                v.pbr[0] = mat.metallic;
                v.pbr[1] = mat.roughness;
            }
        }
    }

    if (m_vkContext && !obj.cpuVertices.empty())
    {
        auto device = m_vkContext->getDevice();
        auto physDevice = m_vkContext->getPhysicalDevice();
        vkDeviceWaitIdle(device);
        obj.solidMesh = std::make_unique<rendering::MeshBuffer>(device, physDevice, obj.cpuVertices,
                                                                obj.cpuIndices);
    }
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

    // 2. Line segments connecting consecutive points (spline interpolated for SmoothCurve)
    std::vector<utils::math::Vec3f> renderPts;
    if (m_sketchTool == SketchTool::SmoothCurve && n >= 3)
    {
        renderPts = geometry::Primitives::evaluateClosedCatmullRom(m_sketchPoints, 8);
    }
    else
    {
        renderPts = m_sketchPoints;
    }

    size_t rn = renderPts.size();
    const float cyan[3] = {0.0f, 0.95f, 1.0f};
    for (size_t i = 0; i + 1 < rn; ++i)
    {
        lineVerts.push_back({{renderPts[i].x, 0.006f, renderPts[i].z},
                             {0.0f, 0.0f, 0.0f},
                             {cyan[0], cyan[1], cyan[2]}});
        lineVerts.push_back({{renderPts[i + 1].x, 0.006f, renderPts[i + 1].z},
                             {0.0f, 0.0f, 0.0f},
                             {cyan[0], cyan[1], cyan[2]}});
    }

    // 3. Closing segment in Yellow/Green if >= 3 points
    if (rn >= 3)
    {
        const float yellowGreen[3] = {1.0f, 0.90f, 0.20f};
        lineVerts.push_back({{renderPts[rn - 1].x, 0.006f, renderPts[rn - 1].z},
                             {0.0f, 0.0f, 0.0f},
                             {yellowGreen[0], yellowGreen[1], yellowGreen[2]}});
        lineVerts.push_back({{renderPts[0].x, 0.006f, renderPts[0].z},
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

    pushUndoState();

    SceneObject newObj;
    newObj.name = "Extrude_" + std::to_string(m_objects.size() + 1);
    newObj.primitiveType = PrimitiveType::Extrude;
    newObj.params.height = m_extrudeHeight;
    newObj.params.bevelWidth = m_bevelWidth;
    newObj.params.bevelSegments = m_bevelSegments;

    if (m_sketchTool == SketchTool::SmoothCurve && m_sketchPoints.size() >= 3)
    {
        newObj.sketchPoints = geometry::Primitives::evaluateClosedCatmullRom(m_sketchPoints, 8);
    }
    else
    {
        newObj.sketchPoints = m_sketchPoints;
    }

    newObj.position = {0.0f, 0.0f, 0.0f};
    newObj.rotationDeg = {0.0f, 0.0f, 0.0f};
    newObj.isSelected = true;
    newObj.isNegative = false;

    m_objects.push_back(std::move(newObj));
    m_selectedIndex = static_cast<int>(m_objects.size() - 1);
    rebuildObjectGeometry(static_cast<size_t>(m_selectedIndex));

    m_sketchPoints.clear();
    m_sketchLineMesh.reset();
    m_sketchMode = false;

    LOGI("Extruded sketch profile to 3D Part: %s (height=%.2f, bevelWidth=%.2f, bevelSteps=%d)",
         m_objects[m_selectedIndex].name.c_str(), m_extrudeHeight, m_bevelWidth, m_bevelSegments);
}

void CADApp::revolveSketchToPart()
{
    if (m_sketchPoints.size() < 3 || !m_vkContext)
        return;

    pushUndoState();

    SceneObject newObj;
    newObj.name = "Revolve_" + std::to_string(m_objects.size() + 1);
    newObj.primitiveType = PrimitiveType::Revolve;
    newObj.params.revolveAngle = m_revolveAngle;
    newObj.params.revolveSegments = m_revolveSegments;
    newObj.params.revolveOffset = m_revolveOffset;

    if (m_sketchTool == SketchTool::SmoothCurve && m_sketchPoints.size() >= 3)
    {
        newObj.sketchPoints = geometry::Primitives::evaluateClosedCatmullRom(m_sketchPoints, 8);
    }
    else
    {
        newObj.sketchPoints = m_sketchPoints;
    }

    newObj.position = {0.0f, 0.0f, 0.0f};
    newObj.rotationDeg = {0.0f, 0.0f, 0.0f};
    newObj.isSelected = true;
    newObj.isNegative = false;

    m_objects.push_back(std::move(newObj));
    m_selectedIndex = static_cast<int>(m_objects.size() - 1);
    rebuildObjectGeometry(static_cast<size_t>(m_selectedIndex));

    m_sketchPoints.clear();
    m_sketchLineMesh.reset();
    m_sketchMode = false;

    LOGI("Revolved sketch profile to 3D Part: %s (angle=%.1f deg, segs=%d, offset=%.2f)",
         m_objects[m_selectedIndex].name.c_str(), m_revolveAngle, m_revolveSegments,
         m_revolveOffset);
}

void CADApp::setPrimitive(PrimitiveType type)
{
    if (m_selectedIndex >= 0 && m_selectedIndex < static_cast<int>(m_objects.size()))
    {
        pushUndoState();
        m_objects[m_selectedIndex].params.csgMode = 0;
        m_objects[m_selectedIndex].primitiveType = type;
        rebuildObjectGeometry(static_cast<size_t>(m_selectedIndex));
    }
}

void CADApp::applyCsgSubtract()
{
    if (m_objects.empty())
    {
        m_csgStatus = "Subtract: Scene is empty";
        return;
    }

    // 1. Identify selected +ve solids
    std::vector<size_t> posIndices;
    for (size_t i = 0; i < m_objects.size(); ++i)
    {
        if (!m_objects[i].isNegative &&
            (m_objects[i].isSelected || static_cast<int>(i) == m_selectedIndex))
        {
            posIndices.push_back(i);
        }
    }
    if (posIndices.empty())
    {
        for (size_t i = 0; i < m_objects.size(); ++i)
        {
            if (!m_objects[i].isNegative)
            {
                posIndices.push_back(i);
                break;
            }
        }
    }

    // 2. Identify cutter -ve objects
    std::vector<size_t> negIndices;
    for (size_t i = 0; i < m_objects.size(); ++i)
    {
        if (m_objects[i].isNegative && m_objects[i].isSelected)
        {
            negIndices.push_back(i);
        }
    }
    // If no -ve cutter was explicitly checked, treat ALL -ve objects in the scene as cutters
    if (negIndices.empty())
    {
        for (size_t i = 0; i < m_objects.size(); ++i)
        {
            if (m_objects[i].isNegative)
            {
                negIndices.push_back(i);
            }
        }
    }

    if (posIndices.empty())
    {
        m_csgStatus = "Subtract: No +ve solid selected";
        return;
    }
    if (negIndices.empty())
    {
        m_csgStatus = "Subtract: No -ve cutter objects in scene";
        return;
    }

    pushUndoState();

    // 3. Union all participating cutters in world space
    manifold::Manifold combinedCutters;
    for (size_t idx : negIndices)
    {
        manifold::Manifold cutterM = m_objects[idx].getWorldManifold();
        if (combinedCutters.IsEmpty())
        {
            combinedCutters = cutterM;
        }
        else
        {
            combinedCutters += cutterM;
        }
    }

    if (combinedCutters.IsEmpty() || combinedCutters.NumVert() == 0)
    {
        m_csgStatus = "Subtract: Cutters produced empty manifold";
        return;
    }

    auto device = m_vkContext->getDevice();
    auto physDevice = m_vkContext->getPhysicalDevice();
    vkDeviceWaitIdle(device);

    // 4. Subtract combinedCutters from each participating +ve solid
    size_t cutCount = 0;
    for (size_t idx : posIndices)
    {
        auto& posObj = m_objects[idx];
        manifold::Manifold worldM = posObj.getWorldManifold();
        manifold::Manifold cutResult = worldM - combinedCutters;

        if (cutResult.Status() == manifold::Manifold::Error::NoError && !cutResult.IsEmpty())
        {
            posObj.localManifold = cutResult;
            posObj.params.csgMode = -1;  // Baked CSG solid
            posObj.position = {0.0f, 0.0f, 0.0f};
            posObj.rotationDeg = {0.0f, 0.0f, 0.0f};
            posObj.scale = {1.0f, 1.0f, 1.0f};

            auto meshData = geometry::Primitives::fromManifold(cutResult);
            posObj.cpuVertices = std::move(meshData.first);
            posObj.cpuIndices = std::move(meshData.second);
            posObj.ensureSurfaceMaterials();
            posObj.updateAABB();

            auto wireLines =
                geometry::Primitives::createWireframeLines(posObj.cpuVertices, posObj.cpuIndices);
            posObj.solidMesh = std::make_unique<rendering::MeshBuffer>(
                device, physDevice, posObj.cpuVertices, posObj.cpuIndices);
            posObj.wireframeMesh =
                std::make_unique<rendering::MeshBuffer>(device, physDevice, wireLines);

            auto selectedWireLines = wireLines;
            for (auto& v : selectedWireLines)
            {
                v.color[0] = 1.0f;
                v.color[1] = 0.78f;
                v.color[2] = 0.10f;
            }
            posObj.selectedWireframeMesh =
                std::make_unique<rendering::MeshBuffer>(device, physDevice, selectedWireLines);

            cutCount++;
        }
    }

    // 5. Remove consumed cutter objects (descending order to preserve indices)
    std::sort(negIndices.rbegin(), negIndices.rend());
    for (size_t idx : negIndices)
    {
        if (idx < m_objects.size())
        {
            m_objects.erase(m_objects.begin() + idx);
        }
    }

    if (m_objects.empty())
    {
        m_selectedIndex = -1;
    }
    else if (m_selectedIndex >= static_cast<int>(m_objects.size()))
    {
        m_selectedIndex = static_cast<int>(m_objects.size() - 1);
    }

    char buf[128];
    snprintf(buf, sizeof(buf), "Subtracted %zu cutter(s) from %zu solid(s)", negIndices.size(),
             cutCount);
    m_csgStatus = buf;
    LOGI("%s", m_csgStatus.c_str());
}

void CADApp::applyUnionOrGroup()
{
    // Collect selected +ve objects
    std::vector<size_t> posIndices;
    for (size_t i = 0; i < m_objects.size(); ++i)
    {
        if (!m_objects[i].isNegative &&
            (m_objects[i].isSelected || static_cast<int>(i) == m_selectedIndex))
        {
            posIndices.push_back(i);
        }
    }

    if (posIndices.size() < 2)
    {
        m_csgStatus = "Union/Group requires at least 2 selected +ve objects";
        return;
    }

    pushUndoState();

    // Check if any pair of selected objects touch or intersect
    bool anyTouching = false;
    for (size_t a = 0; a < posIndices.size() && !anyTouching; ++a)
    {
        for (size_t b = a + 1; b < posIndices.size(); ++b)
        {
            const auto& objA = m_objects[posIndices[a]];
            const auto& objB = m_objects[posIndices[b]];

            auto modelA = objA.getModelMatrix();
            auto modelB = objB.getModelMatrix();

            auto getMinMax = [](const SceneObject& obj, const utils::math::Mat4f& model,
                                utils::math::Vec3f& outMin, utils::math::Vec3f& outMax)
            {
                outMin = {1e30f, 1e30f, 1e30f};
                outMax = {-1e30f, -1e30f, -1e30f};
                const float corners[8][3] = {{obj.aabbMin.x, obj.aabbMin.y, obj.aabbMin.z},
                                             {obj.aabbMax.x, obj.aabbMin.y, obj.aabbMin.z},
                                             {obj.aabbMin.x, obj.aabbMax.y, obj.aabbMin.z},
                                             {obj.aabbMax.x, obj.aabbMax.y, obj.aabbMin.z},
                                             {obj.aabbMin.x, obj.aabbMin.y, obj.aabbMax.z},
                                             {obj.aabbMax.x, obj.aabbMin.y, obj.aabbMax.z},
                                             {obj.aabbMin.x, obj.aabbMax.y, obj.aabbMax.z},
                                             {obj.aabbMax.x, obj.aabbMax.y, obj.aabbMax.z}};
                for (int c = 0; c < 8; ++c)
                {
                    auto w =
                        model * utils::math::Vec3f(corners[c][0], corners[c][1], corners[c][2]);
                    outMin.x = std::min(outMin.x, w.x);
                    outMin.y = std::min(outMin.y, w.y);
                    outMin.z = std::min(outMin.z, w.z);
                    outMax.x = std::max(outMax.x, w.x);
                    outMax.y = std::max(outMax.y, w.y);
                    outMax.z = std::max(outMax.z, w.z);
                }
            };

            utils::math::Vec3f minA, maxA, minB, maxB;
            getMinMax(objA, modelA, minA, maxA);
            getMinMax(objB, modelB, minB, maxB);

            constexpr float eps = 0.02f;
            bool aabbOverlap = (minA.x - eps <= maxB.x && maxA.x + eps >= minB.x) &&
                               (minA.y - eps <= maxB.y && maxA.y + eps >= minB.y) &&
                               (minA.z - eps <= maxB.z && maxA.z + eps >= minB.z);

            if (aabbOverlap)
            {
                manifold::Manifold mA = objA.getWorldManifold();
                manifold::Manifold mB = objB.getWorldManifold();
                manifold::Manifold isect = mA ^ mB;
                if (!isect.IsEmpty() && isect.NumVert() > 0)
                {
                    anyTouching = true;
                    break;
                }
            }
        }
    }

    auto device = m_vkContext->getDevice();
    auto physDevice = m_vkContext->getPhysicalDevice();
    vkDeviceWaitIdle(device);

    SceneObject newObj;
    newObj.position = {0.0f, 0.0f, 0.0f};
    newObj.rotationDeg = {0.0f, 0.0f, 0.0f};
    newObj.scale = {1.0f, 1.0f, 1.0f};
    newObj.isSelected = true;
    newObj.isNegative = false;
    newObj.params.csgMode = -1;

    size_t count = posIndices.size();

    if (anyTouching)
    {
        // 1. Boolean Union
        newObj.name = "Union_Part_" + std::to_string(m_objects.size() + 1);
        manifold::Manifold combined;
        for (size_t idx : posIndices)
        {
            manifold::Manifold wm = m_objects[idx].getWorldManifold();
            if (combined.IsEmpty())
                combined = wm;
            else
                combined += wm;
        }
        newObj.localManifold = combined;

        auto meshData = geometry::Primitives::fromManifold(combined);
        newObj.cpuVertices = std::move(meshData.first);
        newObj.cpuIndices = std::move(meshData.second);
        newObj.ensureSurfaceMaterials();
        newObj.updateAABB();

        auto wireLines =
            geometry::Primitives::createWireframeLines(newObj.cpuVertices, newObj.cpuIndices);
        newObj.solidMesh = std::make_unique<rendering::MeshBuffer>(
            device, physDevice, newObj.cpuVertices, newObj.cpuIndices);
        newObj.wireframeMesh =
            std::make_unique<rendering::MeshBuffer>(device, physDevice, wireLines);

        auto selectedWireLines = wireLines;
        for (auto& v : selectedWireLines)
        {
            v.color[0] = 1.0f;
            v.color[1] = 0.78f;
            v.color[2] = 0.10f;
        }
        newObj.selectedWireframeMesh =
            std::make_unique<rendering::MeshBuffer>(device, physDevice, selectedWireLines);

        char buf[128];
        snprintf(buf, sizeof(buf), "Boolean Union: fused %zu touching parts", count);
        m_csgStatus = buf;
    }
    else
    {
        // 2. Disjoint Grouping
        newObj.name = "Group_Part_" + std::to_string(m_objects.size() + 1);
        manifold::Manifold combined;
        for (size_t idx : posIndices)
        {
            manifold::Manifold wm = m_objects[idx].getWorldManifold();
            if (combined.IsEmpty())
                combined = wm;
            else
                combined += wm;
        }
        newObj.localManifold = combined;

        for (size_t idx : posIndices)
        {
            const auto& src = m_objects[idx];
            auto model = src.getModelMatrix();
            auto normalMat = model.inverted().transposed();
            uint32_t baseIdx = static_cast<uint32_t>(newObj.cpuVertices.size());

            for (const auto& v : src.cpuVertices)
            {
                auto wPos = model * utils::math::Vec3f(v.position[0], v.position[1], v.position[2]);
                auto wNorm = normalMat
                                 .transformDirection(
                                     utils::math::Vec3f(v.normal[0], v.normal[1], v.normal[2]))
                                 .normalized();
                newObj.cpuVertices.push_back({{wPos.x, wPos.y, wPos.z},
                                              {wNorm.x, wNorm.y, wNorm.z},
                                              {v.color[0], v.color[1], v.color[2]}});
            }
            for (uint32_t iv : src.cpuIndices)
            {
                newObj.cpuIndices.push_back(baseIdx + iv);
            }
        }
        newObj.updateAABB();

        newObj.solidMesh = std::make_unique<rendering::MeshBuffer>(
            device, physDevice, newObj.cpuVertices, newObj.cpuIndices);
        auto wireLines =
            geometry::Primitives::createWireframeLines(newObj.cpuVertices, newObj.cpuIndices);
        newObj.wireframeMesh =
            std::make_unique<rendering::MeshBuffer>(device, physDevice, wireLines);

        auto selectedWireLines = wireLines;
        for (auto& v : selectedWireLines)
        {
            v.color[0] = 1.0f;
            v.color[1] = 0.78f;
            v.color[2] = 0.10f;
        }
        newObj.selectedWireframeMesh =
            std::make_unique<rendering::MeshBuffer>(device, physDevice, selectedWireLines);

        char buf[128];
        snprintf(buf, sizeof(buf), "Parts do not touch — grouped %zu parts together", count);
        m_csgStatus = buf;
    }

    // Remove the grouped objects (in reverse order)
    std::sort(posIndices.rbegin(), posIndices.rend());
    for (size_t idx : posIndices)
    {
        if (idx < m_objects.size())
        {
            m_objects.erase(m_objects.begin() + idx);
        }
    }

    m_objects.push_back(std::move(newObj));
    m_selectedIndex = static_cast<int>(m_objects.size() - 1);
    LOGI("%s", m_csgStatus.c_str());
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

bool CADApp::exportOBJ()
{
    if (m_objects.empty())
    {
        m_exportStatus = "Export error: No objects in scene";
        return false;
    }

    std::vector<io::MeshExporter::OBJMeshPart> parts;
    parts.reserve(m_objects.size());

    for (size_t i = 0; i < m_objects.size(); ++i)
    {
        const auto& obj = m_objects[i];
        if (obj.cpuVertices.empty() || obj.cpuIndices.size() < 3)
            continue;

        io::MeshExporter::OBJMeshPart part;
        part.name = obj.name.empty() ? ("Part_" + std::to_string(i + 1)) : obj.name;
        part.indices = obj.cpuIndices;
        part.vertices.reserve(obj.cpuVertices.size());

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

            part.vertices.push_back(tv);
        }

        parts.push_back(std::move(part));
    }

    if (parts.empty())
    {
        m_exportStatus = "Export error: Empty assembly";
        return false;
    }

    const std::string downloadPath = "/sdcard/Download/mobilecad_part.obj";
    const std::string internalPath = "/data/data/com.mobilecad/files/part_export.obj";

    std::remove(downloadPath.c_str());
    bool ok = io::MeshExporter::exportWavefrontOBJ(downloadPath, parts);
    if (ok)
    {
        char buf[256];
        snprintf(buf, sizeof(buf), "Exported %zu OBJ parts -> %s", parts.size(),
                 downloadPath.c_str());
        m_exportStatus = buf;
        LOGI("%s", buf);
        return true;
    }

    LOGI("Primary OBJ export to %s failed, falling back to internal storage: %s",
         downloadPath.c_str(), internalPath.c_str());

    std::remove(internalPath.c_str());
    ok = io::MeshExporter::exportWavefrontOBJ(internalPath, parts);
    if (ok)
    {
        char buf[256];
        snprintf(buf, sizeof(buf), "Exported %zu OBJ parts -> %s", parts.size(),
                 internalPath.c_str());
        m_exportStatus = buf;
        LOGI("%s", buf);
        return true;
    }

    m_exportStatus = "OBJ export failed (check permissions)";
    LOGE("Failed to export OBJ to both %s and %s", downloadPath.c_str(), internalPath.c_str());
    return false;
}

bool CADApp::saveWorkspace()
{
    if (m_objects.empty())
    {
        m_projectStatus = "Save error: Scene is empty";
        return false;
    }

    const std::string downloadPath = "/sdcard/Download/mobilecad_workspace.mcad";
    const std::string internalPath = "/data/data/com.mobilecad/files/workspace.mcad";

    std::remove(downloadPath.c_str());
    bool ok = io::SceneSerializer::saveWorkspace(downloadPath, m_objects);
    if (ok)
    {
        char buf[256];
        snprintf(buf, sizeof(buf), "Saved %zu parts -> %s", m_objects.size(), downloadPath.c_str());
        m_projectStatus = buf;
        LOGI("%s", buf);
        return true;
    }

    LOGI("Primary save to %s failed, falling back to internal storage: %s", downloadPath.c_str(),
         internalPath.c_str());

    std::remove(internalPath.c_str());
    ok = io::SceneSerializer::saveWorkspace(internalPath, m_objects);
    if (ok)
    {
        char buf[256];
        snprintf(buf, sizeof(buf), "Saved %zu parts -> %s", m_objects.size(), internalPath.c_str());
        m_projectStatus = buf;
        LOGI("%s", buf);
        return true;
    }

    m_projectStatus = "Save failed (check permissions)";
    LOGE("Failed to save workspace to both %s and %s", downloadPath.c_str(), internalPath.c_str());
    return false;
}

bool CADApp::loadWorkspace()
{
    if (!m_vkContext)
        return false;

    const std::string downloadPath = "/sdcard/Download/mobilecad_workspace.mcad";
    const std::string internalPath = "/data/data/com.mobilecad/files/workspace.mcad";

    std::vector<SceneObject> loadedObjects;
    bool ok = io::SceneSerializer::loadWorkspace(downloadPath, loadedObjects);
    std::string loadedFrom = downloadPath;
    if (!ok)
    {
        LOGI("Primary load from %s failed, attempting fallback: %s", downloadPath.c_str(),
             internalPath.c_str());
        ok = io::SceneSerializer::loadWorkspace(internalPath, loadedObjects);
        loadedFrom = internalPath;
    }

    if (!ok)
    {
        m_projectStatus = "Load failed: Cannot find .mcad file";
        LOGE("Failed to load workspace from both %s and %s", downloadPath.c_str(),
             internalPath.c_str());
        return false;
    }

    auto device = m_vkContext->getDevice();
    vkDeviceWaitIdle(device);

    m_objects = std::move(loadedObjects);
    m_selectedIndex = m_objects.empty() ? -1 : 0;

    for (size_t i = 0; i < m_objects.size(); ++i)
    {
        rebuildObjectGeometry(i);
    }

    char buf[256];
    snprintf(buf, sizeof(buf), "Loaded %zu parts <- %s", m_objects.size(), loadedFrom.c_str());
    m_projectStatus = buf;
    LOGI("%s", buf);
    return true;
}

bool CADApp::importMeshFile(const std::string& filepath)
{
    if (!m_vkContext)
        return false;

    if (!std::filesystem::exists(filepath))
    {
        m_importStatus = "Import failed: File not found";
        LOGE("importMeshFile: file not found: %s", filepath.c_str());
        return false;
    }

    std::vector<rendering::Vertex> vertices;
    std::vector<uint32_t> indices;

    bool ok = io::MeshImporter::importMesh(filepath, vertices, indices);
    if (!ok || vertices.empty() || indices.size() < 3)
    {
        m_importStatus = "Import failed: Invalid or empty mesh";
        LOGE("importMeshFile: failed to parse %s", filepath.c_str());
        return false;
    }

    // Auto-Centering and Millimeter Auto-Scaling
    float minX = 1e9f, maxX = -1e9f;
    float minY = 1e9f, maxY = -1e9f;
    float minZ = 1e9f, maxZ = -1e9f;

    for (const auto& v : vertices)
    {
        minX = std::min(minX, v.position[0]);
        maxX = std::max(maxX, v.position[0]);
        minY = std::min(minY, v.position[1]);
        maxY = std::max(maxY, v.position[1]);
        minZ = std::min(minZ, v.position[2]);
        maxZ = std::max(maxZ, v.position[2]);
    }

    float dimX = maxX - minX;
    float dimY = maxY - minY;
    float dimZ = maxZ - minZ;
    float maxDim = std::max({dimX, dimY, dimZ});

    float scaleFactor = 1.0f;
    if (maxDim > 15.0f)
    {
        scaleFactor = 3.0f / maxDim;
        LOGI("importMeshFile: auto-scaling millimeter model (maxDim=%.1f, scaleFactor=%.4f)",
             maxDim, scaleFactor);
    }

    float centerX = (minX + maxX) * 0.5f;
    float centerZ = (minZ + maxZ) * 0.5f;
    float baseY = minY;

    for (auto& v : vertices)
    {
        v.position[0] = (v.position[0] - centerX) * scaleFactor;
        v.position[1] = (v.position[1] - baseY) * scaleFactor;
        v.position[2] = (v.position[2] - centerZ) * scaleFactor;
    }

    geometry::Primitives::assignSurfaceClusters(vertices, indices);

    pushUndoState();

    for (auto& obj : m_objects)
    {
        obj.isSelected = false;
    }

    SceneObject newObj;
    std::string stem = std::filesystem::path(filepath).stem().string();
    if (stem.empty())
        stem = "Imported_" + std::to_string(m_objects.size() + 1);
    newObj.name = stem;
    newObj.primitiveType = PrimitiveType::Box;
    newObj.params.csgMode = -1;  // Baked / Custom mesh
    newObj.position = {0.0f, 0.0f, 0.0f};
    newObj.rotationDeg = {0.0f, 0.0f, 0.0f};
    newObj.scale = {1.0f, 1.0f, 1.0f};
    newObj.isSelected = true;
    newObj.isNegative = false;
    newObj.materialPreset = 0;
    newObj.materialColor = {0.75f, 0.78f, 0.82f};
    newObj.metallic = 0.35f;
    newObj.roughness = 0.40f;

    newObj.cpuVertices = std::move(vertices);
    newObj.cpuIndices = std::move(indices);
    newObj.ensureSurfaceMaterials();
    newObj.updateAABB();

    // Watertight manifold building for CSG & Caliper compatibility
    newObj.localManifold = newObj.buildManifoldFromMesh();

    auto device = m_vkContext->getDevice();
    auto physDevice = m_vkContext->getPhysicalDevice();

    newObj.solidMesh = std::make_unique<rendering::MeshBuffer>(
        device, physDevice, newObj.cpuVertices, newObj.cpuIndices);
    auto wireLines =
        geometry::Primitives::createWireframeLines(newObj.cpuVertices, newObj.cpuIndices);
    newObj.wireframeMesh = std::make_unique<rendering::MeshBuffer>(device, physDevice, wireLines);

    auto selectedWireLines = wireLines;
    for (auto& v : selectedWireLines)
    {
        v.color[0] = 1.0f;
        v.color[1] = 0.78f;
        v.color[2] = 0.10f;
    }
    newObj.selectedWireframeMesh =
        std::make_unique<rendering::MeshBuffer>(device, physDevice, selectedWireLines);

    m_objects.push_back(std::move(newObj));
    m_selectedIndex = static_cast<int>(m_objects.size() - 1);

    uint32_t numTris = static_cast<uint32_t>(m_objects.back().cpuIndices.size() / 3);
    char outBuf[256];
    snprintf(outBuf, sizeof(outBuf), "Imported %s (%u tris)", stem.c_str(), numTris);
    m_importStatus = outBuf;
    LOGI("%s", outBuf);
    return true;
}

void CADApp::refreshDiscoveredFiles()
{
    m_discoveredMeshFiles.clear();
    const std::vector<std::string> scanDirs = {"/sdcard/Download",
                                               "/data/data/com.mobilecad/files"};

    for (const auto& dirPath : scanDirs)
    {
        try
        {
            if (std::filesystem::exists(dirPath))
            {
                for (const auto& entry : std::filesystem::directory_iterator(dirPath))
                {
                    if (entry.is_regular_file())
                    {
                        std::string ext = entry.path().extension().string();
                        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
                        if (ext == ".stl" || ext == ".obj")
                        {
                            m_discoveredMeshFiles.push_back(entry.path().string());
                        }
                    }
                }
            }
        }
        catch (const std::exception& e)
        {
            LOGE("refreshDiscoveredFiles: error scanning %s: %s", dirPath.c_str(), e.what());
        }
    }

    std::sort(m_discoveredMeshFiles.begin(), m_discoveredMeshFiles.end());
    m_selectedMeshFileIdx = 0;

    if (!m_discoveredMeshFiles.empty())
    {
        m_importStatus = "Found " + std::to_string(m_discoveredMeshFiles.size()) + " mesh file(s)";
    }
    else
    {
        m_importStatus = "No .stl or .obj files found in /sdcard/Download/";
    }
    LOGI("%s", m_importStatus.c_str());
}

HistoryState CADApp::captureCurrentState() const
{
    HistoryState state;
    state.selectedIndex = m_selectedIndex;
    state.objects.reserve(m_objects.size());
    for (const auto& obj : m_objects)
    {
        SceneObjectSnapshot snap;
        snap.name = obj.name;
        snap.primitiveType = obj.primitiveType;
        snap.params = obj.params;
        snap.sketchPoints = obj.sketchPoints;
        snap.position = obj.position;
        snap.rotationDeg = obj.rotationDeg;
        snap.scale = obj.scale;
        snap.isSelected = obj.isSelected;
        snap.isNegative = obj.isNegative;
        snap.materialPreset = obj.materialPreset;
        snap.materialColor = obj.materialColor;
        snap.metallic = obj.metallic;
        snap.roughness = obj.roughness;
        snap.aabbMin = obj.aabbMin;
        snap.aabbMax = obj.aabbMax;
        snap.cpuVertices = obj.cpuVertices;
        snap.cpuIndices = obj.cpuIndices;
        snap.surfaceMaterials = obj.surfaceMaterials;
        snap.selectedSurfaceId = obj.selectedSurfaceId;
        state.objects.push_back(std::move(snap));
    }
    return state;
}

void CADApp::restoreState(const HistoryState& state)
{
    if (!m_vkContext)
        return;

    auto device = m_vkContext->getDevice();
    auto physDevice = m_vkContext->getPhysicalDevice();
    vkDeviceWaitIdle(device);

    m_objects.clear();
    m_objects.reserve(state.objects.size());

    for (const auto& snap : state.objects)
    {
        SceneObject obj;
        obj.name = snap.name;
        obj.primitiveType = snap.primitiveType;
        obj.params = snap.params;
        obj.sketchPoints = snap.sketchPoints;
        obj.position = snap.position;
        obj.rotationDeg = snap.rotationDeg;
        obj.scale = snap.scale;
        obj.isSelected = snap.isSelected;
        obj.isNegative = snap.isNegative;
        obj.materialPreset = snap.materialPreset;
        obj.materialColor = snap.materialColor;
        obj.metallic = snap.metallic;
        obj.roughness = snap.roughness;
        obj.aabbMin = snap.aabbMin;
        obj.aabbMax = snap.aabbMax;
        obj.cpuVertices = snap.cpuVertices;
        obj.cpuIndices = snap.cpuIndices;
        obj.surfaceMaterials = snap.surfaceMaterials;
        obj.selectedSurfaceId = snap.selectedSurfaceId;

        if (!obj.cpuVertices.empty() && obj.cpuIndices.size() >= 3)
        {
            obj.solidMesh = std::make_unique<rendering::MeshBuffer>(
                device, physDevice, obj.cpuVertices, obj.cpuIndices);

            auto wireLines =
                geometry::Primitives::createWireframeLines(obj.cpuVertices, obj.cpuIndices);
            obj.wireframeMesh =
                std::make_unique<rendering::MeshBuffer>(device, physDevice, wireLines);

            auto selectedWireLines = wireLines;
            for (auto& v : selectedWireLines)
            {
                v.color[0] = 1.0f;
                v.color[1] = 0.78f;
                v.color[2] = 0.10f;
            }
            obj.selectedWireframeMesh =
                std::make_unique<rendering::MeshBuffer>(device, physDevice, selectedWireLines);

            obj.localManifold = obj.buildManifoldFromMesh();
        }

        m_objects.push_back(std::move(obj));
    }

    m_selectedIndex = state.selectedIndex;
    if (m_selectedIndex >= static_cast<int>(m_objects.size()))
    {
        m_selectedIndex = m_objects.empty() ? -1 : static_cast<int>(m_objects.size() - 1);
    }
}

void CADApp::pushUndoState()
{
    m_undoStack.push_back(captureCurrentState());
    if (m_undoStack.size() > kMaxHistory)
    {
        m_undoStack.erase(m_undoStack.begin());
    }
    m_redoStack.clear();
}

void CADApp::undo()
{
    if (m_undoStack.empty())
        return;

    m_redoStack.push_back(captureCurrentState());
    HistoryState prev = std::move(m_undoStack.back());
    m_undoStack.pop_back();
    restoreState(prev);
    LOGI("Undo executed. History stack: %zu undo, %zu redo", m_undoStack.size(),
         m_redoStack.size());
}

void CADApp::redo()
{
    if (m_redoStack.empty())
        return;

    m_undoStack.push_back(captureCurrentState());
    HistoryState next = std::move(m_redoStack.back());
    m_redoStack.pop_back();
    restoreState(next);
    LOGI("Redo executed. History stack: %zu undo, %zu redo", m_undoStack.size(),
         m_redoStack.size());
}

void CADApp::renderCaliperOverlay()
{
    if (m_measurePointCount < 1 || !m_swapchain)
        return;

    float screenW = static_cast<float>(getSwapchainExtentW());
    float screenH = static_cast<float>(getSwapchainExtentH());
    if (screenW <= 0.0f || screenH <= 0.0f)
        return;

    float aspect = screenW / screenH;
    auto vp = m_camera.getViewProjectionMatrix(aspect);

    auto projectWorldToScreen = [&](const utils::math::Vec3f& worldPos,
                                    ImVec2& outScreenPos) -> bool
    {
        float w = vp(3, 0) * worldPos.x + vp(3, 1) * worldPos.y + vp(3, 2) * worldPos.z + vp(3, 3);
        if (w <= 0.001f)
            return false;

        float x = vp(0, 0) * worldPos.x + vp(0, 1) * worldPos.y + vp(0, 2) * worldPos.z + vp(0, 3);
        float y = vp(1, 0) * worldPos.x + vp(1, 1) * worldPos.y + vp(1, 2) * worldPos.z + vp(1, 3);

        float ndcX = x / w;
        float ndcY = y / w;

        outScreenPos.x = (ndcX + 1.0f) * 0.5f * screenW;
        outScreenPos.y = (ndcY + 1.0f) * 0.5f * screenH;
        return true;
    };

    ImDrawList* drawList = ImGui::GetBackgroundDrawList();
    if (!drawList)
        return;

    ImVec2 screenA;
    bool visA = projectWorldToScreen(m_measurePtA, screenA);

    if (visA)
    {
        // Draw CAD Magenta Point A marker
        drawList->AddCircleFilled(screenA, 8.0f, IM_COL32(255, 60, 180, 255));
        drawList->AddCircle(screenA, 13.0f, IM_COL32(255, 255, 255, 240), 20, 2.5f);
        drawList->AddCircle(screenA, 17.0f, IM_COL32(255, 60, 180, 120), 20, 1.5f);
        drawList->AddText(ImVec2(screenA.x + 16.0f, screenA.y - 12.0f),
                          IM_COL32(255, 255, 255, 255), "Point A");
    }

    if (m_measurePointCount >= 2)
    {
        ImVec2 screenB;
        bool visB = projectWorldToScreen(m_measurePtB, screenB);

        if (visB)
        {
            // Draw CAD Cyan Point B marker
            drawList->AddCircleFilled(screenB, 8.0f, IM_COL32(40, 220, 255, 255));
            drawList->AddCircle(screenB, 13.0f, IM_COL32(255, 255, 255, 240), 20, 2.5f);
            drawList->AddCircle(screenB, 17.0f, IM_COL32(40, 220, 255, 120), 20, 1.5f);
            drawList->AddText(ImVec2(screenB.x + 16.0f, screenB.y - 12.0f),
                              IM_COL32(255, 255, 255, 255), "Point B");
        }

        if (visA && visB)
        {
            // Draw dimension line
            drawList->AddLine(screenA, screenB, IM_COL32(255, 240, 90, 255), 2.5f);

            // Perpendicular tick marks at ends
            float dx = screenB.x - screenA.x;
            float dy = screenB.y - screenA.y;
            float lineLen = std::hypot(dx, dy);
            if (lineLen > 1e-3f)
            {
                float nx = -dy / lineLen * 9.0f;
                float ny = dx / lineLen * 9.0f;
                drawList->AddLine(ImVec2(screenA.x - nx, screenA.y - ny),
                                  ImVec2(screenA.x + nx, screenA.y + ny),
                                  IM_COL32(255, 255, 255, 255), 2.0f);
                drawList->AddLine(ImVec2(screenB.x - nx, screenB.y - ny),
                                  ImVec2(screenB.x + nx, screenB.y + ny),
                                  IM_COL32(255, 255, 255, 255), 2.0f);
            }

            // High-contrast floating dimension badge at midpoint
            utils::math::Vec3f delta = m_measurePtB - m_measurePtA;
            float dist = delta.length();
            char badgeText[64];
            snprintf(badgeText, sizeof(badgeText), "L = %.3f", dist);

            ImVec2 midScreen((screenA.x + screenB.x) * 0.5f, (screenA.y + screenB.y) * 0.5f);
            ImVec2 txtSize = ImGui::CalcTextSize(badgeText);
            float padX = 8.0f;
            float padY = 5.0f;
            ImVec2 bMin(midScreen.x - txtSize.x * 0.5f - padX,
                        midScreen.y - txtSize.y * 0.5f - padY);
            ImVec2 bMax(midScreen.x + txtSize.x * 0.5f + padX,
                        midScreen.y + txtSize.y * 0.5f + padY);

            drawList->AddRectFilled(bMin, bMax, IM_COL32(18, 22, 30, 235), 6.0f);
            drawList->AddRect(bMin, bMax, IM_COL32(255, 240, 90, 255), 6.0f, 0, 1.8f);
            drawList->AddText(ImVec2(bMin.x + padX, bMin.y + padY), IM_COL32(255, 255, 255, 255),
                              badgeText);
        }
    }
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
                pushUndoState();
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
    if (ImGui::IsItemActivated())
    {
        pushUndoState();
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
    if (ImGui::IsItemActivated())
    {
        pushUndoState();
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
        pushUndoState();
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
            if (ImGui::IsItemActivated())
            {
                pushUndoState();
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
            if (ImGui::IsItemActivated())
            {
                pushUndoState();
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
    renderCaliperOverlay();

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

            // Undo / Redo History Controls
            float historyBtnW = (availW - spacing) * 0.5f;
            std::string undoLabel = "< Undo (" + std::to_string(m_undoStack.size()) + ")";
            std::string redoLabel = "Redo (" + std::to_string(m_redoStack.size()) + ") >";

            if (m_undoStack.empty())
                ImGui::BeginDisabled();
            if (ImGui::Button(undoLabel.c_str(), ImVec2(historyBtnW, 0.0f)))
            {
                undo();
            }
            if (m_undoStack.empty())
                ImGui::EndDisabled();

            ImGui::SameLine();

            if (m_redoStack.empty())
                ImGui::BeginDisabled();
            if (ImGui::Button(redoLabel.c_str(), ImVec2(historyBtnW, 0.0f)))
            {
                redo();
            }
            if (m_redoStack.empty())
                ImGui::EndDisabled();

            // 3. Scene Assembly Section
            if (ImGui::CollapsingHeader("Scene Assembly", ImGuiTreeNodeFlags_DefaultOpen))
            {
                if (ImGui::Button("+ Add Part"))
                {
                    pushUndoState();
                    SceneObject newObj;
                    newObj.name = "Part_" + std::to_string(m_objects.size() + 1);
                    newObj.position = {static_cast<float>(m_objects.size()) * 1.5f, 0.0f, 0.0f};
                    newObj.isSelected = true;
                    newObj.isNegative = false;
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
                        pushUndoState();
                        const auto& src = m_objects[m_selectedIndex];
                        SceneObject dup;
                        dup.name = src.name + "_copy";
                        dup.primitiveType = src.primitiveType;
                        dup.params = src.params;
                        dup.sketchPoints = src.sketchPoints;
                        dup.position = src.position + utils::math::Vec3f(0.8f, 0.0f, 0.8f);
                        dup.rotationDeg = src.rotationDeg;
                        dup.scale = src.scale;
                        dup.isSelected = true;
                        dup.isNegative = src.isNegative;
                        dup.localManifold = src.localManifold;
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
                        pushUndoState();
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

                // Multi-select controls
                ImGui::Checkbox("Multi-Select Tap Mode", &m_multiSelectTapMode);
                ImGui::SameLine();
                if (ImGui::SmallButton("Select All"))
                {
                    for (auto& o : m_objects)
                        o.isSelected = true;
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("Clear"))
                {
                    for (auto& o : m_objects)
                        o.isSelected = false;
                }

                if (!m_objects.empty())
                {
                    ImGui::Spacing();
                    float availW = ImGui::GetContentRegionAvail().x;
                    float roleBtnW = 110.0f;
                    float checkW = 32.0f;
                    float nameBtnW =
                        availW - (roleBtnW + checkW + ImGui::GetStyle().ItemSpacing.x * 2.0f);
                    if (nameBtnW < 120.0f)
                        nameBtnW = 120.0f;

                    for (size_t i = 0; i < m_objects.size(); ++i)
                    {
                        auto& obj = m_objects[i];
                        ImGui::PushID(static_cast<int>(i));

                        // 1. Checkbox for multi-select
                        ImGui::Checkbox("##sel", &obj.isSelected);
                        ImGui::SameLine();

                        // 2. Selectable button for obj.name
                        bool isActive = (static_cast<int>(i) == m_selectedIndex);
                        if (isActive)
                        {
                            ImGui::PushStyleColor(ImGuiCol_Button,
                                                  ImVec4(0.20f, 0.48f, 0.85f, 1.0f));
                        }
                        if (ImGui::Button(obj.name.c_str(), ImVec2(nameBtnW, 0.0f)))
                        {
                            m_selectedIndex = static_cast<int>(i);
                            obj.isSelected = true;
                        }
                        if (isActive)
                        {
                            ImGui::PopStyleColor();
                        }
                        ImGui::SameLine();

                        // 3. Role toggle: [+ve Solid] vs [-ve Cut]
                        if (obj.isNegative)
                        {
                            ImGui::PushStyleColor(ImGuiCol_Button,
                                                  ImVec4(0.80f, 0.25f, 0.20f, 0.95f));
                            ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                                                  ImVec4(0.90f, 0.35f, 0.30f, 1.0f));
                            if (ImGui::Button("-ve Cut", ImVec2(roleBtnW, 0.0f)))
                            {
                                obj.isNegative = false;
                                rebuildObjectGeometry(i);
                            }
                            ImGui::PopStyleColor(2);
                        }
                        else
                        {
                            ImGui::PushStyleColor(ImGuiCol_Button,
                                                  ImVec4(0.18f, 0.55f, 0.35f, 0.95f));
                            ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                                                  ImVec4(0.25f, 0.68f, 0.45f, 1.0f));
                            if (ImGui::Button("+ve Solid", ImVec2(roleBtnW, 0.0f)))
                            {
                                obj.isNegative = true;
                                rebuildObjectGeometry(i);
                            }
                            ImGui::PopStyleColor(2);
                        }

                        ImGui::PopID();
                    }

                    ImGui::Spacing();
                    ImGui::Separator();
                    ImGui::Spacing();

                    // Boolean & Assembly Action Bar
                    float actionBtnW =
                        (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;

                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.70f, 0.20f, 0.25f, 0.95f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                                          ImVec4(0.85f, 0.28f, 0.32f, 1.0f));
                    if (ImGui::Button("Apply Subtract (Cut -ve)", ImVec2(actionBtnW, 46.0f)))
                    {
                        applyCsgSubtract();
                    }
                    ImGui::PopStyleColor(2);

                    ImGui::SameLine();

                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.45f, 0.75f, 0.95f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                                          ImVec4(0.25f, 0.55f, 0.88f, 1.0f));
                    if (ImGui::Button("Union / Group (+ve)", ImVec2(actionBtnW, 46.0f)))
                    {
                        applyUnionOrGroup();
                    }
                    ImGui::PopStyleColor(2);

                    if (!m_csgStatus.empty())
                    {
                        ImGui::Spacing();
                        ImGui::TextColored(ImVec4(0.40f, 0.95f, 0.55f, 1.0f), "%s",
                                           m_csgStatus.c_str());
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
                ImGui::Checkbox("2D Sketch Mode (Tap/Draw Ground)", &m_sketchMode);
                ImGui::SameLine();
                if (ImGui::Button("Top View"))
                {
                    m_camera.setViewTop();
                }

                const char* toolNames[] = {
                    "Polyline (Tap)",     "Freehand (S-Pen Drag)", "Smooth Curve (Spline)",
                    "Wave (Sine Ribbon)", "Circle (Stamp)",        "Ellipse (Stamp)",
                    "Square (Stamp)",     "Rectangle (Stamp)",     "Triangle (Stamp)"};
                int currentTool = static_cast<int>(m_sketchTool);
                if (ImGui::Combo("Tool", &currentTool, toolNames, IM_ARRAYSIZE(toolNames)))
                {
                    m_sketchTool = static_cast<SketchTool>(currentTool);
                    rebuildSketchMesh();
                }

                if (m_sketchTool == SketchTool::Freehand)
                {
                    ImGui::TextColored(ImVec4(0.40f, 1.0f, 0.50f, 1.0f),
                                       "S-Pen / Finger: Drag on ground plane to draw!");
                }
                else if (m_sketchTool >= SketchTool::Circle || m_sketchTool == SketchTool::Wave)
                {
                    ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.35f, 1.0f),
                                       "Tap ground plane to stamp shape at coordinate");
                    if (ImGui::Button("Stamp Shape at Origin (0,0)", ImVec2(-1.0f, 0.0f)))
                    {
                        switch (m_sketchTool)
                        {
                        case SketchTool::Circle:
                            m_sketchPoints = geometry::Primitives::create2DCircle(
                                0.0f, 0.0f, m_sketchDimX * 0.5f, m_sketchSegments);
                            break;
                        case SketchTool::Ellipse:
                            m_sketchPoints = geometry::Primitives::create2DEllipse(
                                0.0f, 0.0f, m_sketchDimX * 0.5f, m_sketchDimZ * 0.5f,
                                m_sketchSegments);
                            break;
                        case SketchTool::Square:
                            m_sketchPoints =
                                geometry::Primitives::create2DSquare(0.0f, 0.0f, m_sketchDimX);
                            break;
                        case SketchTool::Rectangle:
                            m_sketchPoints = geometry::Primitives::create2DRectangle(
                                0.0f, 0.0f, m_sketchDimX, m_sketchDimZ);
                            break;
                        case SketchTool::Triangle:
                            m_sketchPoints = geometry::Primitives::create2DTriangle(
                                0.0f, 0.0f, m_sketchDimX, m_sketchDimZ);
                            break;
                        case SketchTool::Wave:
                            m_sketchPoints = geometry::Primitives::create2DWave(
                                0.0f, 0.0f, m_sketchDimX, m_sketchDimZ, m_sketchSegments);
                            break;
                        default:
                            break;
                        }
                        rebuildSketchMesh();
                    }
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

                if (m_sketchTool >= SketchTool::Circle || m_sketchTool == SketchTool::Wave)
                {
                    drawNumericControl("Shape Width / RadX", &m_sketchDimX, 0.1f, 0.2f, 10.0f,
                                       "%.2f");
                    drawNumericControl("Shape Depth / RadZ", &m_sketchDimZ, 0.1f, 0.2f, 10.0f,
                                       "%.2f");
                }

                drawNumericControl("Extrude Height", &m_extrudeHeight, 0.05f, 0.2f, 4.0f, "%.2f");
                drawNumericControl("Bevel / Chamfer Width", &m_bevelWidth, 0.02f, 0.0f, 0.50f,
                                   "%.2f");

                float bevelStepsF = static_cast<float>(m_bevelSegments);
                if (drawNumericControl("Bevel Steps (1=Chamf, 2-5=Round)", &bevelStepsF, 1.0f, 1.0f,
                                       5.0f, "%.0f"))
                {
                    m_bevelSegments = static_cast<int>(bevelStepsF);
                }

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

                ImGui::Separator();
                ImGui::Text("3D Revolve / Lathe:");
                drawNumericControl("Revolve Angle", &m_revolveAngle, 5.0f, 15.0f, 360.0f,
                                   "%.1f deg");
                drawNumericControl("Radial Offset", &m_revolveOffset, 0.05f, 0.05f, 5.0f, "%.2f");
                float revSegsF = static_cast<float>(m_revolveSegments);
                if (drawNumericControl("Revolve Steps", &revSegsF, 2.0f, 8.0f, 72.0f, "%.0f"))
                {
                    m_revolveSegments = static_cast<int>(revSegsF);
                }

                if (!canExtrude)
                {
                    ImGui::BeginDisabled();
                }
                if (ImGui::Button("Revolve Profile to 3D Part (Lathe)", ImVec2(-1.0f, 0.0f)))
                {
                    revolveSketchToPart();
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

                    if (obj.params.csgMode == -1)
                    {
                        ImGui::TextColored(ImVec4(0.40f, 0.85f, 1.0f, 1.0f),
                                           "Baked CSG Solid (%zu triangles)",
                                           obj.cpuIndices.size() / 3);
                        ImGui::TextDisabled(
                            "Transform via Transform Inspector or combine with parts");
                    }
                    else
                    {
                        const char* csgItems[] = {"Solid Primitive", "Subtract (Drill Hole)",
                                                  "Union (Boss Hub)", "Intersect (Box ^ Sphere)",
                                                  "Hollow Cylinder Pipe"};
                        if (ImGui::Combo("CSG Op", &obj.params.csgMode, csgItems,
                                         IM_ARRAYSIZE(csgItems)))
                        {
                            pushUndoState();
                            changed = true;
                        }

                        if (obj.params.csgMode == 0)
                        {
                            if (obj.primitiveType == PrimitiveType::Extrude)
                            {
                                ImGui::Text("Extruded Solid Part (%zu sketch points)",
                                            obj.sketchPoints.size());
                                changed |=
                                    drawNumericControl("Extrude Height", &obj.params.height, 0.05f,
                                                       0.2f, 4.0f, "%.2f", onGeomChanged);
                                changed |=
                                    drawNumericControl("Bevel Width", &obj.params.bevelWidth, 0.02f,
                                                       0.0f, 0.50f, "%.2f", onGeomChanged);
                                float stepsF = static_cast<float>(obj.params.bevelSegments);
                                if (drawNumericControl("Bevel Steps (1=Chamf, 2-5=Round)", &stepsF,
                                                       1.0f, 1.0f, 5.0f, "%.0f", onGeomChanged))
                                {
                                    obj.params.bevelSegments = static_cast<int>(stepsF);
                                    changed = true;
                                }
                            }
                            else if (obj.primitiveType == PrimitiveType::Revolve)
                            {
                                ImGui::Text("Revolved Lathe Part (%zu sketch points)",
                                            obj.sketchPoints.size());
                                changed |= drawNumericControl("Revolve Angle",
                                                              &obj.params.revolveAngle, 5.0f, 15.0f,
                                                              360.0f, "%.1f deg", onGeomChanged);
                                changed |=
                                    drawNumericControl("Radial Offset", &obj.params.revolveOffset,
                                                       0.05f, 0.05f, 5.0f, "%.2f", onGeomChanged);
                                float rSegsF = static_cast<float>(obj.params.revolveSegments);
                                if (drawNumericControl("Revolve Steps", &rSegsF, 2.0f, 8.0f, 72.0f,
                                                       "%.0f", onGeomChanged))
                                {
                                    obj.params.revolveSegments = static_cast<int>(rSegsF);
                                    changed = true;
                                }
                            }
                            else
                            {
                                ImGui::Text("Primitive:");
                                if (ImGui::RadioButton("Box",
                                                       obj.primitiveType == PrimitiveType::Box))
                                {
                                    pushUndoState();
                                    obj.primitiveType = PrimitiveType::Box;
                                    changed = true;
                                }
                                ImGui::SameLine();
                                if (ImGui::RadioButton(
                                        "Cylinder", obj.primitiveType == PrimitiveType::Cylinder))
                                {
                                    pushUndoState();
                                    obj.primitiveType = PrimitiveType::Cylinder;
                                    changed = true;
                                }
                                ImGui::SameLine();
                                if (ImGui::RadioButton("Sphere",
                                                       obj.primitiveType == PrimitiveType::Sphere))
                                {
                                    pushUndoState();
                                    obj.primitiveType = PrimitiveType::Sphere;
                                    changed = true;
                                }
                            }
                        }

                        // Parametric Steppers with direct numpad
                        if (obj.primitiveType == PrimitiveType::Box || obj.params.csgMode > 0)
                        {
                            changed |= drawNumericControl("Width", &obj.params.width, 0.05f, 0.2f,
                                                          4.0f, "%.2f", onGeomChanged);
                            changed |= drawNumericControl("Height", &obj.params.height, 0.05f, 0.2f,
                                                          4.0f, "%.2f", onGeomChanged);
                            changed |= drawNumericControl("Depth", &obj.params.depth, 0.05f, 0.2f,
                                                          4.0f, "%.2f", onGeomChanged);
                        }
                        if (obj.primitiveType == PrimitiveType::Cylinder ||
                            obj.primitiveType == PrimitiveType::Sphere || obj.params.csgMode > 0)
                        {
                            changed |= drawNumericControl("Radius", &obj.params.radius, 0.05f, 0.1f,
                                                          2.0f, "%.2f", onGeomChanged);
                        }
                        if (obj.params.csgMode == 1 || obj.params.csgMode == 4)
                        {
                            changed |=
                                drawNumericControl("Hole Radius", &obj.params.holeRadius, 0.02f,
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
                    }

                    if (changed)
                    {
                        rebuildObjectGeometry(static_cast<size_t>(m_selectedIndex));
                    }
                }

                if (ImGui::CollapsingHeader("Material & Surface Finish",
                                            ImGuiTreeNodeFlags_DefaultOpen))
                {
                    obj.ensureSurfaceMaterials();
                    int numSurfaces = obj.getSurfaceCount();

                    // Surface Target Selector
                    std::vector<std::string> targetNames;
                    targetNames.reserve(numSurfaces + 1);
                    targetNames.push_back("Whole Object");
                    for (int si = 0; si < numSurfaces; ++si)
                    {
                        char buf[64];
                        if (obj.primitiveType == PrimitiveType::Box)
                        {
                            const char* boxFaceNames[] = {"Top (+Y)",  "Bottom (-Y)", "Front (+Z)",
                                                          "Back (-Z)", "Right (+X)",  "Left (-X)"};
                            if (si < 6)
                                snprintf(buf, sizeof(buf), "Surface #%d (%s)", si,
                                         boxFaceNames[si]);
                            else
                                snprintf(buf, sizeof(buf), "Surface #%d", si);
                        }
                        else if (obj.primitiveType == PrimitiveType::Cylinder)
                        {
                            const char* cylFaceNames[] = {"Top Cap", "Bottom Cap", "Side Wall"};
                            if (si < 3)
                                snprintf(buf, sizeof(buf), "Surface #%d (%s)", si,
                                         cylFaceNames[si]);
                            else
                                snprintf(buf, sizeof(buf), "Surface #%d", si);
                        }
                        else if (obj.primitiveType == PrimitiveType::Extrude)
                        {
                            const char* extFaceNames[] = {"Top Cap", "Bottom Cap", "Side Walls",
                                                          "Bevel Rim"};
                            if (si < 4)
                                snprintf(buf, sizeof(buf), "Surface #%d (%s)", si,
                                         extFaceNames[si]);
                            else
                                snprintf(buf, sizeof(buf), "Surface #%d", si);
                        }
                        else
                        {
                            snprintf(buf, sizeof(buf), "Surface #%d", si);
                        }
                        targetNames.push_back(buf);
                    }

                    int currentTargetIdx =
                        (obj.selectedSurfaceId < 0 || obj.selectedSurfaceId >= numSurfaces)
                            ? 0
                            : (obj.selectedSurfaceId + 1);

                    std::vector<const char*> targetItems;
                    targetItems.reserve(targetNames.size());
                    for (const auto& s : targetNames)
                        targetItems.push_back(s.c_str());

                    if (ImGui::Combo("Target", &currentTargetIdx, targetItems.data(),
                                     static_cast<int>(targetItems.size())))
                    {
                        obj.selectedSurfaceId = currentTargetIdx - 1;
                    }

                    ImGui::Checkbox("Tap 3D Face to Pick Surface", &m_pickSurfaceMode);

                    static const char* kMaterialPresets[] = {
                        "Default (Multi-Face)", "Brushed Aluminum",  "Polished Brass / Gold",
                        "Anodized Blue",        "Stainless Steel",   "Matte Red ABS",
                        "Polished Copper",      "Titanium Gunmetal", "Custom Color"};

                    int targetSurfaceId = obj.selectedSurfaceId;
                    int currentPreset = 0;
                    float* pMetallic = nullptr;
                    float* pRoughness = nullptr;
                    utils::math::Vec3f currentColor;

                    if (targetSurfaceId >= 0 &&
                        targetSurfaceId < static_cast<int>(obj.surfaceMaterials.size()))
                    {
                        currentPreset = obj.surfaceMaterials[targetSurfaceId].preset;
                        currentColor = obj.surfaceMaterials[targetSurfaceId].color;
                        pMetallic = &obj.surfaceMaterials[targetSurfaceId].metallic;
                        pRoughness = &obj.surfaceMaterials[targetSurfaceId].roughness;
                    }
                    else
                    {
                        currentPreset = obj.materialPreset;
                        currentColor = obj.materialColor;
                        pMetallic = &obj.metallic;
                        pRoughness = &obj.roughness;
                    }

                    if (currentPreset < 0 || currentPreset > 8)
                        currentPreset = 8;

                    if (ImGui::Combo("Preset", &currentPreset, kMaterialPresets,
                                     IM_ARRAYSIZE(kMaterialPresets)))
                    {
                        pushUndoState();
                        SurfaceMaterial sm = getPresetMaterial(currentPreset, currentColor);
                        applySurfaceMaterial(obj, targetSurfaceId, sm);
                    }

                    float col[3] = {currentColor.x, currentColor.y, currentColor.z};
                    if (ImGui::ColorEdit3(targetSurfaceId < 0 ? "Part Color" : "Surface Color",
                                          col))
                    {
                        pushUndoState();
                        SurfaceMaterial sm;
                        sm.color = {col[0], col[1], col[2]};
                        sm.metallic = *pMetallic;
                        sm.roughness = *pRoughness;
                        sm.preset = 8;
                        applySurfaceMaterial(obj, targetSurfaceId, sm);
                    }

                    auto onMatChanged = [this, targetSurfaceId]()
                    {
                        if (m_selectedIndex >= 0 &&
                            m_selectedIndex < static_cast<int>(m_objects.size()))
                        {
                            auto& o = m_objects[m_selectedIndex];
                            if (targetSurfaceId < 0)
                            {
                                o.materialPreset = 8;
                                SurfaceMaterial sm;
                                sm.color = o.materialColor;
                                sm.metallic = o.metallic;
                                sm.roughness = o.roughness;
                                sm.preset = 8;
                                applySurfaceMaterial(o, -1, sm);
                            }
                            else if (targetSurfaceId < static_cast<int>(o.surfaceMaterials.size()))
                            {
                                o.surfaceMaterials[targetSurfaceId].preset = 8;
                                applySurfaceMaterial(o, targetSurfaceId,
                                                     o.surfaceMaterials[targetSurfaceId]);
                            }
                        }
                    };

                    if (drawNumericControl("Metallic", pMetallic, 0.05f, 0.0f, 1.0f, "%.2f",
                                           onMatChanged))
                    {
                        onMatChanged();
                    }

                    if (drawNumericControl("Roughness", pRoughness, 0.05f, 0.04f, 1.0f, "%.2f",
                                           onMatChanged))
                    {
                        onMatChanged();
                    }
                }
            }

            // 3D Point-to-Point Caliper & Measurement
            if (ImGui::CollapsingHeader("3D Caliper & Measure"))
            {
                ImGui::Checkbox("3D Measure Mode (Tap 2 Surface Points)", &m_measureMode);
                ImGui::Checkbox("Snap to Exact Mesh Corners/Vertices", &m_measureSnapVertex);

                if (ImGui::Button("Clear Measurement", ImVec2(-1.0f, 0.0f)))
                {
                    clearMeasurement();
                }

                if (m_measurePointCount == 0)
                {
                    ImGui::TextDisabled("(Tap any 3D object surface or grid to place Point A)");
                }
                else if (m_measurePointCount == 1)
                {
                    ImGui::TextColored(ImVec4(1.0f, 0.25f, 0.70f, 1.0f),
                                       "Point A: (%.3f, %.3f, %.3f)", m_measurePtA.x,
                                       m_measurePtA.y, m_measurePtA.z);
                    ImGui::TextDisabled("(Tap second surface point to place Point B)");
                }
                else if (m_measurePointCount >= 2)
                {
                    utils::math::Vec3f delta = m_measurePtB - m_measurePtA;
                    float dist = delta.length();

                    ImGui::TextColored(ImVec4(1.0f, 0.25f, 0.70f, 1.0f),
                                       "Point A: (%.3f, %.3f, %.3f)", m_measurePtA.x,
                                       m_measurePtA.y, m_measurePtA.z);
                    ImGui::TextColored(ImVec4(0.25f, 0.90f, 1.0f, 1.0f),
                                       "Point B: (%.3f, %.3f, %.3f)", m_measurePtB.x,
                                       m_measurePtB.y, m_measurePtB.z);

                    ImGui::Separator();
                    ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.35f, 1.0f), "Distance L = %.4f", dist);
                    ImGui::Text("Delta: |dX|=%.4f, |dY|=%.4f, |dZ|=%.4f", std::abs(delta.x),
                                std::abs(delta.y), std::abs(delta.z));
                }
            }

            // Display Layers & VSync
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

            // 5. Project Save / Load & Export Section
            if (ImGui::CollapsingHeader("Project Save / Load & Export"))
            {
                if (ImGui::Button("Save Workspace (.mcad)", ImVec2(-1.0f, 0.0f)))
                {
                    saveWorkspace();
                }
                if (ImGui::Button("Load Workspace (.mcad)", ImVec2(-1.0f, 0.0f)))
                {
                    loadWorkspace();
                }
                if (!m_projectStatus.empty())
                {
                    ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.8f, 1.0f), "%s",
                                       m_projectStatus.c_str());
                }
                ImGui::TextDisabled("Workspace: /sdcard/Download/mobilecad_workspace.mcad");

                ImGui::Separator();
                if (ImGui::Button("Export Binary STL (.stl)", ImVec2(-1.0f, 0.0f)))
                {
                    exportSTL();
                }
                if (ImGui::Button("Export Wavefront OBJ (.obj)", ImVec2(-1.0f, 0.0f)))
                {
                    exportOBJ();
                }
                if (!m_exportStatus.empty())
                {
                    ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "%s",
                                       m_exportStatus.c_str());
                }
                ImGui::TextDisabled("Exports: /sdcard/Download/mobilecad_part.{stl,obj}");

                ImGui::Separator();
                ImGui::Text("Import 3D Mesh (.stl / .obj):");
                if (ImGui::Button("Scan /sdcard/Download/ for Meshes", ImVec2(-1.0f, 0.0f)))
                {
                    refreshDiscoveredFiles();
                }
                if (ImGui::Button("Import mobilecad_part.stl", ImVec2(-1.0f, 0.0f)))
                {
                    importMeshFile("/sdcard/Download/mobilecad_part.stl");
                }
                if (ImGui::Button("Import mobilecad_part.obj", ImVec2(-1.0f, 0.0f)))
                {
                    importMeshFile("/sdcard/Download/mobilecad_part.obj");
                }

                if (!m_discoveredMeshFiles.empty())
                {
                    std::vector<const char*> fileItems;
                    std::vector<std::string> fileLabels;
                    fileItems.reserve(m_discoveredMeshFiles.size());
                    fileLabels.reserve(m_discoveredMeshFiles.size());
                    for (const auto& path : m_discoveredMeshFiles)
                    {
                        fileLabels.push_back(std::filesystem::path(path).filename().string());
                    }
                    for (const auto& l : fileLabels)
                    {
                        fileItems.push_back(l.c_str());
                    }

                    if (m_selectedMeshFileIdx < 0 ||
                        m_selectedMeshFileIdx >= static_cast<int>(fileItems.size()))
                        m_selectedMeshFileIdx = 0;

                    ImGui::Combo("Files", &m_selectedMeshFileIdx, fileItems.data(),
                                 static_cast<int>(fileItems.size()));
                    if (ImGui::Button("Import Selected File", ImVec2(-1.0f, 0.0f)))
                    {
                        if (m_selectedMeshFileIdx >= 0 &&
                            m_selectedMeshFileIdx < static_cast<int>(m_discoveredMeshFiles.size()))
                        {
                            importMeshFile(m_discoveredMeshFiles[m_selectedMeshFileIdx]);
                        }
                    }
                }
                if (!m_importStatus.empty())
                {
                    ImGui::TextColored(ImVec4(0.3f, 0.9f, 1.0f, 1.0f), "%s",
                                       m_importStatus.c_str());
                }
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
    if (m_ghostPipeline != VK_NULL_HANDLE)
    {
        vkDestroyPipeline(device, m_ghostPipeline, nullptr);
        m_ghostPipeline = VK_NULL_HANDLE;
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

        if (m_sketchMode && m_sketchTool == SketchTool::Freehand && m_swapchain)
        {
            float screenW = static_cast<float>(getSwapchainExtentW());
            float screenH = static_cast<float>(getSwapchainExtentH());
            if (screenW > 0.0f && screenH > 0.0f)
            {
                float aspect = screenW / screenH;
                auto viewProj = m_camera.getViewProjectionMatrix(aspect);
                auto ray = utils::math::Ray::screenPointToRay(x0, y0, screenW, screenH, viewProj);
                if (std::abs(ray.dir.y) > 1e-4f)
                {
                    float t = -ray.origin.y / ray.dir.y;
                    if (t > 0.0f)
                    {
                        float hitX = ray.origin.x + t * ray.dir.x;
                        float hitZ = ray.origin.z + t * ray.dir.z;
                        if (m_sketchPoints.empty())
                        {
                            m_sketchPoints.push_back({hitX, 0.005f, hitZ});
                            rebuildSketchMesh();
                        }
                        else
                        {
                            float dx = hitX - m_sketchPoints.back().x;
                            float dz = hitZ - m_sketchPoints.back().z;
                            if (std::hypot(dx, dz) >= 0.10f)
                            {
                                m_sketchPoints.push_back({hitX, 0.005f, hitZ});
                                rebuildSketchMesh();
                            }
                        }
                    }
                }
            }
        }
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

                if (m_measureMode)
                {
                    bool hitSurface = false;
                    float closestDist = 1e30f;
                    utils::math::Vec3f bestWorldHit{0.0f, 0.0f, 0.0f};
                    utils::math::Vec3f hitTriVerts[3];
                    bool hasHitTriangle = false;

                    for (const auto& obj : m_objects)
                    {
                        if (obj.cpuVertices.empty() || obj.cpuIndices.size() < 3)
                            continue;

                        auto model = obj.getModelMatrix();
                        auto invModel = model.inverted();

                        utils::math::Ray localRay;
                        localRay.origin = invModel * ray.origin;
                        localRay.dir = invModel.transformDirection(ray.dir).normalized();

                        float tAABB = 0.0f;
                        if (!utils::math::Ray::intersectAABB(localRay, obj.aabbMin, obj.aabbMax,
                                                             tAABB))
                            continue;

                        for (size_t ti = 0; ti + 2 < obj.cpuIndices.size(); ti += 3)
                        {
                            uint32_t i0 = obj.cpuIndices[ti];
                            uint32_t i1 = obj.cpuIndices[ti + 1];
                            uint32_t i2 = obj.cpuIndices[ti + 2];

                            if (i0 >= obj.cpuVertices.size() || i1 >= obj.cpuVertices.size() ||
                                i2 >= obj.cpuVertices.size())
                                continue;

                            utils::math::Vec3f lv0{obj.cpuVertices[i0].position[0],
                                                   obj.cpuVertices[i0].position[1],
                                                   obj.cpuVertices[i0].position[2]};
                            utils::math::Vec3f lv1{obj.cpuVertices[i1].position[0],
                                                   obj.cpuVertices[i1].position[1],
                                                   obj.cpuVertices[i1].position[2]};
                            utils::math::Vec3f lv2{obj.cpuVertices[i2].position[0],
                                                   obj.cpuVertices[i2].position[1],
                                                   obj.cpuVertices[i2].position[2]};

                            float tTri = 0.0f;
                            if (utils::math::Ray::intersectTriangle(localRay, lv0, lv1, lv2, tTri))
                            {
                                utils::math::Vec3f localHit = localRay.origin + localRay.dir * tTri;
                                utils::math::Vec3f worldHit = model * localHit;
                                float dist = (worldHit - ray.origin).length();
                                if (dist < closestDist)
                                {
                                    closestDist = dist;
                                    bestWorldHit = worldHit;
                                    hitSurface = true;
                                    hasHitTriangle = true;
                                    hitTriVerts[0] = model * lv0;
                                    hitTriVerts[1] = model * lv1;
                                    hitTriVerts[2] = model * lv2;
                                }
                            }
                        }
                    }

                    if (hitSurface && hasHitTriangle && m_measureSnapVertex)
                    {
                        float bestSnapDist = 0.18f;
                        for (int vi = 0; vi < 3; ++vi)
                        {
                            float d = (hitTriVerts[vi] - bestWorldHit).length();
                            if (d < bestSnapDist)
                            {
                                bestSnapDist = d;
                                bestWorldHit = hitTriVerts[vi];
                            }
                        }
                    }

                    if (!hitSurface)
                    {
                        // Fallback to ground plane Y = 0
                        if (std::abs(ray.dir.y) > 1e-4f)
                        {
                            float t = -ray.origin.y / ray.dir.y;
                            if (t > 0.0f)
                            {
                                bestWorldHit = {ray.origin.x + t * ray.dir.x, 0.0f,
                                                ray.origin.z + t * ray.dir.z};
                                if (m_snapToGrid)
                                {
                                    bestWorldHit.x = std::round(bestWorldHit.x / 0.25f) * 0.25f;
                                    bestWorldHit.z = std::round(bestWorldHit.z / 0.25f) * 0.25f;
                                }
                                hitSurface = true;
                            }
                        }
                    }

                    if (hitSurface)
                    {
                        if (m_measurePointCount == 0 || m_measurePointCount == 2)
                        {
                            m_measurePtA = bestWorldHit;
                            m_measurePointCount = 1;
                            LOGI("3D Caliper: Placed Point A at (%.3f, %.3f, %.3f)", m_measurePtA.x,
                                 m_measurePtA.y, m_measurePtA.z);
                        }
                        else if (m_measurePointCount == 1)
                        {
                            m_measurePtB = bestWorldHit;
                            m_measurePointCount = 2;
                            float dist = (m_measurePtB - m_measurePtA).length();
                            LOGI(
                                "3D Caliper: Placed Point B at (%.3f, %.3f, %.3f), Distance L = "
                                "%.3f",
                                m_measurePtB.x, m_measurePtB.y, m_measurePtB.z, dist);
                        }
                    }
                }
                else if (m_sketchMode)
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

                            switch (m_sketchTool)
                            {
                            case SketchTool::Polyline:
                            case SketchTool::SmoothCurve:
                                m_sketchPoints.push_back({hitX, 0.005f, hitZ});
                                rebuildSketchMesh();
                                LOGI("Sketch tap -> placed point (%.2f, 0.005, %.2f) [%zu pts]",
                                     hitX, hitZ, m_sketchPoints.size());
                                break;
                            case SketchTool::Freehand:
                                if (m_sketchPoints.empty())
                                {
                                    m_sketchPoints.push_back({hitX, 0.005f, hitZ});
                                    rebuildSketchMesh();
                                }
                                else
                                {
                                    float dx = hitX - m_sketchPoints.back().x;
                                    float dz = hitZ - m_sketchPoints.back().z;
                                    if (std::hypot(dx, dz) >= 0.10f)
                                    {
                                        m_sketchPoints.push_back({hitX, 0.005f, hitZ});
                                        rebuildSketchMesh();
                                    }
                                }
                                break;
                            case SketchTool::Circle:
                                m_sketchPoints = geometry::Primitives::create2DCircle(
                                    hitX, hitZ, m_sketchDimX * 0.5f, m_sketchSegments);
                                rebuildSketchMesh();
                                LOGI("Stamped Circle at (%.2f, %.2f) rad=%.2f", hitX, hitZ,
                                     m_sketchDimX * 0.5f);
                                break;
                            case SketchTool::Ellipse:
                                m_sketchPoints = geometry::Primitives::create2DEllipse(
                                    hitX, hitZ, m_sketchDimX * 0.5f, m_sketchDimZ * 0.5f,
                                    m_sketchSegments);
                                rebuildSketchMesh();
                                LOGI("Stamped Ellipse at (%.2f, %.2f) rx=%.2f, rz=%.2f", hitX, hitZ,
                                     m_sketchDimX * 0.5f, m_sketchDimZ * 0.5f);
                                break;
                            case SketchTool::Square:
                                m_sketchPoints =
                                    geometry::Primitives::create2DSquare(hitX, hitZ, m_sketchDimX);
                                rebuildSketchMesh();
                                LOGI("Stamped Square at (%.2f, %.2f) side=%.2f", hitX, hitZ,
                                     m_sketchDimX);
                                break;
                            case SketchTool::Rectangle:
                                m_sketchPoints = geometry::Primitives::create2DRectangle(
                                    hitX, hitZ, m_sketchDimX, m_sketchDimZ);
                                rebuildSketchMesh();
                                LOGI("Stamped Rectangle at (%.2f, %.2f) w=%.2f, d=%.2f", hitX, hitZ,
                                     m_sketchDimX, m_sketchDimZ);
                                break;
                            case SketchTool::Triangle:
                                m_sketchPoints = geometry::Primitives::create2DTriangle(
                                    hitX, hitZ, m_sketchDimX, m_sketchDimZ);
                                rebuildSketchMesh();
                                LOGI("Stamped Triangle at (%.2f, %.2f) base=%.2f, h=%.2f", hitX,
                                     hitZ, m_sketchDimX, m_sketchDimZ);
                                break;
                            case SketchTool::Wave:
                                m_sketchPoints = geometry::Primitives::create2DWave(
                                    hitX, hitZ, m_sketchDimX, m_sketchDimZ, m_sketchSegments);
                                rebuildSketchMesh();
                                LOGI("Stamped Wave at (%.2f, %.2f) len=%.2f, amp=%.2f", hitX, hitZ,
                                     m_sketchDimX, m_sketchDimZ * 0.3f);
                                break;
                            }
                        }
                    }
                }
                else if (m_pickSurfaceMode)
                {
                    int hitObjIndex = -1;
                    int hitSurfaceId = -1;
                    float closestDist = 1e30f;

                    for (size_t oi = 0; oi < m_objects.size(); ++oi)
                    {
                        const auto& obj = m_objects[oi];
                        if (obj.cpuVertices.empty() || obj.cpuIndices.size() < 3)
                            continue;

                        auto model = obj.getModelMatrix();
                        auto invModel = model.inverted();

                        utils::math::Ray localRay;
                        localRay.origin = invModel * ray.origin;
                        localRay.dir = invModel.transformDirection(ray.dir).normalized();

                        float tAABB = 0.0f;
                        if (!utils::math::Ray::intersectAABB(localRay, obj.aabbMin, obj.aabbMax,
                                                             tAABB))
                            continue;

                        for (size_t ti = 0; ti + 2 < obj.cpuIndices.size(); ti += 3)
                        {
                            uint32_t i0 = obj.cpuIndices[ti];
                            uint32_t i1 = obj.cpuIndices[ti + 1];
                            uint32_t i2 = obj.cpuIndices[ti + 2];
                            if (i0 >= obj.cpuVertices.size() || i1 >= obj.cpuVertices.size() ||
                                i2 >= obj.cpuVertices.size())
                                continue;

                            utils::math::Vec3f lv0{obj.cpuVertices[i0].position[0],
                                                   obj.cpuVertices[i0].position[1],
                                                   obj.cpuVertices[i0].position[2]};
                            utils::math::Vec3f lv1{obj.cpuVertices[i1].position[0],
                                                   obj.cpuVertices[i1].position[1],
                                                   obj.cpuVertices[i1].position[2]};
                            utils::math::Vec3f lv2{obj.cpuVertices[i2].position[0],
                                                   obj.cpuVertices[i2].position[1],
                                                   obj.cpuVertices[i2].position[2]};

                            float tTri = 0.0f;
                            if (utils::math::Ray::intersectTriangle(localRay, lv0, lv1, lv2, tTri))
                            {
                                utils::math::Vec3f localHit = localRay.origin + localRay.dir * tTri;
                                utils::math::Vec3f worldHit = model * localHit;
                                float dist = (worldHit - ray.origin).length();
                                if (dist < closestDist)
                                {
                                    closestDist = dist;
                                    hitObjIndex = static_cast<int>(oi);
                                    hitSurfaceId =
                                        static_cast<int>(std::round(obj.cpuVertices[i0].pbr[2]));
                                }
                            }
                        }
                    }

                    if (hitObjIndex >= 0)
                    {
                        m_selectedIndex = hitObjIndex;
                        for (size_t i = 0; i < m_objects.size(); ++i)
                        {
                            m_objects[i].isSelected = (static_cast<int>(i) == hitObjIndex);
                        }
                        m_objects[hitObjIndex].selectedSurfaceId = hitSurfaceId;
                        m_objects[hitObjIndex].ensureSurfaceMaterials();
                        LOGI("Picked Surface #%d on object %s", hitSurfaceId,
                             m_objects[hitObjIndex].name.c_str());
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

                    if (closestIndex >= 0)
                    {
                        if (m_multiSelectTapMode)
                        {
                            m_objects[closestIndex].isSelected =
                                !m_objects[closestIndex].isSelected;
                            m_selectedIndex = closestIndex;
                        }
                        else
                        {
                            for (auto& obj : m_objects)
                            {
                                obj.isSelected = false;
                            }
                            m_objects[closestIndex].isSelected = true;
                            m_selectedIndex = closestIndex;
                        }
                    }
                    else
                    {
                        if (!m_multiSelectTapMode)
                        {
                            for (auto& obj : m_objects)
                            {
                                obj.isSelected = false;
                            }
                            m_selectedIndex = -1;
                        }
                    }

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

            if (m_sketchMode && m_sketchTool == SketchTool::Freehand && m_swapchain)
            {
                float screenW = static_cast<float>(getSwapchainExtentW());
                float screenH = static_cast<float>(getSwapchainExtentH());
                if (screenW > 0.0f && screenH > 0.0f)
                {
                    float aspect = screenW / screenH;
                    auto viewProj = m_camera.getViewProjectionMatrix(aspect);
                    auto ray =
                        utils::math::Ray::screenPointToRay(x0, y0, screenW, screenH, viewProj);
                    if (std::abs(ray.dir.y) > 1e-4f)
                    {
                        float t = -ray.origin.y / ray.dir.y;
                        if (t > 0.0f)
                        {
                            float hitX = ray.origin.x + t * ray.dir.x;
                            float hitZ = ray.origin.z + t * ray.dir.z;
                            if (m_sketchPoints.empty())
                            {
                                m_sketchPoints.push_back({hitX, 0.005f, hitZ});
                                rebuildSketchMesh();
                            }
                            else
                            {
                                float dx = hitX - m_sketchPoints.back().x;
                                float dz = hitZ - m_sketchPoints.back().z;
                                if (std::hypot(dx, dz) >= 0.10f)
                                {
                                    m_sketchPoints.push_back({hitX, 0.005f, hitZ});
                                    rebuildSketchMesh();
                                }
                            }
                        }
                    }
                }
                m_lastTouchX = x0;
                m_lastTouchY = y0;
                return;
            }

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
    if (!m_vkContext || !m_swapchain || !m_pipeline || !m_ghostPipeline || !m_linePipeline ||
        !m_pipelineLayout || !m_renderPass)
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
        !m_ghostPipeline || !m_linePipeline || !m_pipelineLayout)
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
    utils::math::Mat4f view = m_camera.getViewMatrix();

    // 1. Draw CAD Reference Grid and XYZ Origin Triad (LINE_LIST)
    if (m_showGrid && m_gridMesh && m_gridMesh->getVertexCount() > 0)
    {
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_linePipeline);
        VkBuffer vertexBuffers[] = {m_gridMesh->getVertexBuffer()};
        VkDeviceSize offsets[] = {0};
        vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);
        PushConstantData pcData{};
        pcData.mvp = viewProj;
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
        PushConstantData pcData{};
        pcData.mvp = viewProj;
        vkCmdPushConstants(commandBuffer, m_pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0,
                           sizeof(PushConstantData), &pcData);
        vkCmdDraw(commandBuffer, m_sketchLineMesh->getVertexCount(), 1, 0, 0);
    }

    // 3. Draw 3D Solid CAD Parts (TRIANGLE_LIST)
    if (m_showSolid)
    {
        // Pass 1: Opaque +ve Solids (m_pipeline, alpha = 1.0f)
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline);
        for (const auto& obj : m_objects)
        {
            if (obj.isNegative || !obj.solidMesh || obj.solidMesh->getVertexCount() == 0)
                continue;

            auto model = obj.getModelMatrix();
            auto normalMatView = (view * model).inverted().transposed();

            PushConstantData pcData{};
            pcData.mvp = viewProj * model;

            // Row 0 of normal matrix + materialColor.r
            pcData.normalRow0[0] = normalMatView(0, 0);
            pcData.normalRow0[1] = normalMatView(0, 1);
            pcData.normalRow0[2] = normalMatView(0, 2);
            pcData.normalRow0[3] = obj.materialColor.x;

            // Row 1 of normal matrix + materialColor.g
            pcData.normalRow1[0] = normalMatView(1, 0);
            pcData.normalRow1[1] = normalMatView(1, 1);
            pcData.normalRow1[2] = normalMatView(1, 2);
            pcData.normalRow1[3] = obj.materialColor.y;

            // Row 2 of normal matrix + materialColor.b
            pcData.normalRow2[0] = normalMatView(2, 0);
            pcData.normalRow2[1] = normalMatView(2, 1);
            pcData.normalRow2[2] = normalMatView(2, 2);
            pcData.normalRow2[3] = obj.materialColor.z;

            // PBR parameters: metallic, roughness, useMaterialColor (0 = vertex colors, 1 =
            // material preset), alpha
            pcData.pbrParams[0] = obj.metallic;
            pcData.pbrParams[1] = obj.roughness;
            pcData.pbrParams[2] = (obj.materialPreset > 0) ? 1.0f : 0.0f;
            pcData.pbrParams[3] = 1.0f;

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

        // Pass 2: Transparent -ve Ghost Cutters (m_ghostPipeline, alpha = 0.38f)
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_ghostPipeline);
        for (const auto& obj : m_objects)
        {
            if (!obj.isNegative || !obj.solidMesh || obj.solidMesh->getVertexCount() == 0)
                continue;

            auto model = obj.getModelMatrix();
            auto normalMatView = (view * model).inverted().transposed();

            PushConstantData pcData{};
            pcData.mvp = viewProj * model;

            // Red cutter tint
            pcData.normalRow0[0] = normalMatView(0, 0);
            pcData.normalRow0[1] = normalMatView(0, 1);
            pcData.normalRow0[2] = normalMatView(0, 2);
            pcData.normalRow0[3] = 0.95f;

            pcData.normalRow1[0] = normalMatView(1, 0);
            pcData.normalRow1[1] = normalMatView(1, 1);
            pcData.normalRow1[2] = normalMatView(1, 2);
            pcData.normalRow1[3] = 0.35f;

            pcData.normalRow2[0] = normalMatView(2, 0);
            pcData.normalRow2[1] = normalMatView(2, 1);
            pcData.normalRow2[2] = normalMatView(2, 2);
            pcData.normalRow2[3] = 0.30f;

            pcData.pbrParams[0] = 0.10f;
            pcData.pbrParams[1] = 0.60f;
            pcData.pbrParams[2] = 1.0f;
            pcData.pbrParams[3] = 0.38f;

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
        bool isSelected = (obj.isSelected || static_cast<int>(i) == m_selectedIndex);

        if (!m_showWireframe && !isSelected && !obj.isNegative)
            continue;

        auto* meshToDraw = (isSelected && obj.selectedWireframeMesh)
                               ? obj.selectedWireframeMesh.get()
                               : obj.wireframeMesh.get();

        if (!meshToDraw || meshToDraw->getVertexCount() == 0)
            continue;

        auto model = obj.getModelMatrix();
        PushConstantData pcData{};
        pcData.mvp = viewProj * model;
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
