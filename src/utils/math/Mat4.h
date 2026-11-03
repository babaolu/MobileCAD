#pragma once
#include <array>
#include <cmath>

#include "Vec3.h"

namespace utils::math
{

template <typename T>
struct Mat4
{
    std::array<T, 16> m{};

    constexpr Mat4() noexcept : m{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1} {}

    // Row-major access: m[row * 4 + col]
    constexpr T& operator()(size_t row, size_t col) noexcept { return m[row * 4 + col]; }

    constexpr const T& operator()(size_t row, size_t col) const noexcept
    {
        return m[row * 4 + col];
    }

    // Identity
    static constexpr Mat4 identity() noexcept { return Mat4(); }

    // Translation
    static Mat4 translation(T x, T y, T z) noexcept
    {
        Mat4 result = identity();
        result(0, 3) = x;
        result(1, 3) = y;
        result(2, 3) = z;
        return result;
    }

    static Mat4 translation(const Vec3<T>& v) noexcept { return translation(v.x, v.y, v.z); }

    // Scaling
    static Mat4 scaling(T x, T y, T z) noexcept
    {
        Mat4 result = identity();
        result(0, 0) = x;
        result(1, 1) = y;
        result(2, 2) = z;
        return result;
    }

    static Mat4 scaling(const Vec3<T>& v) noexcept { return scaling(v.x, v.y, v.z); }

    // Rotation around X axis
    static Mat4 rotationX(T angleRad) noexcept
    {
        Mat4 result = identity();
        T c = std::cos(angleRad);
        T s = std::sin(angleRad);
        result(1, 1) = c;
        result(1, 2) = -s;
        result(2, 1) = s;
        result(2, 2) = c;
        return result;
    }

    // Rotation around Y axis
    static Mat4 rotationY(T angleRad) noexcept
    {
        Mat4 result = identity();
        T c = std::cos(angleRad);
        T s = std::sin(angleRad);
        result(0, 0) = c;
        result(0, 2) = s;
        result(2, 0) = -s;
        result(2, 2) = c;
        return result;
    }

    // Rotation around Z axis
    static Mat4 rotationZ(T angleRad) noexcept
    {
        Mat4 result = identity();
        T c = std::cos(angleRad);
        T s = std::sin(angleRad);
        result(0, 0) = c;
        result(0, 1) = -s;
        result(1, 0) = s;
        result(1, 1) = c;
        return result;
    }

    // Perspective projection (Vulkan clip space: +Y down, depth [0, 1])
    static Mat4 perspective(T fovyRad, T aspect, T zNear, T zFar) noexcept
    {
        Mat4 result{};
        T tanHalfFovy = std::tan(fovyRad / T(2));
        result(0, 0) = T(1) / (aspect * tanHalfFovy);
        result(1, 1) = -T(1) / tanHalfFovy;    // Vulkan +Y is down
        result(2, 2) = zFar / (zNear - zFar);  // Vulkan [0, 1] depth
        result(2, 3) = (zFar * zNear) / (zNear - zFar);
        result(3, 2) = T(-1);
        result(3, 3) = T(0);
        return result;
    }

    // Look-at view matrix
    static Mat4 lookAt(const Vec3<T>& eye, const Vec3<T>& center, const Vec3<T>& up) noexcept
    {
        Vec3<T> f = (center - eye).normalized();
        Vec3<T> s = f.cross(up).normalized();
        Vec3<T> u = s.cross(f);

        Mat4 result = identity();
        result(0, 0) = s.x;
        result(0, 1) = s.y;
        result(0, 2) = s.z;
        result(0, 3) = -s.dot(eye);
        result(1, 0) = u.x;
        result(1, 1) = u.y;
        result(1, 2) = u.z;
        result(1, 3) = -u.dot(eye);
        result(2, 0) = -f.x;
        result(2, 1) = -f.y;
        result(2, 2) = -f.z;
        result(2, 3) = f.dot(eye);
        return result;
    }

