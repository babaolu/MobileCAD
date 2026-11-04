#pragma once
#include <algorithm>
#include <cmath>
#include <utility>

#include "Mat4.h"
#include "Vec3.h"

namespace utils::math
{

struct Ray
{
    Vec3f origin;
    Vec3f dir;

    static Ray screenPointToRay(float screenX, float screenY, float screenW, float screenH,
                                const Mat4f& viewProj)
    {
        // Screen to Vulkan NDC: x in [-1, 1], y in [-1, 1] (+Y down)
        float ndcX = (2.0f * screenX / screenW) - 1.0f;
        float ndcY = (2.0f * screenY / screenH) - 1.0f;

        Mat4f invViewProj = viewProj.inverted();

        // Vulkan near depth is 0.0, far depth is 1.0
        Vec3f nearNdc{ndcX, ndcY, 0.0f};
        Vec3f farNdc{ndcX, ndcY, 1.0f};

        Vec3f worldNear = invViewProj * nearNdc;
        Vec3f worldFar = invViewProj * farNdc;

        Ray ray;
        ray.origin = worldNear;
        ray.dir = (worldFar - worldNear).normalized();
        return ray;
    }

    static bool intersectAABB(const Ray& ray, const Vec3f& boxMin, const Vec3f& boxMax, float& tHit)
    {
        float tmin = 0.0f;
        float tmax = 1e30f;

        // X slab
        if (std::abs(ray.dir.x) < 1e-8f)
        {
            if (ray.origin.x < boxMin.x || ray.origin.x > boxMax.x)
                return false;
        }
        else
        {
            float invD = 1.0f / ray.dir.x;
            float t1 = (boxMin.x - ray.origin.x) * invD;
            float t2 = (boxMax.x - ray.origin.x) * invD;
            if (t1 > t2)
                std::swap(t1, t2);
            tmin = std::max(tmin, t1);
            tmax = std::min(tmax, t2);
            if (tmin > tmax)
                return false;
        }

        // Y slab
        if (std::abs(ray.dir.y) < 1e-8f)
        {
            if (ray.origin.y < boxMin.y || ray.origin.y > boxMax.y)
                return false;
        }
        else
        {
            float invD = 1.0f / ray.dir.y;
            float t1 = (boxMin.y - ray.origin.y) * invD;
            float t2 = (boxMax.y - ray.origin.y) * invD;
            if (t1 > t2)
                std::swap(t1, t2);
            tmin = std::max(tmin, t1);
            tmax = std::min(tmax, t2);
            if (tmin > tmax)
                return false;
        }

        // Z slab
        if (std::abs(ray.dir.z) < 1e-8f)
        {
            if (ray.origin.z < boxMin.z || ray.origin.z > boxMax.z)
                return false;
        }
        else
        {
            float invD = 1.0f / ray.dir.z;
            float t1 = (boxMin.z - ray.origin.z) * invD;
            float t2 = (boxMax.z - ray.origin.z) * invD;
            if (t1 > t2)
                std::swap(t1, t2);
            tmin = std::max(tmin, t1);
            tmax = std::min(tmax, t2);
            if (tmin > tmax)
                return false;
        }

        tHit = tmin;
        return true;
    }

    static bool intersectTriangle(const Ray& ray, const Vec3f& v0, const Vec3f& v1, const Vec3f& v2,
                                  float& tOut) noexcept
    {
        const Vec3f e1 = v1 - v0;
        const Vec3f e2 = v2 - v0;
        const Vec3f p = ray.dir.cross(e2);
        const float det = e1.dot(p);

        if (std::abs(det) < 1e-7f)
            return false;

        const float invDet = 1.0f / det;
        const Vec3f tvec = ray.origin - v0;
        const float u = tvec.dot(p) * invDet;
        if (u < 0.0f || u > 1.0f)
            return false;

        const Vec3f q = tvec.cross(e1);
        const float v = ray.dir.dot(q) * invDet;
        if (v < 0.0f || (u + v) > 1.0f)
            return false;

        const float t = e2.dot(q) * invDet;
        if (t > 1e-5f)
        {
            tOut = t;
            return true;
        }
        return false;
    }
};

}  // namespace utils::math
