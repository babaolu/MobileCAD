#pragma once

#include <android/log.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "rendering/MeshBuffer.h"
#include "utils/math/Vec3.h"

namespace io
{

class MeshImporter
{
   public:
    static bool importSTL(const std::string& filepath, std::vector<rendering::Vertex>& outVertices,
                          std::vector<uint32_t>& outIndices)
    {
        outVertices.clear();
        outIndices.clear();

        if (!std::filesystem::exists(filepath))
        {
            __android_log_print(ANDROID_LOG_ERROR, "MobileCAD", "STL import: file %s not found",
                                filepath.c_str());
            return false;
        }

        uintmax_t fileSize = std::filesystem::file_size(filepath);
        if (fileSize < 84)
        {
            __android_log_print(ANDROID_LOG_ERROR, "MobileCAD",
                                "STL import: file %s too small (%ju bytes)", filepath.c_str(),
                                fileSize);
            return false;
        }

        // Check if it's binary STL
        std::ifstream file(filepath, std::ios::binary);
        if (!file.is_open())
        {
            __android_log_print(ANDROID_LOG_ERROR, "MobileCAD", "STL import: cannot open file %s",
                                filepath.c_str());
            return false;
        }

        char header[80];
        file.read(header, 80);

        uint32_t triangleCount = 0;
        file.read(reinterpret_cast<char*>(&triangleCount), sizeof(triangleCount));

        bool isBinary = false;
        if (fileSize == 84 + static_cast<uint64_t>(triangleCount) * 50)
        {
            isBinary = true;
        }
        else
        {
            std::string headerStr(header, 80);
            auto solidPos = headerStr.find("solid");
            if (solidPos != 0 && fileSize >= 84 + static_cast<uint64_t>(triangleCount) * 50)
            {
                isBinary = true;
            }
        }

        const float defaultColor[3] = {0.80f, 0.82f, 0.85f};

        if (isBinary)
        {
            __android_log_print(ANDROID_LOG_INFO, "MobileCAD",
                                "STL import: parsing binary STL with %u triangles", triangleCount);
            outVertices.reserve(triangleCount * 3);
            outIndices.reserve(triangleCount * 3);

            for (uint32_t i = 0; i < triangleCount; ++i)
            {
                float normal[3];
                float v0[3];
                float v1[3];
                float v2[3];
                uint16_t attrByteCount = 0;

                file.read(reinterpret_cast<char*>(normal), 12);
                file.read(reinterpret_cast<char*>(v0), 12);
                file.read(reinterpret_cast<char*>(v1), 12);
                file.read(reinterpret_cast<char*>(v2), 12);
                file.read(reinterpret_cast<char*>(&attrByteCount), 2);

                if (!file.good())
                {
                    __android_log_print(ANDROID_LOG_WARN, "MobileCAD",
                                        "STL import: premature EOF at triangle %u / %u", i,
                                        triangleCount);
                    break;
                }

                // Verify or compute geometric normal
                utils::math::Vec3f n{normal[0], normal[1], normal[2]};
                if (n.lengthSquared() < 1e-6f)
                {
                    utils::math::Vec3f p0{v0[0], v0[1], v0[2]};
                    utils::math::Vec3f p1{v1[0], v1[1], v1[2]};
                    utils::math::Vec3f p2{v2[0], v2[1], v2[2]};
                    n = (p1 - p0).cross(p2 - p0);
                    if (n.lengthSquared() > 1e-8f)
                    {
                        n.normalize();
                    }
                    else
                    {
                        n = {0.0f, 1.0f, 0.0f};
                    }
                }
                else
                {
                    n.normalize();
                }

                uint32_t baseIdx = static_cast<uint32_t>(outVertices.size());
                outVertices.push_back({{v0[0], v0[1], v0[2]},
                                       {n.x, n.y, n.z},
                                       {defaultColor[0], defaultColor[1], defaultColor[2]}});
                outVertices.push_back({{v1[0], v1[1], v1[2]},
                                       {n.x, n.y, n.z},
                                       {defaultColor[0], defaultColor[1], defaultColor[2]}});
                outVertices.push_back({{v2[0], v2[1], v2[2]},
                                       {n.x, n.y, n.z},
                                       {defaultColor[0], defaultColor[1], defaultColor[2]}});

                outIndices.push_back(baseIdx);
                outIndices.push_back(baseIdx + 1);
                outIndices.push_back(baseIdx + 2);
            }

            file.close();
            __android_log_print(ANDROID_LOG_INFO, "MobileCAD",
                                "STL import: successfully loaded binary STL (%zu verts, %zu "
                                "indices)",
                                outVertices.size(), outIndices.size());
            return !outVertices.empty();
        }

        // ASCII STL Fallback
        file.close();
        std::ifstream asciiFile(filepath);
        if (!asciiFile.is_open())
        {
            return false;
        }

        __android_log_print(ANDROID_LOG_INFO, "MobileCAD", "STL import: parsing ASCII STL (%s)",
                            filepath.c_str());

        std::string line;
        utils::math::Vec3f currentNormal{0.0f, 1.0f, 0.0f};
        std::vector<utils::math::Vec3f> facetVerts;

        while (std::getline(asciiFile, line))
        {
            std::istringstream iss(line);
            std::string token;
            if (!(iss >> token))
                continue;

            std::transform(token.begin(), token.end(), token.begin(), ::tolower);

            if (token == "facet")
            {
                std::string sub;
                iss >> sub;
                std::transform(sub.begin(), sub.end(), sub.begin(), ::tolower);
                if (sub == "normal")
                {
                    float nx = 0, ny = 0, nz = 0;
                    if (iss >> nx >> ny >> nz)
                    {
                        currentNormal = {nx, ny, nz};
                        if (currentNormal.lengthSquared() > 1e-6f)
                            currentNormal.normalize();
                    }
                }
                facetVerts.clear();
            }
            else if (token == "vertex")
            {
                float x = 0, y = 0, z = 0;
                if (iss >> x >> y >> z)
                {
                    facetVerts.push_back({x, y, z});
                }
            }
            else if (token == "endfacet")
            {
                if (facetVerts.size() >= 3)
                {
                    // If normal was zero, compute geometric normal
                    if (currentNormal.lengthSquared() < 1e-6f)
                    {
                        currentNormal =
                            (facetVerts[1] - facetVerts[0]).cross(facetVerts[2] - facetVerts[0]);
                        if (currentNormal.lengthSquared() > 1e-8f)
                            currentNormal.normalize();
                        else
                            currentNormal = {0.0f, 1.0f, 0.0f};
                    }

                    // Triangulate facet
                    for (size_t k = 1; k + 1 < facetVerts.size(); ++k)
                    {
                        uint32_t baseIdx = static_cast<uint32_t>(outVertices.size());
                        const auto& v0 = facetVerts[0];
                        const auto& v1 = facetVerts[k];
                        const auto& v2 = facetVerts[k + 1];

                        outVertices.push_back(
                            {{v0.x, v0.y, v0.z},
                             {currentNormal.x, currentNormal.y, currentNormal.z},
                             {defaultColor[0], defaultColor[1], defaultColor[2]}});
                        outVertices.push_back(
                            {{v1.x, v1.y, v1.z},
                             {currentNormal.x, currentNormal.y, currentNormal.z},
                             {defaultColor[0], defaultColor[1], defaultColor[2]}});
                        outVertices.push_back(
                            {{v2.x, v2.y, v2.z},
                             {currentNormal.x, currentNormal.y, currentNormal.z},
                             {defaultColor[0], defaultColor[1], defaultColor[2]}});

                        outIndices.push_back(baseIdx);
                        outIndices.push_back(baseIdx + 1);
                        outIndices.push_back(baseIdx + 2);
                    }
                }
                facetVerts.clear();
                currentNormal = {0.0f, 1.0f, 0.0f};
            }
        }

        asciiFile.close();
        __android_log_print(ANDROID_LOG_INFO, "MobileCAD",
                            "STL import: successfully loaded ASCII STL (%zu verts, %zu indices)",
                            outVertices.size(), outIndices.size());
        return !outVertices.empty();
    }

