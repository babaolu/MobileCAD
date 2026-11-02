#pragma once

#include <android/log.h>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "rendering/MeshBuffer.h"

namespace io
{

class MeshExporter
{
   public:
    static bool exportBinarySTL(const std::string& filepath,
                                const std::vector<rendering::Vertex>& vertices,
                                const std::vector<uint32_t>& indices)
    {
        if (vertices.empty() || indices.size() < 3)
        {
            __android_log_print(ANDROID_LOG_ERROR, "MobileCAD", "STL export failed: empty mesh");
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
            __android_log_print(ANDROID_LOG_ERROR, "MobileCAD", "Failed to create directory: %s",
                                e.what());
        }

        std::ofstream file(filepath, std::ios::binary | std::ios::trunc);
        if (!file.is_open())
        {
            __android_log_print(ANDROID_LOG_ERROR, "MobileCAD", "STL export failed: cannot open %s",
                                filepath.c_str());
            return false;
        }

        // 1. 80-byte header
        char header[80] = {0};
        const char title[] = "MobileCAD 1.4 Binary STL Export";
        std::memcpy(header, title, sizeof(title) - 1);
        file.write(header, 80);

        // 2. uint32_t triangle count
        uint32_t triangleCount = static_cast<uint32_t>(indices.size() / 3);
        file.write(reinterpret_cast<const char*>(&triangleCount), sizeof(triangleCount));

        // 3. Triangles (50 bytes each: normal[3], v1[3], v2[3], v3[3], attrCount)
        const uint16_t attrByteCount = 0;
        for (size_t i = 0; i + 2 < indices.size(); i += 3)
        {
            uint32_t i0 = indices[i];
            uint32_t i1 = indices[i + 1];
            uint32_t i2 = indices[i + 2];

            if (i0 >= vertices.size() || i1 >= vertices.size() || i2 >= vertices.size())
            {
                continue;
            }

            const auto& v0 = vertices[i0];
            const auto& v1 = vertices[i1];
            const auto& v2 = vertices[i2];

            // Compute face normal: (v1 - v0) x (v2 - v0)
            float e1[3] = {v1.position[0] - v0.position[0], v1.position[1] - v0.position[1],
                           v1.position[2] - v0.position[2]};
            float e2[3] = {v2.position[0] - v0.position[0], v2.position[1] - v0.position[1],
                           v2.position[2] - v0.position[2]};

            float normal[3] = {e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2],
                               e1[0] * e2[1] - e1[1] * e2[0]};

            float len = std::hypot(std::hypot(normal[0], normal[1]), normal[2]);
            if (len > 1e-7f)
            {
                normal[0] /= len;
                normal[1] /= len;
                normal[2] /= len;
            }
            else
            {
                normal[0] = 0.0f;
                normal[1] = 1.0f;
                normal[2] = 0.0f;
            }

            // Write normal (3 x float)
            file.write(reinterpret_cast<const char*>(normal), sizeof(normal));

            // Write 3 vertices (3 x 3 x float)
            file.write(reinterpret_cast<const char*>(v0.position), sizeof(v0.position));
            file.write(reinterpret_cast<const char*>(v1.position), sizeof(v1.position));
            file.write(reinterpret_cast<const char*>(v2.position), sizeof(v2.position));

            // Write attribute byte count (uint16_t)
            file.write(reinterpret_cast<const char*>(&attrByteCount), sizeof(attrByteCount));
        }

        file.close();
        __android_log_print(ANDROID_LOG_INFO, "MobileCAD",
                            "STL export: %u triangles successfully written to %s", triangleCount,
                            filepath.c_str());
        return true;
    }
};

}  // namespace io
