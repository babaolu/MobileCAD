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
        m_vkContext.get(), static_cast<uint32_t>(width), static_cast<uint32_t>(height));
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

void CADApp::initPipelineLayout()
{
    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(utils::math::Mat4f);

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

    // 2. 3D Solid CAD Part & Wireframe feature lines
    rebuildGeometry();
}

void CADApp::rebuildGeometry()
{
    auto device = m_vkContext->getDevice();
    auto physDevice = m_vkContext->getPhysicalDevice();

    vkDeviceWaitIdle(device);

    geometry::Primitives::MeshData data;
    if (m_params.csgMode == 0)
    {
        switch (m_currentPrimitive)
        {
        case PrimitiveType::Box:
            data = geometry::Primitives::createBox(m_params.width, m_params.height, m_params.depth);
            break;
        case PrimitiveType::Cylinder:
            data = geometry::Primitives::createCylinder(m_params.radius, m_params.height,
                                                        static_cast<uint32_t>(m_params.segments));
            break;
        case PrimitiveType::Sphere:
            data = geometry::Primitives::createSphere(
                m_params.radius, static_cast<uint32_t>(m_params.segments * 3 / 4),
                static_cast<uint32_t>(m_params.segments));
            break;
        }
    }
    else if (m_params.csgMode == 1)
    {
        data = geometry::Primitives::createCsgDifference(m_params.width, m_params.height,
                                                         m_params.depth, m_params.holeRadius,
                                                         m_params.segments);
    }
    else if (m_params.csgMode == 2)
    {
        data = geometry::Primitives::createCsgUnion(m_params.width, m_params.height, m_params.depth,
                                                    m_params.radius, m_params.segments);
    }
    else if (m_params.csgMode == 3)
    {
        data = geometry::Primitives::createCsgIntersection(
            m_params.width, m_params.height, m_params.depth, m_params.radius, m_params.segments);
    }
    else if (m_params.csgMode == 4)
    {
        data = geometry::Primitives::createHollowCylinder(m_params.radius, m_params.holeRadius,
                                                          m_params.height, m_params.segments);
    }

    m_currentVertices = data.first;
    m_currentIndices = data.second;

    auto wireLines = geometry::Primitives::createWireframeLines(data.first, data.second);

    m_solidMesh =
        std::make_unique<rendering::MeshBuffer>(device, physDevice, data.first, data.second);
    m_wireframeMesh = std::make_unique<rendering::MeshBuffer>(device, physDevice, wireLines);
}

void CADApp::setPrimitive(PrimitiveType type)
{
    m_params.csgMode = 0;
    m_currentPrimitive = type;
    rebuildGeometry();
}

