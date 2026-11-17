#pragma once

#include <android/log.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

#include "rendering/MeshBuffer.h"
#include "utils/math/Mat4.h"
#include "utils/math/Vec3.h"

namespace io
{

class ArchFormat
{
   public:
    static constexpr char MCADX_MAGIC[8] = {'M', 'C', 'A', 'D', 'X', '1', '0', '0'};
    static constexpr uint32_t MCADX_VERSION = 1;

    // Fast CRC32 table calculation
    static uint32_t calculateCRC32(const uint8_t* data, size_t length)
    {
        uint32_t crc = 0xFFFFFFFF;
        for (size_t i = 0; i < length; ++i)
        {
            crc ^= data[i];
            for (int j = 0; j < 8; ++j)
            {
                crc = (crc >> 1) ^ (0xEDB88320 & (-(crc & 1)));
            }
        }
        return ~crc;
    }

    // -------------------------------------------------------------
    // 1. .mcadx Super-Format: Binary + BIM Metadata Serializer
    // -------------------------------------------------------------
    template <typename SceneObjectType, typename CameraType>
    static bool saveMCADX(const std::string& filepath, const std::vector<SceneObjectType>& objects,
                          const std::string& projectName, const std::string& author,
                          const CameraType& camera)
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
            __android_log_print(ANDROID_LOG_ERROR, "MobileCAD",
                                "ArchFormat: create_directories failed: %s", e.what());
        }

        std::stringstream payload(std::ios::in | std::ios::out | std::ios::binary);

        // A. Metadata Chunk
        auto writeString = [](std::ostream& os, const std::string& str)
        {
            uint32_t len = static_cast<uint32_t>(str.size());
            os.write(reinterpret_cast<const char*>(&len), sizeof(len));
            if (len > 0)
                os.write(str.data(), len);
        };

        writeString(payload, projectName);
        writeString(payload, author);
        std::string units = "m";
        writeString(payload, units);
        float gridScale = 0.5f;
        payload.write(reinterpret_cast<const char*>(&gridScale), sizeof(gridScale));

        // Camera State
        payload.write(reinterpret_cast<const char*>(&camera.m_target.x), sizeof(float));
        payload.write(reinterpret_cast<const char*>(&camera.m_target.y), sizeof(float));
        payload.write(reinterpret_cast<const char*>(&camera.m_target.z), sizeof(float));
        payload.write(reinterpret_cast<const char*>(&camera.m_yaw), sizeof(float));
        payload.write(reinterpret_cast<const char*>(&camera.m_pitch), sizeof(float));
        payload.write(reinterpret_cast<const char*>(&camera.m_distance), sizeof(float));
        payload.write(reinterpret_cast<const char*>(&camera.m_fovY), sizeof(float));

        // B. CAD Layer Table
        struct LayerDef
        {
            std::string name;
            float r, g, b, a;
            float lineweight;
            uint8_t visible;
        };
        std::vector<LayerDef> layers = {{"0-DEFAULT", 0.85f, 0.85f, 0.85f, 1.0f, 0.25f, 1},
                                        {"A-WALL", 0.90f, 0.90f, 0.90f, 1.0f, 0.50f, 1},
                                        {"A-DOOR", 0.85f, 0.35f, 0.20f, 1.0f, 0.35f, 1},
                                        {"A-GLAZ", 0.35f, 0.75f, 0.95f, 1.0f, 0.25f, 1},
                                        {"A-STAIR", 0.70f, 0.72f, 0.75f, 1.0f, 0.35f, 1},
                                        {"A-ROOF", 0.80f, 0.40f, 0.25f, 1.0f, 0.35f, 1},
                                        {"A-DIMS", 0.30f, 0.85f, 0.45f, 1.0f, 0.18f, 1},
                                        {"M-MECH", 0.40f, 0.65f, 0.95f, 1.0f, 0.30f, 1}};

        uint32_t numLayers = static_cast<uint32_t>(layers.size());
        payload.write(reinterpret_cast<const char*>(&numLayers), sizeof(numLayers));
        for (const auto& l : layers)
        {
            writeString(payload, l.name);
            payload.write(reinterpret_cast<const char*>(&l.r), sizeof(float));
            payload.write(reinterpret_cast<const char*>(&l.g), sizeof(float));
            payload.write(reinterpret_cast<const char*>(&l.b), sizeof(float));
            payload.write(reinterpret_cast<const char*>(&l.a), sizeof(float));
            payload.write(reinterpret_cast<const char*>(&l.lineweight), sizeof(float));
            payload.write(reinterpret_cast<const char*>(&l.visible), sizeof(uint8_t));
        }

