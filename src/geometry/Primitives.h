#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <map>
#include <tuple>
#include <utility>
#include <vector>

#include "manifold/manifold.h"
#include "rendering/MeshBuffer.h"
#include "utils/math/Vec3.h"

namespace geometry
{

class Primitives
{
   public:
    using MeshData = std::pair<std::vector<rendering::Vertex>, std::vector<uint32_t>>;

    // 1. Box / Block (width=1.5, height=1.5, depth=1.5, resting on Y in [0, height])
    static MeshData createBox(float width = 1.5f, float height = 1.5f, float depth = 1.5f)
    {
        std::vector<rendering::Vertex> vertices;
        std::vector<uint32_t> indices;

        const float hx = width * 0.5f;
        const float y0 = 0.0f;
        const float y1 = height;
        const float hz = depth * 0.5f;

        auto addQuad = [&](const float p0[3], const float p1[3], const float p2[3],
                           const float p3[3], const float n[3], const float c[3])
        {
            uint32_t baseIdx = static_cast<uint32_t>(vertices.size());
            vertices.push_back({{p0[0], p0[1], p0[2]}, {n[0], n[1], n[2]}, {c[0], c[1], c[2]}});
            vertices.push_back({{p1[0], p1[1], p1[2]}, {n[0], n[1], n[2]}, {c[0], c[1], c[2]}});
            vertices.push_back({{p2[0], p2[1], p2[2]}, {n[0], n[1], n[2]}, {c[0], c[1], c[2]}});
            vertices.push_back({{p3[0], p3[1], p3[2]}, {n[0], n[1], n[2]}, {c[0], c[1], c[2]}});

            indices.push_back(baseIdx + 0);
            indices.push_back(baseIdx + 1);
            indices.push_back(baseIdx + 2);
            indices.push_back(baseIdx + 0);
            indices.push_back(baseIdx + 2);
            indices.push_back(baseIdx + 3);
        };

        // +Z Face (Front) - Anodized CAD Blue
        const float pZ_0[3] = {-hx, y0, hz};
        const float pZ_1[3] = {hx, y0, hz};
        const float pZ_2[3] = {hx, y1, hz};
        const float pZ_3[3] = {-hx, y1, hz};
        const float nZ[3] = {0.0f, 0.0f, 1.0f};
        const float cZ[3] = {0.35f, 0.65f, 0.95f};
        addQuad(pZ_0, pZ_1, pZ_2, pZ_3, nZ, cZ);

        // -Z Face (Back) - Bronze / Copper
        const float nZb_0[3] = {hx, y0, -hz};
        const float nZb_1[3] = {-hx, y0, -hz};
        const float nZb_2[3] = {-hx, y1, -hz};
        const float nZb_3[3] = {hx, y1, -hz};
        const float nZb[3] = {0.0f, 0.0f, -1.0f};
        const float cZb[3] = {0.85f, 0.60f, 0.30f};
        addQuad(nZb_0, nZb_1, nZb_2, nZb_3, nZb, cZb);

        // +Y Face (Top) - Polished Brass / Gold
        const float pY_0[3] = {-hx, y1, hz};
        const float pY_1[3] = {hx, y1, hz};
        const float pY_2[3] = {hx, y1, -hz};
        const float pY_3[3] = {-hx, y1, -hz};
        const float nY[3] = {0.0f, 1.0f, 0.0f};
        const float cY[3] = {0.92f, 0.76f, 0.35f};
        addQuad(pY_0, pY_1, pY_2, pY_3, nY, cY);

        // -Y Face (Bottom) - Cast Iron
        const float nY_0[3] = {-hx, y0, -hz};
        const float nY_1[3] = {hx, y0, -hz};
        const float nY_2[3] = {hx, y0, hz};
        const float nY_3[3] = {-hx, y0, hz};
        const float nYb[3] = {0.0f, -1.0f, 0.0f};
        const float cYb[3] = {0.50f, 0.52f, 0.58f};
        addQuad(nY_0, nY_1, nY_2, nY_3, nYb, cYb);

        // +X Face (Right) - CAD Aluminum / Silver
        const float pX_0[3] = {hx, y0, hz};
        const float pX_1[3] = {hx, y0, -hz};
        const float pX_2[3] = {hx, y1, -hz};
        const float pX_3[3] = {hx, y1, hz};
        const float nX[3] = {1.0f, 0.0f, 0.0f};
        const float cX[3] = {0.82f, 0.85f, 0.90f};
        addQuad(pX_0, pX_1, pX_2, pX_3, nX, cX);

        // -X Face (Left) - Slate Steel
        const float nX_0[3] = {-hx, y0, -hz};
        const float nX_1[3] = {-hx, y0, hz};
        const float nX_2[3] = {-hx, y1, hz};
        const float nX_3[3] = {-hx, y1, -hz};
        const float nXb[3] = {-1.0f, 0.0f, 0.0f};
        const float cXb[3] = {0.75f, 0.78f, 0.85f};
        addQuad(nX_0, nX_1, nX_2, nX_3, nXb, cXb);

        return {std::move(vertices), std::move(indices)};
    }

