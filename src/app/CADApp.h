#pragma once
#include <android/native_window.h>

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "geometry/Primitives.h"
#include "io/MeshExporter.h"
#include "rendering/Camera.h"
#include "rendering/MeshBuffer.h"
#include "rendering/Pipeline.h"
#include "rendering/Swapchain.h"
#include "rendering/VulkanContext.h"
#include "utils/math/Ray.h"

namespace app
{

using Vertex = rendering::Vertex;

enum class PrimitiveType
{
    Box,
    Cylinder,
    Sphere
};

struct PrimitiveParams
{
    float width{1.6f};
    float height{1.2f};
    float depth{1.6f};
    float radius{0.6f};
    float holeRadius{0.3f};
    int segments{32};
    int csgMode{0};  // 0 = Solid, 1 = Drill Hole, 2 = Union, 3 = Intersect, 4 = Hollow Pipe
};

struct SceneObject
{
    std::string name{"Part_1"};
    PrimitiveType primitiveType{PrimitiveType::Box};
    PrimitiveParams params;
    utils::math::Vec3f position{0.0f, 0.0f, 0.0f};
    utils::math::Vec3f rotationDeg{0.0f, 0.0f, 0.0f};

    std::unique_ptr<rendering::MeshBuffer> solidMesh;
    std::unique_ptr<rendering::MeshBuffer> wireframeMesh;
    std::unique_ptr<rendering::MeshBuffer> selectedWireframeMesh;

    std::vector<Vertex> cpuVertices;
    std::vector<uint32_t> cpuIndices;
    utils::math::Vec3f aabbMin{-0.8f, -0.6f, -0.8f};
    utils::math::Vec3f aabbMax{0.8f, 0.6f, 0.8f};

    SceneObject() = default;
    SceneObject(SceneObject&&) noexcept = default;
    SceneObject& operator=(SceneObject&&) noexcept = default;

    SceneObject(const SceneObject&) = delete;
    SceneObject& operator=(const SceneObject&) = delete;

    utils::math::Mat4f getModelMatrix() const
    {
        constexpr float degToRad = 3.14159265358979323846f / 180.0f;
        return utils::math::Mat4f::translation(position) *
               utils::math::Mat4f::rotationY(rotationDeg.y * degToRad) *
               utils::math::Mat4f::rotationX(rotationDeg.x * degToRad) *
               utils::math::Mat4f::rotationZ(rotationDeg.z * degToRad);
    }

    void updateAABB()
    {
        if (cpuVertices.empty())
        {
            aabbMin = {-0.5f, -0.5f, -0.5f};
            aabbMax = {0.5f, 0.5f, 0.5f};
            return;
        }
        aabbMin = {cpuVertices[0].position[0], cpuVertices[0].position[1],
                   cpuVertices[0].position[2]};
        aabbMax = aabbMin;
        for (const auto& v : cpuVertices)
        {
            aabbMin.x = std::min(aabbMin.x, v.position[0]);
            aabbMin.y = std::min(aabbMin.y, v.position[1]);
            aabbMin.z = std::min(aabbMin.z, v.position[2]);
            aabbMax.x = std::max(aabbMax.x, v.position[0]);
            aabbMax.y = std::max(aabbMax.y, v.position[1]);
            aabbMax.z = std::max(aabbMax.z, v.position[2]);
        }
    }
};

class CADApp
{
   public:
    CADApp(ANativeWindow* window);
    ~CADApp();

    // Non-copyable, non-movable
    CADApp(const CADApp&) = delete;
    CADApp& operator=(const CADApp&) = delete;

    // NDK lifecycle integration
    void init(ANativeWindow* window);
    bool drawFrame();  // returns false on device-loss → caller should exit
    void cleanup();
    void onWindowResized(ANativeWindow* window, int32_t width, int32_t height);
    void onTouchEvent(int32_t action, int32_t pointerCount, float x0, float y0, float x1, float y1);

    // CAD Scene & Parametric Modeling controls
    void setPrimitive(PrimitiveType type);
    void rebuildGeometry();
    void rebuildObjectGeometry(size_t index);
    bool exportSTL();
    void setExportPath(const std::string& path) { m_exportPath = path; }
    const std::string& getExportPath() const { return m_exportPath; }
    const std::string& getExportStatus() const { return m_exportStatus; }