        // C. Scene Objects & BIM Data
        uint32_t objCount = static_cast<uint32_t>(objects.size());
        payload.write(reinterpret_cast<const char*>(&objCount), sizeof(objCount));

        for (const auto& obj : objects)
        {
            writeString(payload, obj.name);
            int32_t primType = static_cast<int32_t>(obj.primitiveType);
            payload.write(reinterpret_cast<const char*>(&primType), sizeof(primType));
            writeString(payload, obj.params.bimLayer);

            // Primitive & BIM Parameters
            payload.write(reinterpret_cast<const char*>(&obj.params.width), sizeof(float));
            payload.write(reinterpret_cast<const char*>(&obj.params.height), sizeof(float));
            payload.write(reinterpret_cast<const char*>(&obj.params.depth), sizeof(float));
            payload.write(reinterpret_cast<const char*>(&obj.params.radius), sizeof(float));
            payload.write(reinterpret_cast<const char*>(&obj.params.holeRadius), sizeof(float));
            int32_t segs = static_cast<int32_t>(obj.params.segments);
            payload.write(reinterpret_cast<const char*>(&segs), sizeof(int32_t));
            int32_t csg = static_cast<int32_t>(obj.params.csgMode);
            payload.write(reinterpret_cast<const char*>(&csg), sizeof(int32_t));
            payload.write(reinterpret_cast<const char*>(&obj.params.bevelWidth), sizeof(float));
            int32_t bSegs = static_cast<int32_t>(obj.params.bevelSegments);
            payload.write(reinterpret_cast<const char*>(&bSegs), sizeof(int32_t));
            payload.write(reinterpret_cast<const char*>(&obj.params.revolveAngle), sizeof(float));
            int32_t rSegs = static_cast<int32_t>(obj.params.revolveSegments);
            payload.write(reinterpret_cast<const char*>(&rSegs), sizeof(int32_t));
            payload.write(reinterpret_cast<const char*>(&obj.params.revolveOffset), sizeof(float));

            // BIM Parametric Properties
            payload.write(reinterpret_cast<const char*>(&obj.params.wallLength), sizeof(float));
            payload.write(reinterpret_cast<const char*>(&obj.params.wallThickness), sizeof(float));
            payload.write(reinterpret_cast<const char*>(&obj.params.wallHeight), sizeof(float));
            payload.write(reinterpret_cast<const char*>(&obj.params.doorWidth), sizeof(float));
            payload.write(reinterpret_cast<const char*>(&obj.params.doorHeight), sizeof(float));
            payload.write(reinterpret_cast<const char*>(&obj.params.doorSwingAngle), sizeof(float));
            payload.write(reinterpret_cast<const char*>(&obj.params.windowWidth), sizeof(float));
            payload.write(reinterpret_cast<const char*>(&obj.params.windowHeight), sizeof(float));
            payload.write(reinterpret_cast<const char*>(&obj.params.windowSillHeight),
                          sizeof(float));
            int32_t stairSteps = static_cast<int32_t>(obj.params.stairSteps);
            payload.write(reinterpret_cast<const char*>(&stairSteps), sizeof(int32_t));
            payload.write(reinterpret_cast<const char*>(&obj.params.stairRise), sizeof(float));
            payload.write(reinterpret_cast<const char*>(&obj.params.stairRun), sizeof(float));
            payload.write(reinterpret_cast<const char*>(&obj.params.stairWidth), sizeof(float));
            payload.write(reinterpret_cast<const char*>(&obj.params.roofWidth), sizeof(float));
            payload.write(reinterpret_cast<const char*>(&obj.params.roofLength), sizeof(float));
            payload.write(reinterpret_cast<const char*>(&obj.params.roofRidgeHeight),
                          sizeof(float));
            payload.write(reinterpret_cast<const char*>(&obj.params.roofOverhang), sizeof(float));

            // Transform
            payload.write(reinterpret_cast<const char*>(&obj.position.x), sizeof(float));
            payload.write(reinterpret_cast<const char*>(&obj.position.y), sizeof(float));
            payload.write(reinterpret_cast<const char*>(&obj.position.z), sizeof(float));
            payload.write(reinterpret_cast<const char*>(&obj.rotationDeg.x), sizeof(float));
            payload.write(reinterpret_cast<const char*>(&obj.rotationDeg.y), sizeof(float));
            payload.write(reinterpret_cast<const char*>(&obj.rotationDeg.z), sizeof(float));
            payload.write(reinterpret_cast<const char*>(&obj.scale.x), sizeof(float));
            payload.write(reinterpret_cast<const char*>(&obj.scale.y), sizeof(float));
            payload.write(reinterpret_cast<const char*>(&obj.scale.z), sizeof(float));

            // Flags
            uint8_t flags[2] = {static_cast<uint8_t>(obj.isSelected ? 1 : 0),
                                static_cast<uint8_t>(obj.isNegative ? 1 : 0)};
            payload.write(reinterpret_cast<const char*>(flags), 2);

            // PBR Material
            int32_t matPreset = static_cast<int32_t>(obj.materialPreset);
            payload.write(reinterpret_cast<const char*>(&matPreset), sizeof(matPreset));
            payload.write(reinterpret_cast<const char*>(&obj.materialColor.x), sizeof(float));
            payload.write(reinterpret_cast<const char*>(&obj.materialColor.y), sizeof(float));
            payload.write(reinterpret_cast<const char*>(&obj.materialColor.z), sizeof(float));
            payload.write(reinterpret_cast<const char*>(&obj.metallic), sizeof(float));
            payload.write(reinterpret_cast<const char*>(&obj.roughness), sizeof(float));

            // Per-Surface Materials
            uint32_t surfCount = static_cast<uint32_t>(obj.surfaceMaterials.size());
            payload.write(reinterpret_cast<const char*>(&surfCount), sizeof(surfCount));
            for (const auto& sm : obj.surfaceMaterials)
            {
                payload.write(reinterpret_cast<const char*>(&sm.color.x), sizeof(float));
                payload.write(reinterpret_cast<const char*>(&sm.color.y), sizeof(float));
                payload.write(reinterpret_cast<const char*>(&sm.color.z), sizeof(float));
                payload.write(reinterpret_cast<const char*>(&sm.metallic), sizeof(float));
                payload.write(reinterpret_cast<const char*>(&sm.roughness), sizeof(float));
                int32_t p = static_cast<int32_t>(sm.preset);
                payload.write(reinterpret_cast<const char*>(&p), sizeof(p));
            }

            // Sketch Points
            uint32_t ptCount = static_cast<uint32_t>(obj.sketchPoints.size());
            payload.write(reinterpret_cast<const char*>(&ptCount), sizeof(ptCount));
            for (const auto& pt : obj.sketchPoints)
            {
                payload.write(reinterpret_cast<const char*>(&pt.x), sizeof(float));
                payload.write(reinterpret_cast<const char*>(&pt.y), sizeof(float));
                payload.write(reinterpret_cast<const char*>(&pt.z), sizeof(float));
            }

            // CPU Vertices & Indices
            uint32_t vCount = static_cast<uint32_t>(obj.cpuVertices.size());
            payload.write(reinterpret_cast<const char*>(&vCount), sizeof(vCount));
            if (vCount > 0)
            {
                payload.write(reinterpret_cast<const char*>(obj.cpuVertices.data()),
                              vCount * sizeof(rendering::Vertex));
            }

            uint32_t idxCount = static_cast<uint32_t>(obj.cpuIndices.size());
            payload.write(reinterpret_cast<const char*>(&idxCount), sizeof(idxCount));
            if (idxCount > 0)
            {
                payload.write(reinterpret_cast<const char*>(obj.cpuIndices.data()),
                              idxCount * sizeof(uint32_t));
            }

            // 16-byte chunk alignment padding
            std::streampos curPos = payload.tellp();
            size_t rem = static_cast<size_t>(curPos) % 16;
            if (rem != 0)
            {
                size_t padBytes = 16 - rem;
                char pad[16] = {0};
                payload.write(pad, padBytes);
            }
        }