    // 2. Cylinder (32 radial segments, radius 0.6, height 1.2, resting on Y in [0, 1.2])
    static MeshData createCylinder(float radius = 0.6f, float height = 1.2f, uint32_t segments = 32)
    {
        std::vector<rendering::Vertex> vertices;
        std::vector<uint32_t> indices;

        const float sideColor[3] = {0.45f, 0.70f, 0.90f};  // Anodized Silver-Blue
        const float topColor[3] = {0.92f, 0.76f, 0.35f};   // Brass / Gold Top
        const float botColor[3] = {0.50f, 0.52f, 0.58f};   // Cast Iron Bottom
        const float twoPi = 6.28318530718f;

        // A. Side shell (smooth radial normals)
        uint32_t sideBase = static_cast<uint32_t>(vertices.size());
        for (uint32_t i = 0; i <= segments; ++i)
        {
            float theta = twoPi * static_cast<float>(i) / static_cast<float>(segments);
            float cosT = std::cos(theta);
            float sinT = std::sin(theta);
            float nx = cosT;
            float nz = sinT;

            // Bottom vertex at Y = 0
            vertices.push_back({{radius * cosT, 0.0f, radius * sinT},
                                {nx, 0.0f, nz},
                                {sideColor[0], sideColor[1], sideColor[2]}});
            // Top vertex at Y = height
            vertices.push_back({{radius * cosT, height, radius * sinT},
                                {nx, 0.0f, nz},
                                {sideColor[0], sideColor[1], sideColor[2]}});
        }

        for (uint32_t i = 0; i < segments; ++i)
        {
            uint32_t b0 = sideBase + i * 2;
            uint32_t t0 = b0 + 1;
            uint32_t b1 = b0 + 2;
            uint32_t t1 = b0 + 3;

            indices.push_back(b0);
            indices.push_back(b1);
            indices.push_back(t1);

            indices.push_back(b0);
            indices.push_back(t1);
            indices.push_back(t0);
        }

        // B. Top cap (flat +Y normal)
        uint32_t topCenterIdx = static_cast<uint32_t>(vertices.size());
        vertices.push_back(
            {{0.0f, height, 0.0f}, {0.0f, 1.0f, 0.0f}, {topColor[0], topColor[1], topColor[2]}});
        uint32_t topRimBase = static_cast<uint32_t>(vertices.size());
        for (uint32_t i = 0; i <= segments; ++i)
        {
            float theta = twoPi * static_cast<float>(i) / static_cast<float>(segments);
            vertices.push_back({{radius * std::cos(theta), height, radius * std::sin(theta)},
                                {0.0f, 1.0f, 0.0f},
                                {topColor[0], topColor[1], topColor[2]}});
        }
        for (uint32_t i = 0; i < segments; ++i)
        {
            indices.push_back(topCenterIdx);
            indices.push_back(topRimBase + i + 1);
            indices.push_back(topRimBase + i);
        }

        // C. Bottom cap (flat -Y normal)
        uint32_t botCenterIdx = static_cast<uint32_t>(vertices.size());
        vertices.push_back(
            {{0.0f, 0.0f, 0.0f}, {0.0f, -1.0f, 0.0f}, {botColor[0], botColor[1], botColor[2]}});
        uint32_t botRimBase = static_cast<uint32_t>(vertices.size());
        for (uint32_t i = 0; i <= segments; ++i)
        {
            float theta = twoPi * static_cast<float>(i) / static_cast<float>(segments);
            vertices.push_back({{radius * std::cos(theta), 0.0f, radius * std::sin(theta)},
                                {0.0f, -1.0f, 0.0f},
                                {botColor[0], botColor[1], botColor[2]}});
        }
        for (uint32_t i = 0; i < segments; ++i)
        {
            indices.push_back(botCenterIdx);
            indices.push_back(botRimBase + i);
            indices.push_back(botRimBase + i + 1);
        }

        return {std::move(vertices), std::move(indices)};
    }

    // 3. UV Sphere (24 stacks x 32 slices, radius 0.6, resting on Y in [0, 1.2])
    static MeshData createSphere(float radius = 0.6f, uint32_t stacks = 24, uint32_t slices = 32)
    {
        std::vector<rendering::Vertex> vertices;
        std::vector<uint32_t> indices;

        const float centerY = radius;
        const float pi = 3.14159265359f;
        const float twoPi = 6.28318530718f;
        const float sphereColor[3] = {0.30f, 0.72f, 0.85f};  // CAD Cyan / Aquamarine

        for (uint32_t i = 0; i <= stacks; ++i)
        {
            float phi = pi * static_cast<float>(i) / static_cast<float>(stacks);
            float sinPhi = std::sin(phi);
            float cosPhi = std::cos(phi);

            for (uint32_t j = 0; j <= slices; ++j)
            {
                float theta = twoPi * static_cast<float>(j) / static_cast<float>(slices);
                float sinTheta = std::sin(theta);
                float cosTheta = std::cos(theta);

                float nx = sinPhi * cosTheta;
                float ny = cosPhi;
                float nz = sinPhi * sinTheta;

                float px = radius * nx;
                float py = centerY + radius * ny;
                float pz = radius * nz;

                vertices.push_back(
                    {{px, py, pz}, {nx, ny, nz}, {sphereColor[0], sphereColor[1], sphereColor[2]}});
            }
        }

        for (uint32_t i = 0; i < stacks; ++i)
        {
            for (uint32_t j = 0; j < slices; ++j)
            {
                uint32_t first = (i * (slices + 1)) + j;
                uint32_t second = first + slices + 1;

                indices.push_back(first);
                indices.push_back(first + 1);
                indices.push_back(second + 1);

                indices.push_back(first);
                indices.push_back(second + 1);
                indices.push_back(second);
            }
        }

        return {std::move(vertices), std::move(indices)};
    }

    // Convert Manifold solid to MeshData with outward normals
    static MeshData fromManifold(const manifold::Manifold& m, const float defaultColor[3] = nullptr)
    {
        std::vector<rendering::Vertex> vertices;
        std::vector<uint32_t> indices;

        const float defCol[3] = {0.35f, 0.65f, 0.95f};  // Anodized CAD Blue
        const float* col = defaultColor ? defaultColor : defCol;

        manifold::Manifold withNormals = m.CalculateNormals(3, 52.5);
        manifold::MeshGL gl = withNormals.GetMeshGL();

        indices = std::move(gl.triVerts);
        size_t numVerts = gl.NumVert();
        vertices.reserve(numVerts);

        int numProp = gl.numProp;
        for (size_t i = 0; i < numVerts; ++i)
        {
            float px = gl.vertProperties[i * numProp + 0];
            float py = gl.vertProperties[i * numProp + 1];
            float pz = gl.vertProperties[i * numProp + 2];
            float nx = 0.0f, ny = 1.0f, nz = 0.0f;
            if (numProp >= 6)
            {
                nx = gl.vertProperties[i * numProp + 3];
                ny = gl.vertProperties[i * numProp + 4];
                nz = gl.vertProperties[i * numProp + 5];
            }
            vertices.push_back({{px, py, pz}, {nx, ny, nz}, {col[0], col[1], col[2]}});
        }

        if (numProp < 6 && indices.size() >= 3)
        {
            for (size_t t = 0; t + 2 < indices.size(); t += 3)
            {
                uint32_t i0 = indices[t];
                uint32_t i1 = indices[t + 1];
                uint32_t i2 = indices[t + 2];

                float e1[3] = {vertices[i1].position[0] - vertices[i0].position[0],
                               vertices[i1].position[1] - vertices[i0].position[1],
                               vertices[i1].position[2] - vertices[i0].position[2]};
                float e2[3] = {vertices[i2].position[0] - vertices[i0].position[0],
                               vertices[i2].position[1] - vertices[i0].position[1],
                               vertices[i2].position[2] - vertices[i0].position[2]};

                float fn[3] = {e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2],
                               e1[0] * e2[1] - e1[1] * e2[0]};
                float len = std::hypot(std::hypot(fn[0], fn[1]), fn[2]);
                if (len > 1e-6f)
                {
                    fn[0] /= len;
                    fn[1] /= len;
                    fn[2] /= len;
                }

                vertices[i0].normal[0] = fn[0];
                vertices[i0].normal[1] = fn[1];
                vertices[i0].normal[2] = fn[2];
                vertices[i1].normal[0] = fn[0];
                vertices[i1].normal[1] = fn[1];
                vertices[i1].normal[2] = fn[2];
                vertices[i2].normal[0] = fn[0];
                vertices[i2].normal[1] = fn[1];
                vertices[i2].normal[2] = fn[2];
            }
        }

