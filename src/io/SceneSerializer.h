#pragma once

#include <android/log.h>

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace io
{

class SceneSerializer
{
   public:
    static constexpr uint32_t MCAD_MAGIC = 0x4441434D;  // 'M', 'C', 'A', 'D'
    static constexpr uint32_t MCAD_VERSION = 2;

    template <typename SceneObjectType>
    static bool saveWorkspace(const std::string& filepath,
                              const std::vector<SceneObjectType>& objects)
    {
        try
        {
            std::filesystem::path p(filepath);
            if (p.has_parent_path())
            {
                std::filesystem::create_directories(p.parent_path());
            }
        }
        catch (const std::exception& e)
        {
            __android_log_print(ANDROID_LOG_ERROR, "MobileCAD", "Failed to create directory: %s",
                                e.what());
        }

        std::ofstream file(filepath, std::ios::binary | std::ios::trunc);
        if (!file.is_open())
        {
            __android_log_print(ANDROID_LOG_ERROR, "MobileCAD",
                                "SceneSerializer: Cannot open %s for saving", filepath.c_str());
            return false;
        }

        // 1. Header
        file.write(reinterpret_cast<const char*>(&MCAD_MAGIC), sizeof(MCAD_MAGIC));
        file.write(reinterpret_cast<const char*>(&MCAD_VERSION), sizeof(MCAD_VERSION));

        // 2. Object Count
        uint32_t objCount = static_cast<uint32_t>(objects.size());
        file.write(reinterpret_cast<const char*>(&objCount), sizeof(objCount));

        // 3. Serialize each object
        for (const auto& obj : objects)
        {
            // Name
            uint32_t nameLen = static_cast<uint32_t>(obj.name.size());
            file.write(reinterpret_cast<const char*>(&nameLen), sizeof(nameLen));
            if (nameLen > 0)
            {
                file.write(obj.name.data(), nameLen);
            }

            // Primitive Type
            int32_t primType = static_cast<int32_t>(obj.primitiveType);
            file.write(reinterpret_cast<const char*>(&primType), sizeof(primType));

            // Params (Field-by-field for binary stability)
            file.write(reinterpret_cast<const char*>(&obj.params.width), sizeof(float));
            file.write(reinterpret_cast<const char*>(&obj.params.height), sizeof(float));
            file.write(reinterpret_cast<const char*>(&obj.params.depth), sizeof(float));
            file.write(reinterpret_cast<const char*>(&obj.params.radius), sizeof(float));
            file.write(reinterpret_cast<const char*>(&obj.params.holeRadius), sizeof(float));
            int32_t segs = static_cast<int32_t>(obj.params.segments);
            file.write(reinterpret_cast<const char*>(&segs), sizeof(int32_t));
            int32_t csg = static_cast<int32_t>(obj.params.csgMode);
            file.write(reinterpret_cast<const char*>(&csg), sizeof(int32_t));
            file.write(reinterpret_cast<const char*>(&obj.params.bevelWidth), sizeof(float));
            int32_t bSegs = static_cast<int32_t>(obj.params.bevelSegments);
            file.write(reinterpret_cast<const char*>(&bSegs), sizeof(int32_t));
            file.write(reinterpret_cast<const char*>(&obj.params.revolveAngle), sizeof(float));
            int32_t rSegs = static_cast<int32_t>(obj.params.revolveSegments);
            file.write(reinterpret_cast<const char*>(&rSegs), sizeof(int32_t));
            file.write(reinterpret_cast<const char*>(&obj.params.revolveOffset), sizeof(float));

            // Position, Rotation, Scale
            file.write(reinterpret_cast<const char*>(&obj.position.x), sizeof(float));
            file.write(reinterpret_cast<const char*>(&obj.position.y), sizeof(float));
            file.write(reinterpret_cast<const char*>(&obj.position.z), sizeof(float));

            file.write(reinterpret_cast<const char*>(&obj.rotationDeg.x), sizeof(float));
            file.write(reinterpret_cast<const char*>(&obj.rotationDeg.y), sizeof(float));
            file.write(reinterpret_cast<const char*>(&obj.rotationDeg.z), sizeof(float));

            file.write(reinterpret_cast<const char*>(&obj.scale.x), sizeof(float));
            file.write(reinterpret_cast<const char*>(&obj.scale.y), sizeof(float));
            file.write(reinterpret_cast<const char*>(&obj.scale.z), sizeof(float));

            // Flags
            uint8_t flags[2] = {static_cast<uint8_t>(obj.isSelected ? 1 : 0),
                                static_cast<uint8_t>(obj.isNegative ? 1 : 0)};
            file.write(reinterpret_cast<const char*>(flags), 2);

            // Material properties (MCAD_VERSION >= 2)
            int32_t matPreset = static_cast<int32_t>(obj.materialPreset);
            file.write(reinterpret_cast<const char*>(&matPreset), sizeof(matPreset));
            file.write(reinterpret_cast<const char*>(&obj.materialColor.x), sizeof(float));
            file.write(reinterpret_cast<const char*>(&obj.materialColor.y), sizeof(float));
            file.write(reinterpret_cast<const char*>(&obj.materialColor.z), sizeof(float));
            file.write(reinterpret_cast<const char*>(&obj.metallic), sizeof(float));
            file.write(reinterpret_cast<const char*>(&obj.roughness), sizeof(float));

            // AABB
            file.write(reinterpret_cast<const char*>(&obj.aabbMin.x), sizeof(float));
            file.write(reinterpret_cast<const char*>(&obj.aabbMin.y), sizeof(float));
            file.write(reinterpret_cast<const char*>(&obj.aabbMin.z), sizeof(float));

            file.write(reinterpret_cast<const char*>(&obj.aabbMax.x), sizeof(float));
            file.write(reinterpret_cast<const char*>(&obj.aabbMax.y), sizeof(float));
            file.write(reinterpret_cast<const char*>(&obj.aabbMax.z), sizeof(float));

            // Sketch points
            uint32_t spCount = static_cast<uint32_t>(obj.sketchPoints.size());
            file.write(reinterpret_cast<const char*>(&spCount), sizeof(spCount));
            for (const auto& sp : obj.sketchPoints)
            {
                file.write(reinterpret_cast<const char*>(&sp.x), sizeof(float));
                file.write(reinterpret_cast<const char*>(&sp.y), sizeof(float));
                file.write(reinterpret_cast<const char*>(&sp.z), sizeof(float));
            }

            // CPU Vertices
            uint32_t vCount = static_cast<uint32_t>(obj.cpuVertices.size());
            file.write(reinterpret_cast<const char*>(&vCount), sizeof(vCount));
            if (vCount > 0)
            {
                file.write(reinterpret_cast<const char*>(obj.cpuVertices.data()),
                           vCount * sizeof(obj.cpuVertices[0]));
            }

            // CPU Indices
            uint32_t idxCount = static_cast<uint32_t>(obj.cpuIndices.size());
            file.write(reinterpret_cast<const char*>(&idxCount), sizeof(idxCount));
            if (idxCount > 0)
            {
                file.write(reinterpret_cast<const char*>(obj.cpuIndices.data()),
                           idxCount * sizeof(uint32_t));
            }
        }

        file.close();
        __android_log_print(ANDROID_LOG_INFO, "MobileCAD",
                            "SceneSerializer: Saved %u objects to %s", objCount, filepath.c_str());
        return true;
    }

    template <typename SceneObjectType>
    static bool loadWorkspace(const std::string& filepath, std::vector<SceneObjectType>& outObjects)
    {
        std::ifstream file(filepath, std::ios::binary);
        if (!file.is_open())
        {
            __android_log_print(ANDROID_LOG_ERROR, "MobileCAD",
                                "SceneSerializer: Cannot open %s for loading", filepath.c_str());
            return false;
        }

        uint32_t magic = 0;
        uint32_t version = 0;
        file.read(reinterpret_cast<char*>(&magic), sizeof(magic));
        file.read(reinterpret_cast<char*>(&version), sizeof(version));

        if (magic != MCAD_MAGIC || (version != 1 && version != MCAD_VERSION))
        {
            __android_log_print(ANDROID_LOG_ERROR, "MobileCAD",
                                "SceneSerializer: Invalid MCAD file (magic=0x%08X ver=%u)", magic,
                                version);
            return false;
        }

        uint32_t objCount = 0;
        file.read(reinterpret_cast<char*>(&objCount), sizeof(objCount));

        std::vector<SceneObjectType> loadedObjects;
        loadedObjects.reserve(objCount);

        for (uint32_t i = 0; i < objCount; ++i)
        {
            SceneObjectType obj;

            // Name
            uint32_t nameLen = 0;
            file.read(reinterpret_cast<char*>(&nameLen), sizeof(nameLen));
            if (nameLen > 0 && nameLen < 4096)
            {
                obj.name.resize(nameLen);
                file.read(&obj.name[0], nameLen);
            }

            // Primitive Type
            int32_t primType = 0;
            file.read(reinterpret_cast<char*>(&primType), sizeof(primType));
            obj.primitiveType = static_cast<decltype(obj.primitiveType)>(primType);

            // Params
            file.read(reinterpret_cast<char*>(&obj.params.width), sizeof(float));
            file.read(reinterpret_cast<char*>(&obj.params.height), sizeof(float));
            file.read(reinterpret_cast<char*>(&obj.params.depth), sizeof(float));
            file.read(reinterpret_cast<char*>(&obj.params.radius), sizeof(float));
            file.read(reinterpret_cast<char*>(&obj.params.holeRadius), sizeof(float));
            int32_t segs = 32;
            file.read(reinterpret_cast<char*>(&segs), sizeof(int32_t));
            obj.params.segments = segs;
            int32_t csg = 0;
            file.read(reinterpret_cast<char*>(&csg), sizeof(int32_t));
            obj.params.csgMode = csg;
            file.read(reinterpret_cast<char*>(&obj.params.bevelWidth), sizeof(float));
            int32_t bSegs = 1;
            file.read(reinterpret_cast<char*>(&bSegs), sizeof(int32_t));
            obj.params.bevelSegments = bSegs;
            file.read(reinterpret_cast<char*>(&obj.params.revolveAngle), sizeof(float));
            int32_t rSegs = 36;
            file.read(reinterpret_cast<char*>(&rSegs), sizeof(int32_t));
            obj.params.revolveSegments = rSegs;
            file.read(reinterpret_cast<char*>(&obj.params.revolveOffset), sizeof(float));

            // Position, Rotation, Scale
            file.read(reinterpret_cast<char*>(&obj.position.x), sizeof(float));
            file.read(reinterpret_cast<char*>(&obj.position.y), sizeof(float));
            file.read(reinterpret_cast<char*>(&obj.position.z), sizeof(float));

            file.read(reinterpret_cast<char*>(&obj.rotationDeg.x), sizeof(float));
            file.read(reinterpret_cast<char*>(&obj.rotationDeg.y), sizeof(float));
            file.read(reinterpret_cast<char*>(&obj.rotationDeg.z), sizeof(float));

            file.read(reinterpret_cast<char*>(&obj.scale.x), sizeof(float));
            file.read(reinterpret_cast<char*>(&obj.scale.y), sizeof(float));
            file.read(reinterpret_cast<char*>(&obj.scale.z), sizeof(float));

            // Flags
            uint8_t flags[2] = {0, 0};
            file.read(reinterpret_cast<char*>(flags), 2);
            obj.isSelected = (flags[0] != 0);
            obj.isNegative = (flags[1] != 0);

            // Material properties (MCAD_VERSION >= 2)
            if (version >= 2)
            {
                int32_t matPreset = 0;
                file.read(reinterpret_cast<char*>(&matPreset), sizeof(matPreset));
                obj.materialPreset = static_cast<int>(matPreset);
                file.read(reinterpret_cast<char*>(&obj.materialColor.x), sizeof(float));
                file.read(reinterpret_cast<char*>(&obj.materialColor.y), sizeof(float));
                file.read(reinterpret_cast<char*>(&obj.materialColor.z), sizeof(float));
                file.read(reinterpret_cast<char*>(&obj.metallic), sizeof(float));
                file.read(reinterpret_cast<char*>(&obj.roughness), sizeof(float));
            }
            else
            {
                obj.materialPreset = 0;
                obj.materialColor = {0.75f, 0.78f, 0.82f};
                obj.metallic = 0.35f;
                obj.roughness = 0.40f;
            }

            // AABB
            file.read(reinterpret_cast<char*>(&obj.aabbMin.x), sizeof(float));
            file.read(reinterpret_cast<char*>(&obj.aabbMin.y), sizeof(float));
            file.read(reinterpret_cast<char*>(&obj.aabbMin.z), sizeof(float));

            file.read(reinterpret_cast<char*>(&obj.aabbMax.x), sizeof(float));
            file.read(reinterpret_cast<char*>(&obj.aabbMax.y), sizeof(float));
            file.read(reinterpret_cast<char*>(&obj.aabbMax.z), sizeof(float));

            // Sketch points
            uint32_t spCount = 0;
            file.read(reinterpret_cast<char*>(&spCount), sizeof(spCount));
            if (spCount > 0 && spCount < 1000000)
            {
                obj.sketchPoints.resize(spCount);
                for (size_t s = 0; s < spCount; ++s)
                {
                    file.read(reinterpret_cast<char*>(&obj.sketchPoints[s].x), sizeof(float));
                    file.read(reinterpret_cast<char*>(&obj.sketchPoints[s].y), sizeof(float));
                    file.read(reinterpret_cast<char*>(&obj.sketchPoints[s].z), sizeof(float));
                }
            }

            // CPU Vertices
            uint32_t vCount = 0;
            file.read(reinterpret_cast<char*>(&vCount), sizeof(vCount));
            if (vCount > 0 && vCount < 5000000)
            {
                obj.cpuVertices.resize(vCount);
                file.read(reinterpret_cast<char*>(obj.cpuVertices.data()),
                          vCount * sizeof(obj.cpuVertices[0]));
            }

            // CPU Indices
            uint32_t idxCount = 0;
            file.read(reinterpret_cast<char*>(&idxCount), sizeof(idxCount));
            if (idxCount > 0 && idxCount < 15000000)
            {
                obj.cpuIndices.resize(idxCount);
                file.read(reinterpret_cast<char*>(obj.cpuIndices.data()),
                          idxCount * sizeof(uint32_t));
            }

            loadedObjects.push_back(std::move(obj));
        }

        file.close();
        outObjects = std::move(loadedObjects);
        __android_log_print(ANDROID_LOG_INFO, "MobileCAD",
                            "SceneSerializer: Loaded %u objects from %s", objCount,
                            filepath.c_str());
        return true;
    }
};

}  // namespace io