bool CADApp::exportSTL()
{
    if (m_currentVertices.empty() || m_currentIndices.size() < 3)
    {
        m_exportStatus = "Export error: No geometry";
        return false;
    }

    bool ok = io::MeshExporter::exportBinarySTL(m_exportPath, m_currentVertices, m_currentIndices);
    if (ok)
    {
        uint32_t triCount = static_cast<uint32_t>(m_currentIndices.size() / 3);
        char buf[128];
        snprintf(buf, sizeof(buf), "Exported: %u tris to part_export.stl", triCount);
        m_exportStatus = buf;
        LOGI("%s (%s)", buf, m_exportPath.c_str());
    }
    else
    {
        m_exportStatus = "Export failed (check permissions)";
        LOGE("Failed to export STL to %s", m_exportPath.c_str());
    }
    return ok;
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

void CADApp::renderImGui()
{
    ImGui::SetNextWindowPos(ImVec2(30.0f, 30.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowBgAlpha(0.88f);
    if (ImGui::Begin("MobileCAD Studio", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        // 1. Performance & Stats
        float fps = ImGui::GetIO().Framerate;
        uint32_t vertCount = static_cast<uint32_t>(m_currentVertices.size());
        uint32_t triCount = static_cast<uint32_t>(m_currentIndices.size() / 3);
        if (m_gridMesh && m_showGrid)
        {
            vertCount += m_gridMesh->getVertexCount();
        }
        if (m_wireframeMesh && m_showWireframe)
        {
            vertCount += m_wireframeMesh->getVertexCount();
        }

        ImGui::TextColored(ImVec4(0.35f, 0.85f, 1.0f, 1.0f), "Vulkan 1.4 Native CAD");
        ImGui::Text("FPS: %.1f (%.2f ms/frame) | %s", fps, 1000.0f / (fps > 0.0f ? fps : 60.0f),
                    m_vsyncEnabled ? "VSync 120Hz" : "Unlocked");
        ImGui::Text("Solid: %u verts, %u tris (Total: %u)",
                    static_cast<uint32_t>(m_currentVertices.size()), triCount, vertCount);
        ImGui::Separator();

        // 2. View Presets
        ImGui::Text("Camera Presets:");
        if (ImGui::Button("Iso"))
        {
            m_camera.setViewIso();
        }
        ImGui::SameLine();
        if (ImGui::Button("Top"))
        {
            m_camera.setViewTop();
        }
        ImGui::SameLine();
        if (ImGui::Button("Front"))
        {
            m_camera.setViewFront();
        }
        ImGui::SameLine();
        if (ImGui::Button("Right"))
        {
            m_camera.setViewRight();
        }
        ImGui::SameLine();
        if (ImGui::Button("Reset"))
        {
            m_camera.resetView();
        }

        // 3. Parametric Modeling & CSG Section
        if (ImGui::CollapsingHeader("Parameters & CSG", ImGuiTreeNodeFlags_DefaultOpen))
        {
            bool changed = false;

            const char* csgItems[] = {"Solid Primitive", "Subtract (Drill Hole)",
                                      "Union (Boss Hub)", "Intersect (Box ^ Sphere)",
                                      "Hollow Cylinder Pipe"};
            if (ImGui::Combo("CSG Op", &m_params.csgMode, csgItems, IM_ARRAYSIZE(csgItems)))
            {
                changed = true;
            }

            if (m_params.csgMode == 0)
            {
                ImGui::Text("Primitive:");
                if (ImGui::RadioButton("Box", m_currentPrimitive == PrimitiveType::Box))
                {
                    m_currentPrimitive = PrimitiveType::Box;
                    changed = true;
                }
                ImGui::SameLine();
                if (ImGui::RadioButton("Cylinder", m_currentPrimitive == PrimitiveType::Cylinder))
                {
                    m_currentPrimitive = PrimitiveType::Cylinder;
                    changed = true;
                }
                ImGui::SameLine();
                if (ImGui::RadioButton("Sphere", m_currentPrimitive == PrimitiveType::Sphere))
                {
                    m_currentPrimitive = PrimitiveType::Sphere;
                    changed = true;
                }
            }

            // Parametric Sliders
            if (m_currentPrimitive == PrimitiveType::Box || m_params.csgMode > 0)
            {
                changed |= ImGui::SliderFloat("Width", &m_params.width, 0.2f, 4.0f, "%.2f");
                changed |= ImGui::SliderFloat("Height", &m_params.height, 0.2f, 4.0f, "%.2f");
                changed |= ImGui::SliderFloat("Depth", &m_params.depth, 0.2f, 4.0f, "%.2f");
            }
            if (m_currentPrimitive == PrimitiveType::Cylinder ||
                m_currentPrimitive == PrimitiveType::Sphere || m_params.csgMode > 0)
            {
                changed |= ImGui::SliderFloat("Radius", &m_params.radius, 0.1f, 2.0f, "%.2f");
            }
            if (m_params.csgMode == 1 || m_params.csgMode == 4)
            {
                changed |=
                    ImGui::SliderFloat("Hole Radius", &m_params.holeRadius, 0.05f, 1.8f, "%.2f");
            }
            if (m_currentPrimitive == PrimitiveType::Cylinder ||
                m_currentPrimitive == PrimitiveType::Sphere || m_params.csgMode > 0)
            {
                changed |= ImGui::SliderInt("Segments", &m_params.segments, 8, 64);
            }

            if (changed)
            {
                rebuildGeometry();
            }
        }

        // 4. Display Layers
        if (ImGui::CollapsingHeader("Display Layers & VSync", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::Checkbox("Grid & Triad", &m_showGrid);
            ImGui::SameLine();
            ImGui::Checkbox("Solid", &m_showSolid);
            ImGui::SameLine();
            ImGui::Checkbox("Wireframe", &m_showWireframe);

            ImGui::Checkbox("VSync (120Hz)", &m_vsyncEnabled);
        }

        // 5. Watertight Binary STL Export
        if (ImGui::CollapsingHeader("Binary STL Export", ImGuiTreeNodeFlags_DefaultOpen))
        {
            if (ImGui::Button("Export STL Model", ImVec2(-1.0f, 0.0f)))
            {
                exportSTL();
            }
            if (!m_exportStatus.empty())
            {
                ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "%s", m_exportStatus.c_str());
            }
            ImGui::TextDisabled("File: part_export.stl");
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

    if (m_wireframeMesh)
    {
        m_wireframeMesh.reset();
    }
    if (m_solidMesh)
    {
        m_solidMesh.reset();
    }
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
    if (action == AMOTION_EVENT_ACTION_UP || action == AMOTION_EVENT_ACTION_CANCEL)
    {
        m_activePointers = 0;
        m_lastPinchDist = 0.0f;
        return;
    }

    if (pointerCount != m_activePointers)
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
        else
        {
            m_lastPinchDist = 0.0f;
        }
        return;
    }

    if (action == AMOTION_EVENT_ACTION_MOVE)
    {
        if (pointerCount == 1)
        {
            float dx = x0 - m_lastTouchX;
            float dy = y0 - m_lastTouchY;
            m_camera.orbit(dx, dy);
            m_lastTouchX = x0;
            m_lastTouchY = y0;
        }
        else if (pointerCount >= 2)
        {
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
    else if (action == AMOTION_EVENT_ACTION_DOWN || action == AMOTION_EVENT_ACTION_POINTER_DOWN)
    {
        m_lastTouchX = x0;
        m_lastTouchY = y0;
        if (pointerCount >= 2)
        {
            m_lastPinchDist = std::hypot(x1 - x0, y1 - y0);
            m_lastMidX = (x0 + x1) * 0.5f;
            m_lastMidY = (y0 + y1) * 0.5f;
        }
    }
}

bool CADApp::drawFrame()
{
    if (!m_vkContext || !m_swapchain || !m_pipeline || !m_linePipeline || !m_pipelineLayout ||
        !m_renderPass)
    {
        LOGE(
            "FATAL: drawFrame #%u — null resource (vkCtx=%p swap=%p pipe=%p linePipe=%p layout=%p "
            "rp=%p)",
            m_drawFrameCount + 1, (void*)m_vkContext.get(), (void*)m_swapchain.get(),
            (void*)m_pipeline, (void*)m_linePipeline, (void*)m_pipelineLayout, (void*)m_renderPass);
        return false;
    }

    auto device = m_vkContext->getDevice();
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
    utils::math::Mat4f mvp = m_camera.getViewProjectionMatrix(aspect);

    // 1. Draw CAD Reference Grid and XYZ Origin Triad (LINE_LIST)
    if (m_showGrid && m_gridMesh && m_gridMesh->getVertexCount() > 0)
    {
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_linePipeline);
        VkBuffer vertexBuffers[] = {m_gridMesh->getVertexBuffer()};
        VkDeviceSize offsets[] = {0};
        vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);
        vkCmdPushConstants(commandBuffer, m_pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0,
                           sizeof(utils::math::Mat4f), &mvp);
        vkCmdDraw(commandBuffer, m_gridMesh->getVertexCount(), 1, 0, 0);
    }

    // 2. Draw 3D Solid CAD Part (TRIANGLE_LIST)
    if (m_showSolid && m_solidMesh && m_solidMesh->getVertexCount() > 0)
    {
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline);
        VkBuffer vertexBuffers[] = {m_solidMesh->getVertexBuffer()};
        VkDeviceSize offsets[] = {0};
        vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);

        if (m_solidMesh->hasIndices())
        {
            vkCmdBindIndexBuffer(commandBuffer, m_solidMesh->getIndexBuffer(), 0,
                                 VK_INDEX_TYPE_UINT32);
            vkCmdPushConstants(commandBuffer, m_pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0,
                               sizeof(utils::math::Mat4f), &mvp);
            vkCmdDrawIndexed(commandBuffer, m_solidMesh->getIndexCount(), 1, 0, 0, 0);
        }
        else
        {
            vkCmdPushConstants(commandBuffer, m_pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0,
                               sizeof(utils::math::Mat4f), &mvp);
            vkCmdDraw(commandBuffer, m_solidMesh->getVertexCount(), 1, 0, 0);
        }
    }

    // 3. Draw Wireframe Feature Lines (LINE_LIST)
    if (m_showWireframe && m_wireframeMesh && m_wireframeMesh->getVertexCount() > 0)
    {
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_linePipeline);
        VkBuffer vertexBuffers[] = {m_wireframeMesh->getVertexBuffer()};
        VkDeviceSize offsets[] = {0};
        vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);
        vkCmdPushConstants(commandBuffer, m_pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0,
                           sizeof(utils::math::Mat4f), &mvp);
        vkCmdDraw(commandBuffer, m_wireframeMesh->getVertexCount(), 1, 0, 0);
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