        return {std::move(vertices), std::move(indices)};
    }

    // 4. CSG Difference / Subtract (Drilled Mechanical Bracket: Box minus cylindrical bore)
    static MeshData createCsgDifference(float width = 1.6f, float height = 1.2f, float depth = 1.6f,
                                        float holeRadius = 0.35f, int segments = 32)
    {
        manifold::Manifold box = manifold::Manifold::Cube({width, height, depth}, true)
                                     .Translate({0.0, height * 0.5, 0.0});
        manifold::Manifold hole =
            manifold::Manifold::Cylinder(height * 1.5, holeRadius, holeRadius, segments, true)
                .Translate({0.0, height * 0.5, 0.0});
        manifold::Manifold result = box - hole;
        const float bronze[3] = {0.85f, 0.65f, 0.35f};
        return fromManifold(result, bronze);
    }

    // 5. CSG Union (Base Plate + Cylindrical Boss/Hub)
    static MeshData createCsgUnion(float width = 1.6f, float height = 1.2f, float depth = 1.6f,
                                   float cylRadius = 0.5f, int segments = 32)
    {
        float baseH = height * 0.4f;
        float bossH = height * 0.8f;
        manifold::Manifold base = manifold::Manifold::Cube({width, baseH, depth}, true)
                                      .Translate({0.0, baseH * 0.5, 0.0});
        manifold::Manifold boss =
            manifold::Manifold::Cylinder(bossH, cylRadius, cylRadius, segments, false)
                .Translate({0.0, baseH * 0.5, 0.0});
        manifold::Manifold result = base + boss;
        const float steel[3] = {0.45f, 0.70f, 0.90f};
        return fromManifold(result, steel);
    }

    // 6. CSG Intersection (Rounded Solid Part: Box ^ Sphere)
    static MeshData createCsgIntersection(float width = 1.6f, float height = 1.2f,
                                          float depth = 1.6f, float sphereRadius = 1.0f,
                                          int segments = 32)
    {
        manifold::Manifold box = manifold::Manifold::Cube({width, height, depth}, true)
                                     .Translate({0.0, height * 0.5, 0.0});
        manifold::Manifold sphere =
            manifold::Manifold::Sphere(sphereRadius, segments).Translate({0.0, height * 0.5, 0.0});
        manifold::Manifold result = box ^ sphere;
        const float gold[3] = {0.92f, 0.76f, 0.35f};
        return fromManifold(result, gold);
    }

    // 7. CSG Hollow Cylinder (Bored Pipe with exact inner and outer walls)
    static MeshData createHollowCylinder(float outerRadius = 0.7f, float innerRadius = 0.4f,
                                         float height = 1.4f, int segments = 32)
    {
        manifold::Manifold outer =
            manifold::Manifold::Cylinder(height, outerRadius, outerRadius, segments, false);
        manifold::Manifold inner =
            manifold::Manifold::Cylinder(height * 1.5, innerRadius, innerRadius, segments, true)
                .Translate({0.0, height * 0.5, 0.0});
        manifold::Manifold result = outer - inner;
        const float cyan[3] = {0.30f, 0.75f, 0.85f};
        return fromManifold(result, cyan);
    }

    // 8. 2D Parametric Profile Generators & Spline Interpolation
    static std::vector<utils::math::Vec3f> create2DCircle(float cx, float cz, float radius,
                                                          int segments = 32)
    {
        std::vector<utils::math::Vec3f> pts;
        if (segments < 3)
            segments = 3;
        pts.reserve(segments);
        const float twoPi = 6.28318530718f;
        for (int i = 0; i < segments; ++i)
        {
            float theta = twoPi * static_cast<float>(i) / static_cast<float>(segments);
            pts.push_back({cx + radius * std::cos(theta), 0.005f, cz + radius * std::sin(theta)});
        }
        return pts;
    }

    static std::vector<utils::math::Vec3f> create2DEllipse(float cx, float cz, float rx, float rz,
                                                           int segments = 32)
    {
        std::vector<utils::math::Vec3f> pts;
        if (segments < 3)
            segments = 3;
        pts.reserve(segments);
        const float twoPi = 6.28318530718f;
        for (int i = 0; i < segments; ++i)
        {
            float theta = twoPi * static_cast<float>(i) / static_cast<float>(segments);
            pts.push_back({cx + rx * std::cos(theta), 0.005f, cz + rz * std::sin(theta)});
        }
        return pts;
    }

    static std::vector<utils::math::Vec3f> create2DSquare(float cx, float cz, float side)
    {
        float h = side * 0.5f;
        return {{cx - h, 0.005f, cz - h},
                {cx + h, 0.005f, cz - h},
                {cx + h, 0.005f, cz + h},
                {cx - h, 0.005f, cz + h}};
    }

