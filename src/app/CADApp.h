#pragma once
#include <android/native_window.h>

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <tuple>
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
    Sphere,
    Extrude,
    Revolve
};

enum class SketchTool : int
{
    Polyline = 0,
    Freehand = 1,
    SmoothCurve = 2,
    Wave = 3,
    Circle = 4,
    Ellipse = 5,
    Square = 6,
    Rectangle = 7,
    Triangle = 8
};

struct PrimitiveParams
{
    float width{1.6f};
    float height{1.2f};
    float depth{1.6f};
    float radius{0.6f};
    float holeRadius{0.3f};
    int segments{32};
    int csgMode{
        0};  // 0 = Solid, 1 = Drill Hole, 2 = Union, 3 = Intersect, 4 = Hollow Pipe, -1 = Baked CSG
    float bevelWidth{0.0f};
    int bevelSegments{1};
    float revolveAngle{360.0f};
    int revolveSegments{36};
    float revolveOffset{0.35f};
};

struct SceneObject
{
    std::string name{"Part_1"};
    PrimitiveType primitiveType{PrimitiveType::Box};
    PrimitiveParams params;
    std::vector<utils::math::Vec3f> sketchPoints;
    utils::math::Vec3f position{0.0f, 0.0f, 0.0f};
    utils::math::Vec3f rotationDeg{0.0f, 0.0f, 0.0f};
    utils::math::Vec3f scale{1.0f, 1.0f, 1.0f};
    bool isSelected{false};
    bool isNegative{false};
    int materialPreset{0};
    utils::math::Vec3f materialColor{0.75f, 0.78f, 0.82f};
    float metallic{0.35f};
    float roughness{0.40f};
    manifold::Manifold localManifold;

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
               utils::math::Mat4f::rotationX(rotationDeg.x * degToRad) *
               utils::math::Mat4f::rotationY(rotationDeg.y * degToRad) *
               utils::math::Mat4f::rotationZ(rotationDeg.z * degToRad) *
               utils::math::Mat4f::scaling(scale);
    }

    manifold::Manifold buildManifoldFromMesh() const
    {
        if (cpuVertices.empty() || cpuIndices.empty())
            return manifold::Manifold();

        std::map<std::tuple<int64_t, int64_t, int64_t>, uint32_t> coordMap;
        std::vector<uint32_t> remap(cpuVertices.size());
        std::vector<float> weldedVerts;

        for (size_t i = 0; i < cpuVertices.size(); ++i)
        {
            const auto& p = cpuVertices[i].position;
            int64_t qx = static_cast<int64_t>(std::round(p[0] * 100000.0f));
            int64_t qy = static_cast<int64_t>(std::round(p[1] * 100000.0f));
            int64_t qz = static_cast<int64_t>(std::round(p[2] * 100000.0f));
            auto key = std::make_tuple(qx, qy, qz);
            auto it = coordMap.find(key);
            if (it == coordMap.end())
            {
                uint32_t newIdx = static_cast<uint32_t>(weldedVerts.size() / 3);
                coordMap[key] = newIdx;
                remap[i] = newIdx;
                weldedVerts.push_back(p[0]);
                weldedVerts.push_back(p[1]);
                weldedVerts.push_back(p[2]);
            }
            else
            {
                remap[i] = it->second;
            }
        }

        std::vector<uint32_t> weldedIndices;
        weldedIndices.reserve(cpuIndices.size());
        for (size_t i = 0; i + 2 < cpuIndices.size(); i += 3)
        {
            uint32_t i0 = remap[cpuIndices[i]];
            uint32_t i1 = remap[cpuIndices[i + 1]];
            uint32_t i2 = remap[cpuIndices[i + 2]];
            if (i0 != i1 && i1 != i2 && i2 != i0)
            {
                weldedIndices.push_back(i0);
                weldedIndices.push_back(i1);
                weldedIndices.push_back(i2);
            }
        }

        manifold::MeshGL gl;
        gl.numProp = 3;
        gl.vertProperties = std::move(weldedVerts);
        gl.triVerts = std::move(weldedIndices);
        manifold::Manifold result(gl);
        return result;
    }

    manifold::Manifold getWorldManifold() const
    {
        manifold::Manifold m = localManifold;
        if (m.IsEmpty())
        {
            m = buildManifoldFromMesh();
        }
        m = m.Scale({scale.x, scale.y, scale.z});
        if (std::abs(rotationDeg.z) > 1e-4f)
            m = m.Rotate(0.0, 0.0, rotationDeg.z);
        if (std::abs(rotationDeg.y) > 1e-4f)
            m = m.Rotate(0.0, rotationDeg.y, 0.0);
        if (std::abs(rotationDeg.x) > 1e-4f)
            m = m.Rotate(rotationDeg.x, 0.0, 0.0);
        m = m.Translate({position.x, position.y, position.z});
        return m;
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

struct SceneObjectSnapshot
{
    std::string name;
    PrimitiveType primitiveType{PrimitiveType::Box};
    PrimitiveParams params;
    std::vector<utils::math::Vec3f> sketchPoints;
    utils::math::Vec3f position;
    utils::math::Vec3f rotationDeg;
    utils::math::Vec3f scale;
    bool isSelected{false};
    bool isNegative{false};
    int materialPreset{0};
    utils::math::Vec3f materialColor{0.75f, 0.78f, 0.82f};
    float metallic{0.35f};
    float roughness{0.40f};
    utils::math::Vec3f aabbMin;
    utils::math::Vec3f aabbMax;
    std::vector<Vertex> cpuVertices;
    std::vector<uint32_t> cpuIndices;
};

struct HistoryState
{
    std::vector<SceneObjectSnapshot> objects;
    int selectedIndex{-1};
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
    bool exportOBJ();
    bool saveWorkspace();
    bool loadWorkspace();
    bool importMeshFile(const std::string& filepath);
    void refreshDiscoveredFiles();
    void setExportPath(const std::string& path) { m_exportPath = path; }
    const std::string& getExportPath() const { return m_exportPath; }
    const std::string& getExportStatus() const { return m_exportStatus; }
    const std::string& getProjectStatus() const { return m_projectStatus; }
    const std::string& getImportStatus() const { return m_importStatus; }

    // 2D Sketch & Extrude / Revolve controls
    void rebuildSketchMesh();
    void undoSketchPoint();
    void clearSketch();
    void extrudeSketchToPart();
    void revolveSketchToPart();
    bool isSketchMode() const { return m_sketchMode; }
    void setSketchMode(bool enabled) { m_sketchMode = enabled; }

    // History & Undo / Redo controls
    HistoryState captureCurrentState() const;
    void restoreState(const HistoryState& state);
    void pushUndoState();
    void undo();
    void redo();
    size_t getUndoCount() const { return m_undoStack.size(); }
    size_t getRedoCount() const { return m_redoStack.size(); }

    // 3D Point-to-Point Caliper & Measurement controls
    bool isMeasureMode() const { return m_measureMode; }
    void setMeasureMode(bool enabled) { m_measureMode = enabled; }
    void clearMeasurement()
    {
        m_measurePointCount = 0;
        m_measurePtA = {0.0f, 0.0f, 0.0f};
        m_measurePtB = {0.0f, 0.0f, 0.0f};
    }

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
    VkPipeline m_pipeline{VK_NULL_HANDLE};       // Solid triangle pipeline (opaque +ve)
    VkPipeline m_ghostPipeline{VK_NULL_HANDLE};  // Transparent ghost pipeline (-ve cutters)
    VkPipeline m_linePipeline{VK_NULL_HANDLE};   // Grid / axis / wireframe line pipeline
    std::vector<VkFramebuffer> m_swapchainFramebuffers;

    // Dear ImGui resources & state
    VkDescriptorPool m_imguiDescriptorPool{VK_NULL_HANDLE};
    bool m_showGrid{true};
    bool m_showSolid{true};
    bool m_showWireframe{false};
    bool m_vsync{true};
    bool m_vsyncChanged{false};

    // Export & Workspace state
    std::string m_exportPath{"/data/data/com.mobilecad/files/part_export.stl"};
    std::string m_exportStatus;
    std::string m_projectStatus;
    std::string m_importStatus;
    std::vector<std::string> m_discoveredMeshFiles;
    int m_selectedMeshFileIdx{0};

    // Lifecycle exit callback
    std::function<void()> m_exitCallback;

    // UI toolbar state
    bool m_showMenu{true};
    bool m_fullWidth{true};

    // Scene geometry & Multi-object scene graph
    std::vector<SceneObject> m_objects;
    int m_selectedIndex{0};
    bool m_multiSelectTapMode{false};
    std::string m_csgStatus;
    std::unique_ptr<rendering::MeshBuffer> m_gridMesh;

    // CSG operations
    void applyCsgSubtract();
    void applyUnionOrGroup();

    // 2D Sketch & Extrude / Revolve state
    bool m_sketchMode{false};
    bool m_snapToGrid{true};
    SketchTool m_sketchTool{SketchTool::Polyline};
    float m_sketchDimX{1.6f};
    float m_sketchDimZ{1.2f};
    int m_sketchSegments{32};
    float m_extrudeHeight{1.2f};
    float m_bevelWidth{0.0f};
    int m_bevelSegments{1};
    float m_revolveAngle{360.0f};
    int m_revolveSegments{36};
    float m_revolveOffset{0.35f};
    std::vector<utils::math::Vec3f> m_sketchPoints;
    std::unique_ptr<rendering::MeshBuffer> m_sketchLineMesh;

    // History state
    static constexpr size_t kMaxHistory = 20;
    std::vector<HistoryState> m_undoStack;
    std::vector<HistoryState> m_redoStack;

    // 3D Caliper & Measurement state
    bool m_measureMode{false};
    bool m_measureSnapVertex{true};
    int m_measurePointCount{0};
    utils::math::Vec3f m_measurePtA{0.0f, 0.0f, 0.0f};
    utils::math::Vec3f m_measurePtB{0.0f, 0.0f, 0.0f};
    void renderCaliperOverlay();

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

    // Direct Numeric & Keypad helpers
    bool drawNumericControl(const char* label, float* value, float step, float minVal, float maxVal,
                            const char* format = "%.2f", std::function<void()> onChanged = nullptr);
    bool drawVec3Control(const char* label, utils::math::Vec3f* vec, float step, float minVal,
                         float maxVal, float resetVal = 0.0f,
                         std::function<void()> onChanged = nullptr);
    void openNumpad(const char* title, float* target, float minVal, float maxVal, float defaultVal,
                    const char* format = "%.2f", std::function<void()> onChanged = nullptr);
    void renderNumpadPopup();
    void renderCompassGizmo();

    // On-screen numeric keypad state
    struct NumpadState
    {
        std::string title;
        std::string inputBuffer;
        float* targetValue{nullptr};
        float minVal{-1e9f};
        float maxVal{1e9f};
        float defaultVal{0.0f};
        std::function<void()> onChanged;
    };
    NumpadState m_numpad;
    bool m_numpadNeedOpen{false};
};

}  // namespace app
