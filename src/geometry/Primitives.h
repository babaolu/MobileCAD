#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
#include <utility>
#include <vector>

#include "manifold/manifold.h"
#include "rendering/MeshBuffer.h"

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

    // 8. Feature Edge Wireframe Lines (LINE_LIST, normal={0,0,0} for unlit crisp rendering)
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