    static std::vector<utils::math::Vec3f> create2DRectangle(float cx, float cz, float width,
                                                             float depth)
    {
        float hw = width * 0.5f;
        float hd = depth * 0.5f;
        return {{cx - hw, 0.005f, cz - hd},
                {cx + hw, 0.005f, cz - hd},
                {cx + hw, 0.005f, cz + hd},
                {cx - hw, 0.005f, cz + hd}};
    }

    static std::vector<utils::math::Vec3f> create2DTriangle(float cx, float cz, float baseW,
                                                            float heightH)
    {
        float hw = baseW * 0.5f;
        float hh = heightH * 0.5f;
        return {{cx - hw, 0.005f, cz - hh}, {cx + hw, 0.005f, cz - hh}, {cx, 0.005f, cz + hh}};
    }

    static std::vector<utils::math::Vec3f> create2DWave(float cx, float cz, float length,
                                                        float depth, int segments = 32)
    {
        std::vector<utils::math::Vec3f> pts;
        int nHalf = std::max(8, segments / 2);
        pts.reserve(nHalf * 2 + 2);

        float halfL = length * 0.5f;
        float thickness = 0.4f;
        float amp = depth * 0.3f;
        const float twoPi = 6.28318530718f;

        // Top edge: from -halfL to +halfL
        for (int i = 0; i <= nHalf; ++i)
        {
            float u = static_cast<float>(i) / static_cast<float>(nHalf);
            float x = cx - halfL + u * length;
            float z = cz + amp * std::sin(twoPi * 2.0f * u) + thickness * 0.5f;
            pts.push_back({x, 0.005f, z});
        }

        // Bottom edge: from +halfL back to -halfL
        for (int i = nHalf; i >= 0; --i)
        {
            float u = static_cast<float>(i) / static_cast<float>(nHalf);
            float x = cx - halfL + u * length;
            float z = cz + amp * std::sin(twoPi * 2.0f * u) - thickness * 0.5f;
            pts.push_back({x, 0.005f, z});
        }

        return pts;
    }

    static std::vector<utils::math::Vec3f> evaluateClosedCatmullRom(
        const std::vector<utils::math::Vec3f>& ctrlPts, int subdivPerSpan = 8)
    {
        std::vector<utils::math::Vec3f> result;
        size_t m = ctrlPts.size();
        if (m < 3)
            return ctrlPts;

        result.reserve(m * subdivPerSpan);
        for (size_t i = 0; i < m; ++i)
        {
            const auto& pPrev = ctrlPts[(i + m - 1) % m];
            const auto& p0 = ctrlPts[i];
            const auto& p1 = ctrlPts[(i + 1) % m];
            const auto& p2 = ctrlPts[(i + 2) % m];

            for (int step = 0; step < subdivPerSpan; ++step)
            {
                float t = static_cast<float>(step) / static_cast<float>(subdivPerSpan);
                float t2 = t * t;
                float t3 = t2 * t;

                float f0 = -0.5f * t3 + t2 - 0.5f * t;
                float f1 = 1.5f * t3 - 2.5f * t2 + 1.0f;
                float f2 = -1.5f * t3 + 2.0f * t2 + 0.5f * t;
                float f3 = 0.5f * t3 - 0.5f * t2;

                float x = f0 * pPrev.x + f1 * p0.x + f2 * p1.x + f3 * p2.x;
                float y = f0 * pPrev.y + f1 * p0.y + f2 * p1.y + f3 * p2.y;
                float z = f0 * pPrev.z + f1 * p0.z + f2 * p1.z + f3 * p2.z;

                result.push_back({x, y, z});
            }
        }
        return result;
    }