    std::vector<SceneObject>& getObjects() { return m_objects; }
    const std::vector<SceneObject>& getObjects() const { return m_objects; }
    int getSelectedIndex() const { return m_selectedIndex; }
    void setSelectedIndex(int index) { m_selectedIndex = index; }

    PrimitiveParams& getParams()
    {
        if (m_selectedIndex >= 0 && m_selectedIndex < static_cast<int>(m_objects.size()))
            return m_objects[m_selectedIndex].params;
        static PrimitiveParams fallback;
        return fallback;
    }
    const PrimitiveParams& getParams() const
    {
        if (m_selectedIndex >= 0 && m_selectedIndex < static_cast<int>(m_objects.size()))
            return m_objects[m_selectedIndex].params;
        static PrimitiveParams fallback;
        return fallback;
    }

    void setExitCallback(std::function<void()> cb) { m_exitCallback = std::move(cb); }

    bool isVsyncEnabled() const { return m_vsync; }
    void setVsync(bool enabled)
    {
        if (m_vsync != enabled)
        {
            m_vsync = enabled;
            m_vsyncChanged = true;
        }
    }

    rendering::Camera& getCamera() { return m_camera; }
    const rendering::Camera& getCamera() const { return m_camera; }

    bool isInitialized() const { return m_initialized; }
    bool isDeviceLost() const { return m_deviceLost; }

    // Diagnostic / main.cpp accessors (const, no side effects)
    rendering::VulkanContext* getVkContextPtr() const { return m_vkContext.get(); }
    rendering::Swapchain* getSwapchainPtr() const { return m_swapchain.get(); }
    uint32_t getSwapchainExtentW() const
    {
        return m_swapchain ? m_swapchain->getExtent().width : 0;
    }
    uint32_t getSwapchainExtentH() const
    {
        return m_swapchain ? m_swapchain->getExtent().height : 0;
    }

   private:
    bool m_initialized{false};
    bool m_deviceLost{false};

    std::unique_ptr<rendering::VulkanContext> m_vkContext;
    std::unique_ptr<rendering::Swapchain> m_swapchain;
    rendering::Camera m_camera;

    // Touch gesture tracking
    float m_lastTouchX{0.0f};
    float m_lastTouchY{0.0f};
    float m_downX{0.0f};
    float m_downY{0.0f};
    float m_maxDragDist{0.0f};
    float m_lastPinchDist{0.0f};
    float m_lastMidX{0.0f};
    float m_lastMidY{0.0f};
    int32_t m_activePointers{0};

    // Resources
    VkPipelineLayout m_pipelineLayout{VK_NULL_HANDLE};
    VkRenderPass m_renderPass{VK_NULL_HANDLE};
    VkPipeline m_pipeline{VK_NULL_HANDLE};      // Solid triangle pipeline
    VkPipeline m_linePipeline{VK_NULL_HANDLE};  // Grid / axis / wireframe line pipeline
    std::vector<VkFramebuffer> m_swapchainFramebuffers;

    // Dear ImGui resources & state
    VkDescriptorPool m_imguiDescriptorPool{VK_NULL_HANDLE};
    bool m_showGrid{true};
    bool m_showSolid{true};
    bool m_showWireframe{false};
    bool m_vsync{true};
    bool m_vsyncChanged{false};

    // Export state
    std::string m_exportPath{"/data/data/com.mobilecad/files/part_export.stl"};
    std::string m_exportStatus;

    // Lifecycle exit callback
    std::function<void()> m_exitCallback;

    // Scene geometry & Multi-object scene graph
    std::vector<SceneObject> m_objects;
    int m_selectedIndex{0};
    std::unique_ptr<rendering::MeshBuffer> m_gridMesh;

    // Sync per-frame
    uint32_t m_currentFrame{0};
    uint32_t m_drawFrameCount{0};

    // Initialization
    void initSwapchain();
    void initRenderPass();
    void initPipelineLayout();
    void initGraphicsPipeline();
    void initFramebuffers();
    void initSceneGeometry();
    void initImGui(ANativeWindow* window);

    // Cleanup
    void cleanupSwapchain();

    // UI & Rendering
    void renderImGui();
    void recordCommandBuffer(VkCommandBuffer commandBuffer, uint32_t imageIndex);
};

}  // namespace app