    // Arithmetic
    constexpr Mat4 operator+(const Mat4& other) const noexcept
    {
        Mat4 result;
        for (size_t i = 0; i < 16; ++i)
            result.m[i] = m[i] + other.m[i];
        return result;
    }

    constexpr Mat4 operator-(const Mat4& other) const noexcept
    {
        Mat4 result;
        for (size_t i = 0; i < 16; ++i)
            result.m[i] = m[i] - other.m[i];
        return result;
    }

    constexpr Mat4 operator*(T scalar) const noexcept
    {
        Mat4 result;
        for (size_t i = 0; i < 16; ++i)
            result.m[i] = m[i] * scalar;
        return result;
    }

    constexpr Mat4 operator*(const Mat4& other) const noexcept
    {
        Mat4 result;
        for (size_t row = 0; row < 4; ++row)
        {
            for (size_t col = 0; col < 4; ++col)
            {
                T sum = 0;
                for (size_t k = 0; k < 4; ++k)
                {
                    sum += (*this)(row, k) * other(k, col);
                }
                result(row, col) = sum;
            }
        }
        return result;
    }

    Mat4& operator*=(const Mat4& other) noexcept { return *this = *this * other; }

    // Multiply Vec3 (treat as point, w=1)
    Vec3<T> operator*(const Vec3<T>& v) const noexcept
    {
        T x = (*this)(0, 0) * v.x + (*this)(0, 1) * v.y + (*this)(0, 2) * v.z + (*this)(0, 3);
        T y = (*this)(1, 0) * v.x + (*this)(1, 1) * v.y + (*this)(1, 2) * v.z + (*this)(1, 3);
        T z = (*this)(2, 0) * v.x + (*this)(2, 1) * v.y + (*this)(2, 2) * v.z + (*this)(2, 3);
        T w = (*this)(3, 0) * v.x + (*this)(3, 1) * v.y + (*this)(3, 2) * v.z + (*this)(3, 3);
        if (std::abs(w) > T(1e-7))
        {
            return Vec3<T>(x / w, y / w, z / w);
        }
        return Vec3<T>(x, y, z);
    }

    // Multiply Vec3 as direction (w=0, no translation)
    Vec3<T> transformDirection(const Vec3<T>& v) const noexcept
    {
        T x = (*this)(0, 0) * v.x + (*this)(0, 1) * v.y + (*this)(0, 2) * v.z;
        T y = (*this)(1, 0) * v.x + (*this)(1, 1) * v.y + (*this)(1, 2) * v.z;
        T z = (*this)(2, 0) * v.x + (*this)(2, 1) * v.y + (*this)(2, 2) * v.z;
        return Vec3<T>(x, y, z);
    }

    // Transpose
    constexpr Mat4 transposed() const noexcept
    {
        Mat4 result;
        for (size_t r = 0; r < 4; ++r)
        {
            for (size_t c = 0; c < 4; ++c)
            {
                result(c, r) = (*this)(r, c);
            }
        }
        return result;
    }