        std::string payloadBytes = payload.str();
        uint64_t payloadSize = static_cast<uint64_t>(payloadBytes.size());
        uint32_t crc32 = calculateCRC32(reinterpret_cast<const uint8_t*>(payloadBytes.data()),
                                        payloadBytes.size());

        std::ofstream file(filepath, std::ios::binary | std::ios::trunc);
        if (!file.is_open())
        {
            __android_log_print(ANDROID_LOG_ERROR, "MobileCAD",
                                "ArchFormat: Cannot open %s for saving", filepath.c_str());
            return false;
        }

        // File Header: 8B Magic + 4B Version + 4B CRC32 + 8B PayloadSize = 24 bytes
        file.write(MCADX_MAGIC, 8);
        file.write(reinterpret_cast<const char*>(&MCADX_VERSION), sizeof(MCADX_VERSION));
        file.write(reinterpret_cast<const char*>(&crc32), sizeof(crc32));
        file.write(reinterpret_cast<const char*>(&payloadSize), sizeof(payloadSize));

        // Write Payload
        file.write(payloadBytes.data(), payloadSize);
        file.close();

        __android_log_print(ANDROID_LOG_INFO, "MobileCAD",
                            "ArchFormat: Successfully saved %s (CRC32: 0x%08X, %llu bytes)",
                            filepath.c_str(), crc32, (unsigned long long)payloadSize);
        return true;
    }

    template <typename SceneObjectType, typename CameraType>
    static bool loadMCADX(const std::string& filepath, std::vector<SceneObjectType>& outObjects,
                          CameraType& outCamera, std::string* outProjectName = nullptr)
    {
        std::ifstream file(filepath, std::ios::binary);
        if (!file.is_open())
        {
            __android_log_print(ANDROID_LOG_ERROR, "MobileCAD",
                                "ArchFormat: Cannot open %s for loading", filepath.c_str());
            return false;
        }

        char magic[8];
        file.read(magic, 8);
        if (std::memcmp(magic, MCADX_MAGIC, 8) != 0)
        {
            __android_log_print(ANDROID_LOG_ERROR, "MobileCAD",
                                "ArchFormat: Invalid .mcadx magic in %s", filepath.c_str());
            return false;
        }

        uint32_t version = 0;
        file.read(reinterpret_cast<char*>(&version), sizeof(version));
        if (version != MCADX_VERSION)
        {
            __android_log_print(ANDROID_LOG_ERROR, "MobileCAD",
                                "ArchFormat: Unsupported .mcadx version %u", version);
            return false;
        }

        uint32_t expectedCRC32 = 0;
        file.read(reinterpret_cast<char*>(&expectedCRC32), sizeof(expectedCRC32));
        uint64_t payloadSize = 0;
        file.read(reinterpret_cast<char*>(&payloadSize), sizeof(payloadSize));

        std::string payloadBytes;
        payloadBytes.resize(payloadSize);
        file.read(&payloadBytes[0], payloadSize);
        file.close();

        uint32_t actualCRC32 = calculateCRC32(reinterpret_cast<const uint8_t*>(payloadBytes.data()),
                                              payloadBytes.size());
        if (actualCRC32 != expectedCRC32)
        {
            __android_log_print(ANDROID_LOG_ERROR, "MobileCAD",
                                "ArchFormat: CRC32 mismatch in %s (exp: 0x%08X, got: 0x%08X)",
                                filepath.c_str(), expectedCRC32, actualCRC32);
            return false;
        }

        std::stringstream payload(payloadBytes, std::ios::in | std::ios::binary);

        auto readString = [](std::istream& is) -> std::string
        {
            uint32_t len = 0;
            is.read(reinterpret_cast<char*>(&len), sizeof(len));
            if (len == 0 || len > 1024 * 1024)
                return "";
            std::string s(len, '\0');
            is.read(&s[0], len);
            return s;
        };

        std::string projName = readString(payload);
        if (outProjectName)
            *outProjectName = projName;
        std::string author = readString(payload);
        std::string units = readString(payload);
        float gridScale = 0.5f;
        payload.read(reinterpret_cast<char*>(&gridScale), sizeof(gridScale));

        // Camera State
        payload.read(reinterpret_cast<char*>(&outCamera.m_target.x), sizeof(float));
        payload.read(reinterpret_cast<char*>(&outCamera.m_target.y), sizeof(float));
        payload.read(reinterpret_cast<char*>(&outCamera.m_target.z), sizeof(float));
        payload.read(reinterpret_cast<char*>(&outCamera.m_yaw), sizeof(float));
        payload.read(reinterpret_cast<char*>(&outCamera.m_pitch), sizeof(float));
        payload.read(reinterpret_cast<char*>(&outCamera.m_distance), sizeof(float));
        payload.read(reinterpret_cast<char*>(&outCamera.m_fovY), sizeof(float));

        // CAD Layer Table
        uint32_t numLayers = 0;
        payload.read(reinterpret_cast<char*>(&numLayers), sizeof(numLayers));
        for (uint32_t i = 0; i < numLayers; ++i)
        {
            std::string lName = readString(payload);
            float r, g, b, a, lw;
            uint8_t vis;
            payload.read(reinterpret_cast<char*>(&r), sizeof(float));
            payload.read(reinterpret_cast<char*>(&g), sizeof(float));
            payload.read(reinterpret_cast<char*>(&b), sizeof(float));
            payload.read(reinterpret_cast<char*>(&a), sizeof(float));
            payload.read(reinterpret_cast<char*>(&lw), sizeof(float));
            payload.read(reinterpret_cast<char*>(&vis), sizeof(uint8_t));
        }

        // Scene Objects
        uint32_t objCount = 0;
        payload.read(reinterpret_cast<char*>(&objCount), sizeof(objCount));
        outObjects.clear();
        outObjects.reserve(objCount);

        for (uint32_t i = 0; i < objCount; ++i)
        {
            SceneObjectType obj;
            obj.name = readString(payload);
            int32_t primType = 0;
            payload.read(reinterpret_cast<char*>(&primType), sizeof(primType));
            obj.primitiveType = static_cast<decltype(obj.primitiveType)>(primType);
            obj.params.bimLayer = readString(payload);

            payload.read(reinterpret_cast<char*>(&obj.params.width), sizeof(float));
            payload.read(reinterpret_cast<char*>(&obj.params.height), sizeof(float));
            payload.read(reinterpret_cast<char*>(&obj.params.depth), sizeof(float));
            payload.read(reinterpret_cast<char*>(&obj.params.radius), sizeof(float));
            payload.read(reinterpret_cast<char*>(&obj.params.holeRadius), sizeof(float));
            int32_t segs = 32;
            payload.read(reinterpret_cast<char*>(&segs), sizeof(int32_t));
            obj.params.segments = segs;
            int32_t csg = 0;
            payload.read(reinterpret_cast<char*>(&csg), sizeof(int32_t));
            obj.params.csgMode = csg;
            payload.read(reinterpret_cast<char*>(&obj.params.bevelWidth), sizeof(float));
            int32_t bSegs = 1;
            payload.read(reinterpret_cast<char*>(&bSegs), sizeof(int32_t));
            obj.params.bevelSegments = bSegs;
            payload.read(reinterpret_cast<char*>(&obj.params.revolveAngle), sizeof(float));
            int32_t rSegs = 36;
            payload.read(reinterpret_cast<char*>(&rSegs), sizeof(int32_t));
            obj.params.revolveSegments = rSegs;
            payload.read(reinterpret_cast<char*>(&obj.params.revolveOffset), sizeof(float));

            // BIM Parametric Properties
            payload.read(reinterpret_cast<char*>(&obj.params.wallLength), sizeof(float));
            payload.read(reinterpret_cast<char*>(&obj.params.wallThickness), sizeof(float));
            payload.read(reinterpret_cast<char*>(&obj.params.wallHeight), sizeof(float));
            payload.read(reinterpret_cast<char*>(&obj.params.doorWidth), sizeof(float));
            payload.read(reinterpret_cast<char*>(&obj.params.doorHeight), sizeof(float));
            payload.read(reinterpret_cast<char*>(&obj.params.doorSwingAngle), sizeof(float));
            payload.read(reinterpret_cast<char*>(&obj.params.windowWidth), sizeof(float));
            payload.read(reinterpret_cast<char*>(&obj.params.windowHeight), sizeof(float));
            payload.read(reinterpret_cast<char*>(&obj.params.windowSillHeight), sizeof(float));
            int32_t stairSteps = 10;
            payload.read(reinterpret_cast<char*>(&stairSteps), sizeof(int32_t));
            obj.params.stairSteps = stairSteps;
            payload.read(reinterpret_cast<char*>(&obj.params.stairRise), sizeof(float));
            payload.read(reinterpret_cast<char*>(&obj.params.stairRun), sizeof(float));
            payload.read(reinterpret_cast<char*>(&obj.params.stairWidth), sizeof(float));
            payload.read(reinterpret_cast<char*>(&obj.params.roofWidth), sizeof(float));
            payload.read(reinterpret_cast<char*>(&obj.params.roofLength), sizeof(float));
            payload.read(reinterpret_cast<char*>(&obj.params.roofRidgeHeight), sizeof(float));
            payload.read(reinterpret_cast<char*>(&obj.params.roofOverhang), sizeof(float));

            // Transform
            payload.read(reinterpret_cast<char*>(&obj.position.x), sizeof(float));
            payload.read(reinterpret_cast<char*>(&obj.position.y), sizeof(float));
            payload.read(reinterpret_cast<char*>(&obj.position.z), sizeof(float));
            payload.read(reinterpret_cast<char*>(&obj.rotationDeg.x), sizeof(float));
            payload.read(reinterpret_cast<char*>(&obj.rotationDeg.y), sizeof(float));
            payload.read(reinterpret_cast<char*>(&obj.rotationDeg.z), sizeof(float));
            payload.read(reinterpret_cast<char*>(&obj.scale.x), sizeof(float));
            payload.read(reinterpret_cast<char*>(&obj.scale.y), sizeof(float));
            payload.read(reinterpret_cast<char*>(&obj.scale.z), sizeof(float));

            // Flags
            uint8_t flags[2] = {0, 0};
            payload.read(reinterpret_cast<char*>(flags), 2);
            obj.isSelected = (flags[0] != 0);
            obj.isNegative = (flags[1] != 0);

            // Material
            int32_t matPreset = 0;
            payload.read(reinterpret_cast<char*>(&matPreset), sizeof(matPreset));
            obj.materialPreset = matPreset;
            payload.read(reinterpret_cast<char*>(&obj.materialColor.x), sizeof(float));
            payload.read(reinterpret_cast<char*>(&obj.materialColor.y), sizeof(float));
            payload.read(reinterpret_cast<char*>(&obj.materialColor.z), sizeof(float));
            payload.read(reinterpret_cast<char*>(&obj.metallic), sizeof(float));
            payload.read(reinterpret_cast<char*>(&obj.roughness), sizeof(float));

            // Per-Surface Materials
            uint32_t surfCount = 0;
            payload.read(reinterpret_cast<char*>(&surfCount), sizeof(surfCount));
            obj.surfaceMaterials.resize(surfCount);
            for (uint32_t s = 0; s < surfCount; ++s)
            {
                payload.read(reinterpret_cast<char*>(&obj.surfaceMaterials[s].color.x),
                             sizeof(float));
                payload.read(reinterpret_cast<char*>(&obj.surfaceMaterials[s].color.y),
                             sizeof(float));
                payload.read(reinterpret_cast<char*>(&obj.surfaceMaterials[s].color.z),
                             sizeof(float));
                payload.read(reinterpret_cast<char*>(&obj.surfaceMaterials[s].metallic),
                             sizeof(float));
                payload.read(reinterpret_cast<char*>(&obj.surfaceMaterials[s].roughness),
                             sizeof(float));
                int32_t p = 0;
                payload.read(reinterpret_cast<char*>(&p), sizeof(p));
                obj.surfaceMaterials[s].preset = p;
            }

            // Sketch Points
            uint32_t ptCount = 0;
            payload.read(reinterpret_cast<char*>(&ptCount), sizeof(ptCount));
            obj.sketchPoints.resize(ptCount);
            for (uint32_t p = 0; p < ptCount; ++p)
            {
                payload.read(reinterpret_cast<char*>(&obj.sketchPoints[p].x), sizeof(float));
                payload.read(reinterpret_cast<char*>(&obj.sketchPoints[p].y), sizeof(float));
                payload.read(reinterpret_cast<char*>(&obj.sketchPoints[p].z), sizeof(float));
            }

            // CPU Vertices & Indices
            uint32_t vCount = 0;
            payload.read(reinterpret_cast<char*>(&vCount), sizeof(vCount));
            obj.cpuVertices.resize(vCount);
            if (vCount > 0)
            {
                payload.read(reinterpret_cast<char*>(obj.cpuVertices.data()),
                             vCount * sizeof(rendering::Vertex));
            }

            uint32_t idxCount = 0;
            payload.read(reinterpret_cast<char*>(&idxCount), sizeof(idxCount));
            obj.cpuIndices.resize(idxCount);
            if (idxCount > 0)
            {
                payload.read(reinterpret_cast<char*>(obj.cpuIndices.data()),
                             idxCount * sizeof(uint32_t));
            }

            // Read past 16-byte alignment padding
            std::streampos curPos = payload.tellg();
            size_t rem = static_cast<size_t>(curPos) % 16;
            if (rem != 0)
            {
                size_t padBytes = 16 - rem;
                char pad[16];
                payload.read(pad, padBytes);
            }

            obj.updateAABB();
            outObjects.push_back(std::move(obj));
        }

        __android_log_print(ANDROID_LOG_INFO, "MobileCAD",
                            "ArchFormat: Successfully loaded %zu objects from %s",
                            outObjects.size(), filepath.c_str());
        return true;
    }

    // -------------------------------------------------------------
    // 2. AutoCAD .dxf (2D Floorplan + 3D Mesh Exchange Format)
    // -------------------------------------------------------------
    template <typename SceneObjectType>
    static bool exportDXF(const std::string& filepath, const std::vector<SceneObjectType>& objects,
                          const std::string& modelName = "MobileCAD_Floorplan")
    {
        if (objects.empty())
        {
            __android_log_print(ANDROID_LOG_ERROR, "MobileCAD",
                                "DXF export failed: scene is empty");
            return false;
        }

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
            __android_log_print(ANDROID_LOG_ERROR, "MobileCAD", "DXF create_directories error: %s",
                                e.what());
        }

        std::ofstream file(filepath, std::ios::trunc);
        if (!file.is_open())
        {
            __android_log_print(ANDROID_LOG_ERROR, "MobileCAD", "DXF export failed: cannot open %s",
                                filepath.c_str());
            return false;
        }

        file << std::fixed << std::setprecision(4);

        // DXF HEADER SECTION
        file << "999\nMobileCAD DXF Export: " << modelName << "\n";
        file << "0\nSECTION\n";
        file << "2\nHEADER\n";
        file << "9\n$ACADVER\n1\nAC1015\n";  // AutoCAD 2000
        file << "9\n$INSUNITS\n70\n6\n";     // 6 = Meters (standard architectural)
        file << "9\n$DIMTXT\n40\n0.25\n";    // Text height = 0.25m
        file << "0\nENDSEC\n";

        // DXF TABLES SECTION
        file << "0\nSECTION\n";
        file << "2\nTABLES\n";

        // LAYER TABLE
        file << "0\nTABLE\n";
        file << "2\nLAYER\n";
        file << "70\n8\n";  // 8 layers defined

        auto writeLayer = [&](const char* name, int colorIndex)
        {
            file << "0\nLAYER\n";
            file << "2\n" << name << "\n";
            file << "70\n0\n";
            file << "62\n" << colorIndex << "\n";
            file << "6\nCONTINUOUS\n";
        };

        writeLayer("0", 7);        // White
        writeLayer("A-WALL", 7);   // White
        writeLayer("A-DOOR", 1);   // Red
        writeLayer("A-WIND", 4);   // Cyan
        writeLayer("A-STAIR", 2);  // Yellow
        writeLayer("A-ROOF", 6);   // Magenta
        writeLayer("A-DIMS", 3);   // Green
        writeLayer("3D-MESH", 5);  // Blue

        file << "0\nENDTAB\n";
        file << "0\nENDSEC\n";

        // DXF BLOCKS SECTION (empty)
        file << "0\nSECTION\n";
        file << "2\nBLOCKS\n";
        file << "0\nENDSEC\n";

        // DXF ENTITIES SECTION
        file << "0\nSECTION\n";
        file << "2\nENTITIES\n";

        size_t totalFaces = 0;
        size_t totalLines = 0;

        for (size_t objIdx = 0; objIdx < objects.size(); ++objIdx)
        {
            const auto& obj = objects[objIdx];
            if (obj.isNegative || obj.cpuVertices.empty() || obj.cpuIndices.size() < 3)
                continue;

            auto model = obj.getModelMatrix();

            // Layer assignment based on primitive type / bimLayer
            std::string layer = obj.params.bimLayer.empty() ? "3D-MESH" : obj.params.bimLayer;

            // A. Write 3D Mesh as 3DFACE entities
            for (size_t i = 0; i + 2 < obj.cpuIndices.size(); i += 3)
            {
                uint32_t i0 = obj.cpuIndices[i + 0];
                uint32_t i1 = obj.cpuIndices[i + 1];
                uint32_t i2 = obj.cpuIndices[i + 2];

                if (i0 >= obj.cpuVertices.size() || i1 >= obj.cpuVertices.size() ||
                    i2 >= obj.cpuVertices.size())
                    continue;

                auto p0 = model * utils::math::Vec3f{obj.cpuVertices[i0].position[0],
                                                     obj.cpuVertices[i0].position[1],
                                                     obj.cpuVertices[i0].position[2]};
                auto p1 = model * utils::math::Vec3f{obj.cpuVertices[i1].position[0],
                                                     obj.cpuVertices[i1].position[1],
                                                     obj.cpuVertices[i1].position[2]};
                auto p2 = model * utils::math::Vec3f{obj.cpuVertices[i2].position[0],
                                                     obj.cpuVertices[i2].position[1],
                                                     obj.cpuVertices[i2].position[2]};

                file << "0\n3DFACE\n";
                file << "8\n" << layer << "\n";
                file << "10\n" << p0.x << "\n20\n" << p0.z << "\n30\n" << p0.y << "\n";
                file << "11\n" << p1.x << "\n21\n" << p1.z << "\n31\n" << p1.y << "\n";
                file << "12\n" << p2.x << "\n22\n" << p2.z << "\n32\n" << p2.y << "\n";
                file << "13\n" << p2.x << "\n23\n" << p2.z << "\n33\n" << p2.y << "\n";
                totalFaces++;
            }

            // B. 2D Architectural Floorplan Projections at Ground Plane Y = 0
            // For wall-like primitives (ArchWall, ArchDoor, ArchWindow) or Box
            float w = obj.params.wallLength;
            float t = obj.params.wallThickness;
            if (w <= 0.01f)
                w = obj.params.width;
            if (t <= 0.01f)
                t = obj.params.depth;

            // Compute 4 footprint corners in world X-Z space (at Y = 0)
            float hw = w * 0.5f;
            float ht = t * 0.5f;

            utils::math::Vec3f cLocal[4] = {
                {-hw, 0.0f, -ht}, {hw, 0.0f, -ht}, {hw, 0.0f, ht}, {-hw, 0.0f, ht}};
            utils::math::Vec3f cWorld[4];
            for (int k = 0; k < 4; ++k)
            {
                cWorld[k] = model * cLocal[k];
            }

            auto writeLine =
                [&](const char* lName, const utils::math::Vec3f& a, const utils::math::Vec3f& b)
            {
                file << "0\nLINE\n";
                file << "8\n" << lName << "\n";
                file << "10\n" << a.x << "\n20\n" << a.z << "\n30\n0.0\n";
                file << "11\n" << b.x << "\n21\n" << b.z << "\n31\n0.0\n";
                totalLines++;
            };

            // Wall footprint perimeter
            writeLine("A-WALL", cWorld[0], cWorld[1]);
            writeLine("A-WALL", cWorld[1], cWorld[2]);
            writeLine("A-WALL", cWorld[2], cWorld[3]);
            writeLine("A-WALL", cWorld[3], cWorld[0]);

            // If Door: Add door swing arc and opening
            if (obj.name.find("Door") != std::string::npos || layer == "A-DOOR")
            {
                float dw = obj.params.doorWidth > 0.1f ? obj.params.doorWidth : 0.90f;
                utils::math::Vec3f hingeLocal{-dw * 0.5f, 0.0f, 0.0f};
                utils::math::Vec3f tipLocal{-dw * 0.5f, 0.0f, dw};
                auto hingeWorld = model * hingeLocal;
                auto tipWorld = model * tipLocal;
                writeLine("A-DOOR", hingeWorld, tipWorld);

                // Add DXF ARC entity for door swing
                file << "0\nARC\n";
                file << "8\nA-DOOR\n";
                file << "10\n" << hingeWorld.x << "\n20\n" << hingeWorld.z << "\n30\n0.0\n";
                file << "40\n" << dw << "\n";
                float baseAngleDeg = -obj.rotationDeg.y;
                file << "50\n" << baseAngleDeg << "\n";
                file << "51\n" << (baseAngleDeg + 90.0f) << "\n";
            }

            // Dimension callout at wall centerline
            if (w >= 0.5f)
            {
                utils::math::Vec3f midWorld = (cWorld[0] + cWorld[2]) * 0.5f;
                char dimText[64];
                snprintf(dimText, sizeof(dimText), "%.2f m", w);

                file << "0\nTEXT\n";
                file << "8\nA-DIMS\n";
                file << "10\n" << midWorld.x << "\n20\n" << (midWorld.z + 0.15f) << "\n30\n0.0\n";
                file << "40\n0.22\n";  // text height
                file << "1\n" << dimText << "\n";
            }
        }

        file << "0\nENDSEC\n";
        file << "0\nEOF\n";
        file.close();

        __android_log_print(
            ANDROID_LOG_INFO, "MobileCAD",
            "DXF export: %zu 3DFACE entities and %zu 2D floorplan lines written to %s", totalFaces,
            totalLines, filepath.c_str());
        return true;
    }
};

}  // namespace io