    static bool importOBJ(const std::string& filepath, std::vector<rendering::Vertex>& outVertices,
                          std::vector<uint32_t>& outIndices)
    {
        outVertices.clear();
        outIndices.clear();

        std::ifstream file(filepath);
        if (!file.is_open())
        {
            __android_log_print(ANDROID_LOG_ERROR, "MobileCAD", "OBJ import: cannot open %s",
                                filepath.c_str());
            return false;
        }

        std::vector<utils::math::Vec3f> positions;
        std::vector<utils::math::Vec3f> normals;
        positions.reserve(4096);
        normals.reserve(4096);

        struct FaceVertex
        {
            int vIdx{0};
            int nIdx{0};
        };

        const float defaultColor[3] = {0.80f, 0.82f, 0.85f};
        std::string line;

        while (std::getline(file, line))
        {
            if (line.empty() || line[0] == '#')
                continue;

            std::istringstream iss(line);
            std::string prefix;
            if (!(iss >> prefix))
                continue;

            if (prefix == "v")
            {
                float x = 0, y = 0, z = 0;
                if (iss >> x >> y >> z)
                {
                    positions.push_back({x, y, z});
                }
            }
            else if (prefix == "vn")
            {
                float x = 0, y = 0, z = 0;
                if (iss >> x >> y >> z)
                {
                    utils::math::Vec3f n{x, y, z};
                    if (n.lengthSquared() > 1e-6f)
                        n.normalize();
                    normals.push_back(n);
                }
            }
            else if (prefix == "f")
            {
                std::vector<FaceVertex> faceVerts;
                std::string vertToken;
                while (iss >> vertToken)
                {
                    FaceVertex fv;
                    // Format: v, v/vt, v//vn, or v/vt/vn
                    size_t slash1 = vertToken.find('/');
                    if (slash1 == std::string::npos)
                    {
                        fv.vIdx = std::stoi(vertToken);
                        fv.nIdx = 0;
                    }
                    else
                    {
                        fv.vIdx = std::stoi(vertToken.substr(0, slash1));
                        size_t slash2 = vertToken.find('/', slash1 + 1);
                        if (slash2 != std::string::npos)
                        {
                            std::string normSub = vertToken.substr(slash2 + 1);
                            if (!normSub.empty())
                            {
                                fv.nIdx = std::stoi(normSub);
                            }
                        }
                    }
                    faceVerts.push_back(fv);
                }

                if (faceVerts.size() < 3)
                    continue;

                auto resolveIdx = [](int idx, size_t totalCount) -> int
                {
                    if (idx > 0)
                        return idx - 1;
                    if (idx < 0)
                        return static_cast<int>(totalCount) + idx;
                    return -1;
                };

                // Triangulate polygon via triangle fan (0, k, k+1)
                for (size_t k = 1; k + 1 < faceVerts.size(); ++k)
                {
                    int i0 = resolveIdx(faceVerts[0].vIdx, positions.size());
                    int i1 = resolveIdx(faceVerts[k].vIdx, positions.size());
                    int i2 = resolveIdx(faceVerts[k + 1].vIdx, positions.size());

                    if (i0 < 0 || i0 >= static_cast<int>(positions.size()) || i1 < 0 ||
                        i1 >= static_cast<int>(positions.size()) || i2 < 0 ||
                        i2 >= static_cast<int>(positions.size()))
                    {
                        continue;
                    }

                    const auto& p0 = positions[i0];
                    const auto& p1 = positions[i1];
                    const auto& p2 = positions[i2];

                    // Determine normal for each vertex
                    utils::math::Vec3f geomNormal = (p1 - p0).cross(p2 - p0);
                    if (geomNormal.lengthSquared() > 1e-8f)
                        geomNormal.normalize();
                    else
                        geomNormal = {0.0f, 1.0f, 0.0f};

                    auto getNormal = [&](int nIdx) -> utils::math::Vec3f
                    {
                        int rIdx = resolveIdx(nIdx, normals.size());
                        if (rIdx >= 0 && rIdx < static_cast<int>(normals.size()))
                        {
                            const auto& n = normals[rIdx];
                            if (n.lengthSquared() > 1e-6f)
                                return n;
                        }
                        return geomNormal;
                    };

                    utils::math::Vec3f n0 = getNormal(faceVerts[0].nIdx);
                    utils::math::Vec3f n1 = getNormal(faceVerts[k].nIdx);
                    utils::math::Vec3f n2 = getNormal(faceVerts[k + 1].nIdx);

                    uint32_t baseIdx = static_cast<uint32_t>(outVertices.size());
                    outVertices.push_back({{p0.x, p0.y, p0.z},
                                           {n0.x, n0.y, n0.z},
                                           {defaultColor[0], defaultColor[1], defaultColor[2]}});
                    outVertices.push_back({{p1.x, p1.y, p1.z},
                                           {n1.x, n1.y, n1.z},
                                           {defaultColor[0], defaultColor[1], defaultColor[2]}});
                    outVertices.push_back({{p2.x, p2.y, p2.z},
                                           {n2.x, n2.y, n2.z},
                                           {defaultColor[0], defaultColor[1], defaultColor[2]}});

                    outIndices.push_back(baseIdx);
                    outIndices.push_back(baseIdx + 1);
                    outIndices.push_back(baseIdx + 2);
                }
            }
        }

        file.close();
        __android_log_print(ANDROID_LOG_INFO, "MobileCAD",
                            "OBJ import: successfully loaded OBJ %s (%zu verts, %zu indices)",
                            filepath.c_str(), outVertices.size(), outIndices.size());
        return !outVertices.empty();
    }

    static bool importMesh(const std::string& filepath, std::vector<rendering::Vertex>& outVertices,
                           std::vector<uint32_t>& outIndices)
    {
        std::filesystem::path p(filepath);
        std::string ext = p.extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

        if (ext == ".stl")
        {
            return importSTL(filepath, outVertices, outIndices);
        }
        if (ext == ".obj")
        {
            return importOBJ(filepath, outVertices, outIndices);
        }

        // Try STL first, then OBJ
        if (importSTL(filepath, outVertices, outIndices))
            return true;
        return importOBJ(filepath, outVertices, outIndices);
    }
};

}  // namespace io