    // 9. 2D Sketch Extrusion with 3D Chamfers & Multi-Step Bevels + Watertight Manifold
    static MeshData createExtrudedPolygon(const std::vector<utils::math::Vec3f>& rawPts,
                                          float height = 1.2f, float bevelWidth = 0.0f,
                                          int bevelSegments = 1,
                                          manifold::Manifold* outManifold = nullptr)
    {
        std::vector<rendering::Vertex> vertices;
        std::vector<uint32_t> indices;

        if (rawPts.size() < 3)
            return {std::move(vertices), std::move(indices)};

        // 1. Clean points: remove consecutive duplicates and closing duplicate
        std::vector<utils::math::Vec3f> pts;
        pts.reserve(rawPts.size());
        for (const auto& p : rawPts)
        {
            if (pts.empty())
            {
                pts.push_back({p.x, 0.0f, p.z});
            }
            else
            {
                float dx = p.x - pts.back().x;
                float dz = p.z - pts.back().z;
                if (std::hypot(dx, dz) > 1e-4f)
                {
                    pts.push_back({p.x, 0.0f, p.z});
                }
            }
        }
        if (pts.size() >= 3)
        {
            float dx = pts.front().x - pts.back().x;
            float dz = pts.front().z - pts.back().z;
            if (std::hypot(dx, dz) <= 1e-4f)
            {
                pts.pop_back();
            }
        }

        size_t n = pts.size();
        if (n < 3)
            return {std::move(vertices), std::move(indices)};

        // 2. Ensure consistent CCW winding in (X, Z) where 2A = sum(x_i * z_{i+1} - x_{i+1} * z_i)
        // > 0
        float signedArea2 = 0.0f;
        for (size_t i = 0; i < n; ++i)
        {
            size_t j = (i + 1) % n;
            signedArea2 += (pts[i].x * pts[j].z - pts[j].x * pts[i].z);
        }

        if (std::abs(signedArea2) < 1e-6f)
            return {std::move(vertices), std::move(indices)};

        if (signedArea2 < 0.0f)
        {
            std::reverse(pts.begin(), pts.end());
        }

        // 3. Triangulate 2D polygon using Ear-Clipping
        auto cross2D = [](const utils::math::Vec3f& a, const utils::math::Vec3f& b,
                          const utils::math::Vec3f& c) -> float
        { return (b.x - a.x) * (c.z - a.z) - (b.z - a.z) * (c.x - a.x); };

        auto pointInTriangle = [&](const utils::math::Vec3f& a, const utils::math::Vec3f& b,
                                   const utils::math::Vec3f& c, const utils::math::Vec3f& p) -> bool
        {
            float c1 = cross2D(a, b, p);
            float c2 = cross2D(b, c, p);
            float c3 = cross2D(c, a, p);
            return (c1 >= -1e-6f && c2 >= -1e-6f && c3 >= -1e-6f);
        };

        std::vector<int> V(n);
        for (size_t i = 0; i < n; ++i)
            V[i] = static_cast<int>(i);

        std::vector<std::array<int, 3>> capTriangles;
        int count = static_cast<int>(n);
        int curr = 0;
        int maxIters = count * count * 2;
        int iters = 0;

        while (count > 3 && iters < maxIters)
        {
            iters++;
            int prev = (curr + count - 1) % count;
            int next = (curr + 1) % count;

            int iA = V[prev];
            int iB = V[curr];
            int iC = V[next];

            const auto& pA = pts[iA];
            const auto& pB = pts[iB];
            const auto& pC = pts[iC];

            bool isEar = cross2D(pA, pB, pC) > 1e-7f;
            if (isEar)
            {
                for (int k = 0; k < count; ++k)
                {
                    if (k == prev || k == curr || k == next)
                        continue;
                    int iK = V[k];
                    if (pointInTriangle(pA, pB, pC, pts[iK]))
                    {
                        isEar = false;
                        break;
                    }
                }
            }

            if (isEar)
            {
                capTriangles.push_back({iA, iB, iC});
                V.erase(V.begin() + curr);
                count--;
                if (curr >= count)
                    curr = 0;
            }
            else
            {
                curr = (curr + 1) % count;
            }
        }

        if (count == 3)
        {
            capTriangles.push_back({V[0], V[1], V[2]});
        }
        else if (count > 3)
        {
            // Fallback fan triangulation if polygon has complex self-tangencies
            for (int k = 1; k < count - 1; ++k)
            {
                capTriangles.push_back({V[0], V[k], V[k + 1]});
            }
        }

        // 4. Compute per-vertex 2D inward miter vectors
        struct MiterVec
        {
            float vx{0.0f};
            float vz{0.0f};
        };
        std::vector<MiterVec> miters(n);
        for (size_t i = 0; i < n; ++i)
        {
            size_t prev = (i + n - 1) % n;
            size_t next = (i + 1) % n;

            float e1x = pts[i].x - pts[prev].x;
            float e1z = pts[i].z - pts[prev].z;
            float l1 = std::hypot(e1x, e1z);
            if (l1 > 1e-6f)
            {
                e1x /= l1;
                e1z /= l1;
            }

            float e2x = pts[next].x - pts[i].x;
            float e2z = pts[next].z - pts[i].z;
            float l2 = std::hypot(e2x, e2z);
            if (l2 > 1e-6f)
            {
                e2x /= l2;
                e2z /= l2;
            }

            // Inward normal for CCW edge (dx, dz) is (-dz, dx)
            float n1x = -e1z;
            float n1z = e1x;
            float n2x = -e2z;
            float n2z = e2x;

            float mx = n1x + n2x;
            float mz = n1z + n2z;
            float ml = std::hypot(mx, mz);
            if (ml < 1e-4f)
            {
                miters[i] = {n1x, n1z};
            }
            else
            {
                float s = std::min(2.0f / ml, 2.0f);
                miters[i] = {(mx / ml) * s, (mz / ml) * s};
            }
        }

        // 5. Centroid & minimum radius for safe inset clamping
        float cx = 0.0f;
        float cz = 0.0f;
        for (const auto& p : pts)
        {
            cx += p.x;
            cz += p.z;
        }
        cx /= static_cast<float>(n);
        cz /= static_cast<float>(n);

        float minRad = 1e9f;
        for (const auto& p : pts)
        {
            float d = std::hypot(p.x - cx, p.z - cz);
            if (d < minRad)
                minRad = d;
        }

        float b = std::max(0.0f, bevelWidth);
        b = std::min(b, 0.35f * height);
        b = std::min(b, 0.35f * minRad);

        struct ProfileStep
        {
            float y{0.0f};
            float inset{0.0f};
        };
        std::vector<ProfileStep> profile;

        if (b <= 0.001f)
        {
            profile.push_back({0.0f, 0.0f});
            profile.push_back({height, 0.0f});
        }
        else if (bevelSegments <= 1)
        {
            profile.push_back({0.0f, b});
            profile.push_back({b, 0.0f});
            profile.push_back({height - b, 0.0f});
            profile.push_back({height, b});
        }
        else
        {
            int s = std::clamp(bevelSegments, 2, 8);
            const float pi = 3.14159265359f;
            // Bottom quarter-arc: theta from 0 to pi/2
            for (int k = 0; k <= s; ++k)
            {
                float theta = (pi * 0.5f) * static_cast<float>(k) / static_cast<float>(s);
                float y = b * (1.0f - std::cos(theta));
                float in = b * (1.0f - std::sin(theta));
                profile.push_back({y, in});
            }
            // Top quarter-arc: theta from 0 to pi/2
            for (int k = 0; k <= s; ++k)
            {
                float theta = (pi * 0.5f) * static_cast<float>(k) / static_cast<float>(s);
                float y = (height - b) + b * std::sin(theta);
                float in = b * (1.0f - std::cos(theta));
                profile.push_back({y, in});
            }
        }

        auto getRingVertex = [&](size_t ringIdx, size_t vertIdx) -> utils::math::Vec3f
        {
            float y = profile[ringIdx].y;
            float in = profile[ringIdx].inset;
            float vx = pts[vertIdx].x + in * miters[vertIdx].vx;
            float vz = pts[vertIdx].z + in * miters[vertIdx].vz;
            return {vx, y, vz};
        };

        // 6. Generate 3D Mesh
        const float topColor[3] = {0.92f, 0.76f, 0.35f};      // Polished Brass / Gold Top
        const float botColor[3] = {0.50f, 0.52f, 0.58f};      // Slate Steel / Cast Iron Bottom
        const float sideColor[3] = {0.20f, 0.72f, 0.85f};     // Anodized CAD Cyan / Teal
        const float chamferColor[3] = {0.95f, 0.82f, 0.40f};  // Polished Gold/Brass Highlight

        // A. Top Cap (Y = profile.back().y, normal = (0, 1, 0))
        uint32_t topBase = static_cast<uint32_t>(vertices.size());
        size_t topRingIdx = profile.size() - 1;
        for (size_t i = 0; i < n; ++i)
        {
            auto pt = getRingVertex(topRingIdx, i);
            vertices.push_back(
                {{pt.x, pt.y, pt.z}, {0.0f, 1.0f, 0.0f}, {topColor[0], topColor[1], topColor[2]}});
        }
        for (const auto& tri : capTriangles)
        {
            indices.push_back(topBase + tri[0]);
            indices.push_back(topBase + tri[2]);
            indices.push_back(topBase + tri[1]);
        }

        // B. Bottom Cap (Y = profile.front().y, normal = (0, -1, 0))
        uint32_t botBase = static_cast<uint32_t>(vertices.size());
        for (size_t i = 0; i < n; ++i)
        {
            auto pt = getRingVertex(0, i);
            vertices.push_back(
                {{pt.x, pt.y, pt.z}, {0.0f, -1.0f, 0.0f}, {botColor[0], botColor[1], botColor[2]}});
        }
        for (const auto& tri : capTriangles)
        {
            indices.push_back(botBase + tri[0]);
            indices.push_back(botBase + tri[1]);
            indices.push_back(botBase + tri[2]);
        }

        // C. Side and Bevel Bands between consecutive rings
        for (size_t r = 0; r + 1 < profile.size(); ++r)
        {
            bool isBevelBand = (profile[r].inset > 1e-4f || profile[r + 1].inset > 1e-4f);
            const float* bandColor = isBevelBand ? chamferColor : sideColor;

            for (size_t i = 0; i < n; ++i)
            {
                size_t j = (i + 1) % n;

                auto p00 = getRingVertex(r, i);
                auto p01 = getRingVertex(r, j);
                auto p11 = getRingVertex(r + 1, j);
                auto p10 = getRingVertex(r + 1, i);

                // Compute outward normal: (p10 - p00) x (p01 - p00)
                float vx = p10.x - p00.x;
                float vy = p10.y - p00.y;
                float vz = p10.z - p00.z;

                float ux = p01.x - p00.x;
                float uy = p01.y - p00.y;
                float uz = p01.z - p00.z;

                float nx = vy * uz - vz * uy;
                float ny = vz * ux - vx * uz;
                float nz = vx * uy - vy * ux;
                float nl = std::sqrt(nx * nx + ny * ny + nz * nz);
                if (nl > 1e-6f)
                {
                    nx /= nl;
                    ny /= nl;
                    nz /= nl;
                }

                uint32_t wBase = static_cast<uint32_t>(vertices.size());
                vertices.push_back({{p00.x, p00.y, p00.z},
                                    {nx, ny, nz},
                                    {bandColor[0], bandColor[1], bandColor[2]}});
                vertices.push_back({{p01.x, p01.y, p01.z},
                                    {nx, ny, nz},
                                    {bandColor[0], bandColor[1], bandColor[2]}});
                vertices.push_back({{p11.x, p11.y, p11.z},
                                    {nx, ny, nz},
                                    {bandColor[0], bandColor[1], bandColor[2]}});
                vertices.push_back({{p10.x, p10.y, p10.z},
                                    {nx, ny, nz},
                                    {bandColor[0], bandColor[1], bandColor[2]}});

                indices.push_back(wBase + 0);
                indices.push_back(wBase + 3);
                indices.push_back(wBase + 2);

                indices.push_back(wBase + 0);
                indices.push_back(wBase + 2);
                indices.push_back(wBase + 1);
            }
        }

        // 7. Watertight manifold construction
        if (outManifold != nullptr)
        {
            std::map<std::tuple<int64_t, int64_t, int64_t>, uint32_t> coordMap;
            std::vector<uint32_t> remap(vertices.size());
            std::vector<float> weldedVerts;

            for (size_t k = 0; k < vertices.size(); ++k)
            {
                const auto& pos = vertices[k].position;
                int64_t qx = static_cast<int64_t>(std::round(pos[0] * 100000.0f));
                int64_t qy = static_cast<int64_t>(std::round(pos[1] * 100000.0f));
                int64_t qz = static_cast<int64_t>(std::round(pos[2] * 100000.0f));
                auto key = std::make_tuple(qx, qy, qz);
                auto it = coordMap.find(key);
                if (it == coordMap.end())
                {
                    uint32_t newIdx = static_cast<uint32_t>(weldedVerts.size() / 3);
                    coordMap[key] = newIdx;
                    remap[k] = newIdx;
                    weldedVerts.push_back(pos[0]);
                    weldedVerts.push_back(pos[1]);
                    weldedVerts.push_back(pos[2]);
                }
                else
                {
                    remap[k] = it->second;
                }
            }

            std::vector<uint32_t> weldedIndices;
            weldedIndices.reserve(indices.size());
            for (size_t k = 0; k + 2 < indices.size(); k += 3)
            {
                uint32_t i0 = remap[indices[k]];
                uint32_t i1 = remap[indices[k + 1]];
                uint32_t i2 = remap[indices[k + 2]];
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
            *outManifold = manifold::Manifold(gl);
        }

        return {std::move(vertices), std::move(indices)};
    }

    // 10. 3D Revolve / Lathe Tool (Sweeps 2D profile around Y-axis)
    static MeshData createRevolvedPolygon(const std::vector<utils::math::Vec3f>& rawPts,
                                          float revolveAngleDeg = 360.0f, int revolveSegments = 36,
                                          float radialOffset = 0.4f,
                                          manifold::Manifold* outManifold = nullptr)
    {
        std::vector<rendering::Vertex> vertices;
        std::vector<uint32_t> indices;

        if (rawPts.size() < 3)
            return {std::move(vertices), std::move(indices)};

        // 1. Clean points: remove consecutive duplicates and closing duplicate
        std::vector<utils::math::Vec3f> pts;
        pts.reserve(rawPts.size());
        for (const auto& p : rawPts)
        {
            if (pts.empty())
            {
                pts.push_back({p.x, 0.0f, p.z});
            }
            else
            {
                float dx = p.x - pts.back().x;
                float dz = p.z - pts.back().z;
                if (std::hypot(dx, dz) > 1e-4f)
                {
                    pts.push_back({p.x, 0.0f, p.z});
                }
            }
        }
        if (pts.size() >= 3)
        {
            float dx = pts.front().x - pts.back().x;
            float dz = pts.front().z - pts.back().z;
            if (std::hypot(dx, dz) <= 1e-4f)
            {
                pts.pop_back();
            }
        }

        size_t n = pts.size();
        if (n < 3)
            return {std::move(vertices), std::move(indices)};

        // 2. Ensure consistent CCW winding in (X, Z) where 2A = sum(x_i * z_{i+1} - x_{i+1} * z_i)
        // > 0
        float signedArea2 = 0.0f;
        for (size_t i = 0; i < n; ++i)
        {
            size_t j = (i + 1) % n;
            signedArea2 += (pts[i].x * pts[j].z - pts[j].x * pts[i].z);
        }

        if (std::abs(signedArea2) < 1e-6f)
            return {std::move(vertices), std::move(indices)};

        if (signedArea2 < 0.0f)
        {
            std::reverse(pts.begin(), pts.end());
        }

        // 3. Map 2D profile to radial cross-section coordinates (r_i, h_i)
        float xMin = pts[0].x;
        float zMin = pts[0].z;
        for (const auto& p : pts)
        {
            if (p.x < xMin)
                xMin = p.x;
            if (p.z < zMin)
                zMin = p.z;
        }

        std::vector<float> r(n);
        std::vector<float> h(n);
        float safeOffset = std::max(0.05f, radialOffset);
        for (size_t i = 0; i < n; ++i)
        {
            r[i] = std::max(0.05f, (pts[i].x - xMin) + safeOffset);
            h[i] = pts[i].z - zMin;
        }

        // 4. Sweeping angle and steps
        float angleDeg = std::clamp(revolveAngleDeg, 15.0f, 360.0f);
        bool isFullCircle = (angleDeg >= 359.9f);
        int steps = std::clamp(revolveSegments, 8, 128);
        const float pi = 3.14159265359f;
        float totalAngleRad = angleDeg * (pi / 180.0f);

        // 5. Ear-clipping triangulation (used if partial revolution caps needed)
        auto cross2D = [](const utils::math::Vec3f& a, const utils::math::Vec3f& b,
                          const utils::math::Vec3f& c) -> float
        { return (b.x - a.x) * (c.z - a.z) - (b.z - a.z) * (c.x - a.x); };

        auto pointInTriangle = [&](const utils::math::Vec3f& a, const utils::math::Vec3f& b,
                                   const utils::math::Vec3f& c, const utils::math::Vec3f& p) -> bool
        {
            float c1 = cross2D(a, b, p);
            float c2 = cross2D(b, c, p);
            float c3 = cross2D(c, a, p);
            return (c1 >= -1e-6f && c2 >= -1e-6f && c3 >= -1e-6f);
        };

        std::vector<int> V(n);
        for (size_t i = 0; i < n; ++i)
            V[i] = static_cast<int>(i);

        std::vector<std::array<int, 3>> capTriangles;
        int count = static_cast<int>(n);
        int curr = 0;
        int maxIters = count * count * 2;
        int iters = 0;

        while (count > 3 && iters < maxIters)
        {
            iters++;
            int prev = (curr + count - 1) % count;
            int next = (curr + 1) % count;

            int iA = V[prev];
            int iB = V[curr];
            int iC = V[next];

            const auto& pA = pts[iA];
            const auto& pB = pts[iB];
            const auto& pC = pts[iC];

            bool isEar = cross2D(pA, pB, pC) > 1e-7f;
            if (isEar)
            {
                for (int k = 0; k < count; ++k)
                {
                    if (k == prev || k == curr || k == next)
                        continue;
                    int iK = V[k];
                    if (pointInTriangle(pA, pB, pC, pts[iK]))
                    {
                        isEar = false;
                        break;
                    }
                }
            }

            if (isEar)
            {
                capTriangles.push_back({iA, iB, iC});
                V.erase(V.begin() + curr);
                count--;
                if (curr >= count)
                    curr = 0;
            }
            else
            {
                curr = (curr + 1) % count;
            }
        }

        if (count == 3)
        {
            capTriangles.push_back({V[0], V[1], V[2]});
        }
        else if (count > 3)
        {
            for (int k = 1; k < count - 1; ++k)
            {
                capTriangles.push_back({V[0], V[k], V[k + 1]});
            }
        }

        // 6. Generate Swept Quad Bands
        const float copperColor[3] = {0.85f, 0.60f, 0.30f};  // CAD Copper / Bronze
        const float blueColor[3] = {0.35f, 0.65f, 0.95f};    // Anodized CAD Blue
        const float goldColor[3] = {0.92f, 0.76f, 0.35f};    // Polished Brass / Gold

        for (int s = 0; s < steps; ++s)
        {
            float theta0 = totalAngleRad * static_cast<float>(s) / static_cast<float>(steps);
            float theta1 = totalAngleRad * static_cast<float>(s + 1) / static_cast<float>(steps);
            float cos0 = std::cos(theta0);
            float sin0 = std::sin(theta0);
            float cos1 = std::cos(theta1);
            float sin1 = std::sin(theta1);

            for (size_t i = 0; i < n; ++i)
            {
                size_t j = (i + 1) % n;

                utils::math::Vec3f p00{r[i] * cos0, h[i], r[i] * sin0};
                utils::math::Vec3f p10{r[i] * cos1, h[i], r[i] * sin1};
                utils::math::Vec3f p11{r[j] * cos1, h[j], r[j] * sin1};
                utils::math::Vec3f p01{r[j] * cos0, h[j], r[j] * sin0};

                // Compute outward normal: (p01 - p00) x (p10 - p00)
                float vx = p01.x - p00.x;
                float vy = p01.y - p00.y;
                float vz = p01.z - p00.z;

                float ux = p10.x - p00.x;
                float uy = p10.y - p00.y;
                float uz = p10.z - p00.z;

                float nx = vy * uz - vz * uy;
                float ny = vz * ux - vx * uz;
                float nz = vx * uy - vy * ux;
                float nl = std::sqrt(nx * nx + ny * ny + nz * nz);
                if (nl > 1e-6f)
                {
                    nx /= nl;
                    ny /= nl;
                    nz /= nl;
                }

                const float* bandColor = ((i % 2) == 0) ? copperColor : blueColor;

                uint32_t wBase = static_cast<uint32_t>(vertices.size());
                vertices.push_back({{p00.x, p00.y, p00.z},
                                    {nx, ny, nz},
                                    {bandColor[0], bandColor[1], bandColor[2]}});
                vertices.push_back({{p10.x, p10.y, p10.z},
                                    {nx, ny, nz},
                                    {bandColor[0], bandColor[1], bandColor[2]}});
                vertices.push_back({{p11.x, p11.y, p11.z},
                                    {nx, ny, nz},
                                    {bandColor[0], bandColor[1], bandColor[2]}});
                vertices.push_back({{p01.x, p01.y, p01.z},
                                    {nx, ny, nz},
                                    {bandColor[0], bandColor[1], bandColor[2]}});

                indices.push_back(wBase + 0);
                indices.push_back(wBase + 3);
                indices.push_back(wBase + 2);

                indices.push_back(wBase + 0);
                indices.push_back(wBase + 2);
                indices.push_back(wBase + 1);
            }
        }

        // 7. Partial revolution caps (if < 360 deg)
        if (!isFullCircle)
        {
            // Start Face at theta = 0: normal = (0, 0, -1)
            uint32_t startBase = static_cast<uint32_t>(vertices.size());
            for (size_t i = 0; i < n; ++i)
            {
                vertices.push_back({{r[i], h[i], 0.0f},
                                    {0.0f, 0.0f, -1.0f},
                                    {goldColor[0], goldColor[1], goldColor[2]}});
            }
            for (const auto& tri : capTriangles)
            {
                indices.push_back(startBase + tri[0]);
                indices.push_back(startBase + tri[2]);
                indices.push_back(startBase + tri[1]);
            }

            // End Face at theta = totalAngleRad
            float cosEnd = std::cos(totalAngleRad);
            float sinEnd = std::sin(totalAngleRad);
            float nEnd[3] = {-sinEnd, 0.0f, cosEnd};

            uint32_t endBase = static_cast<uint32_t>(vertices.size());
            for (size_t i = 0; i < n; ++i)
            {
                vertices.push_back({{r[i] * cosEnd, h[i], r[i] * sinEnd},
                                    {nEnd[0], nEnd[1], nEnd[2]},
                                    {goldColor[0], goldColor[1], goldColor[2]}});
            }
            for (const auto& tri : capTriangles)
            {
                indices.push_back(endBase + tri[0]);
                indices.push_back(endBase + tri[1]);
                indices.push_back(endBase + tri[2]);
            }
        }

        // 8. Watertight manifold construction
        if (outManifold != nullptr)
        {
            std::map<std::tuple<int64_t, int64_t, int64_t>, uint32_t> coordMap;
            std::vector<uint32_t> remap(vertices.size());
            std::vector<float> weldedVerts;

            for (size_t k = 0; k < vertices.size(); ++k)
            {
                const auto& pos = vertices[k].position;
                int64_t qx = static_cast<int64_t>(std::round(pos[0] * 100000.0f));
                int64_t qy = static_cast<int64_t>(std::round(pos[1] * 100000.0f));
                int64_t qz = static_cast<int64_t>(std::round(pos[2] * 100000.0f));
                auto key = std::make_tuple(qx, qy, qz);
                auto it = coordMap.find(key);
                if (it == coordMap.end())
                {
                    uint32_t newIdx = static_cast<uint32_t>(weldedVerts.size() / 3);
                    coordMap[key] = newIdx;
                    remap[k] = newIdx;
                    weldedVerts.push_back(pos[0]);
                    weldedVerts.push_back(pos[1]);
                    weldedVerts.push_back(pos[2]);
                }
                else
                {
                    remap[k] = it->second;
                }
            }

            std::vector<uint32_t> weldedIndices;
            weldedIndices.reserve(indices.size());
            for (size_t k = 0; k + 2 < indices.size(); k += 3)
            {
                uint32_t i0 = remap[indices[k]];
                uint32_t i1 = remap[indices[k + 1]];
                uint32_t i2 = remap[indices[k + 2]];
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
            *outManifold = manifold::Manifold(gl);
        }

        return {std::move(vertices), std::move(indices)};
    }

    // 11. Feature Edge Wireframe Lines (LINE_LIST, normal={0,0,0} for unlit crisp rendering)
    static std::vector<rendering::Vertex> createWireframeLines(
        const std::vector<rendering::Vertex>& verts, const std::vector<uint32_t>& indices,
        const float color[3] = nullptr)
    {
        std::vector<rendering::Vertex> lines;
        if (indices.size() < 3)
            return lines;

        const float defaultColor[3] = {0.95f, 0.95f, 0.95f};  // Crisp high-contrast line
        const float* lineColor = color ? color : defaultColor;

        std::map<std::pair<uint32_t, uint32_t>, bool> uniqueEdges;

        for (size_t i = 0; i + 2 < indices.size(); i += 3)
        {
            uint32_t i0 = indices[i];
            uint32_t i1 = indices[i + 1];
            uint32_t i2 = indices[i + 2];

            auto addEdge = [&](uint32_t a, uint32_t b)
            {
                if (a > b)
                    std::swap(a, b);
                uniqueEdges[{a, b}] = true;
            };

            addEdge(i0, i1);
            addEdge(i1, i2);
            addEdge(i2, i0);
        }

        for (const auto& edgePair : uniqueEdges)
        {
            uint32_t a = edgePair.first.first;
            uint32_t b = edgePair.first.second;
            if (a < verts.size() && b < verts.size())
            {
                rendering::Vertex va = verts[a];
                rendering::Vertex vb = verts[b];
                va.normal[0] = va.normal[1] = va.normal[2] = 0.0f;
                vb.normal[0] = vb.normal[1] = vb.normal[2] = 0.0f;
                va.color[0] = vb.color[0] = lineColor[0];
                va.color[1] = vb.color[1] = lineColor[1];
                va.color[2] = vb.color[2] = lineColor[2];
                lines.push_back(va);
                lines.push_back(vb);
            }
        }

        return lines;
    }
};

}  // namespace geometry