    // Matrix Inversion (Cofactor / Adjugate)
    Mat4 inverted() const noexcept
    {
        const T* m = this->m.data();
        T inv[16];

        inv[0] = m[5] * m[10] * m[15] - m[5] * m[11] * m[14] - m[9] * m[6] * m[15] +
                 m[9] * m[7] * m[14] + m[13] * m[6] * m[11] - m[13] * m[7] * m[10];

        inv[4] = -m[4] * m[10] * m[15] + m[4] * m[11] * m[14] + m[8] * m[6] * m[15] -
                 m[8] * m[7] * m[14] - m[12] * m[6] * m[11] + m[12] * m[7] * m[10];

        inv[8] = m[4] * m[9] * m[15] - m[4] * m[11] * m[13] - m[8] * m[5] * m[15] +
                 m[8] * m[7] * m[13] + m[12] * m[5] * m[11] - m[12] * m[7] * m[9];

        inv[12] = -m[4] * m[9] * m[14] + m[4] * m[10] * m[13] + m[8] * m[5] * m[14] -
                  m[8] * m[6] * m[13] - m[12] * m[5] * m[10] + m[12] * m[6] * m[9];

        inv[1] = -m[1] * m[10] * m[15] + m[1] * m[11] * m[14] + m[9] * m[2] * m[15] -
                 m[9] * m[3] * m[14] - m[13] * m[2] * m[11] + m[13] * m[3] * m[10];

        inv[5] = m[0] * m[10] * m[15] - m[0] * m[11] * m[14] - m[8] * m[2] * m[15] +
                 m[8] * m[3] * m[14] + m[12] * m[2] * m[11] - m[12] * m[3] * m[10];

        inv[9] = -m[0] * m[9] * m[15] + m[0] * m[11] * m[13] + m[8] * m[1] * m[15] -
                 m[8] * m[3] * m[13] - m[12] * m[1] * m[11] + m[12] * m[3] * m[9];

        inv[13] = m[0] * m[9] * m[14] - m[0] * m[10] * m[13] - m[8] * m[1] * m[14] +
                  m[8] * m[2] * m[13] + m[12] * m[1] * m[10] - m[12] * m[2] * m[9];

        inv[2] = m[1] * m[6] * m[15] - m[1] * m[7] * m[14] - m[5] * m[2] * m[15] +
                 m[5] * m[3] * m[14] + m[13] * m[2] * m[7] - m[13] * m[3] * m[6];

        inv[6] = -m[0] * m[6] * m[15] + m[0] * m[7] * m[14] + m[4] * m[2] * m[15] -
                 m[4] * m[3] * m[14] - m[12] * m[2] * m[7] + m[12] * m[3] * m[6];

        inv[10] = m[0] * m[5] * m[15] - m[0] * m[7] * m[13] - m[4] * m[1] * m[15] +
                  m[4] * m[3] * m[13] + m[12] * m[1] * m[7] - m[12] * m[3] * m[5];

        inv[14] = -m[0] * m[5] * m[14] + m[0] * m[6] * m[13] + m[4] * m[1] * m[14] -
                  m[4] * m[2] * m[13] - m[12] * m[1] * m[6] + m[12] * m[2] * m[5];

        inv[3] = -m[1] * m[6] * m[11] + m[1] * m[7] * m[10] + m[5] * m[2] * m[11] -
                 m[5] * m[3] * m[10] - m[9] * m[2] * m[7] + m[9] * m[3] * m[6];

        inv[7] = m[0] * m[6] * m[11] - m[0] * m[7] * m[10] - m[4] * m[2] * m[11] +
                 m[4] * m[3] * m[10] + m[8] * m[2] * m[7] - m[8] * m[3] * m[6];

        inv[11] = -m[0] * m[5] * m[11] + m[0] * m[7] * m[9] + m[4] * m[1] * m[11] -
                  m[4] * m[3] * m[9] - m[8] * m[1] * m[7] + m[8] * m[3] * m[5];

        inv[15] = m[0] * m[5] * m[10] - m[0] * m[6] * m[9] - m[4] * m[1] * m[10] +
                  m[4] * m[2] * m[9] + m[8] * m[1] * m[6] - m[8] * m[2] * m[5];

        T det = m[0] * inv[0] + m[1] * inv[4] + m[2] * inv[8] + m[3] * inv[12];
        if (std::abs(det) < T(1e-8))
        {
            return Mat4::identity();
        }

        T invDet = T(1) / det;
        Mat4 result;
        for (size_t i = 0; i < 16; ++i)
        {
            result.m[i] = inv[i] * invDet;
        }
        return result;
    }
};

using Mat4f = Mat4<float>;
using Mat4d = Mat4<double>;

template <typename T>
std::ostream& operator<<(std::ostream& os, const Mat4<T>& m)
{
    os << "Mat4:\n";
    for (size_t r = 0; r < 4; ++r)
    {
        os << "  ";
        for (size_t c = 0; c < 4; ++c)
        {
            os << m(r, c) << " ";
        }
        os << "\n";
    }
    return os;
}

}  // namespace utils::math
