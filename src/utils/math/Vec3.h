#pragma once
#include <cmath>
#include <iostream>

namespace utils::math {

template<typename T>
struct Vec3 {
    T x{0}, y{0}, z{0};

    Vec3() = default;
    constexpr Vec3(T x, T y, T z) noexcept : x(x), y(y), z(z) {}

    // Accessors
    constexpr T& operator[](size_t i) noexcept {
        return *(&x + i);
    }
    constexpr const T& operator[](size_t i) const noexcept {
        return *(&x + i);
    }

    // Arithmetic
    constexpr Vec3 operator+(const Vec3& other) const noexcept {
        return Vec3(x + other.x, y + other.y, z + other.z);
    }

    constexpr Vec3 operator-(const Vec3& other) const noexcept {
        return Vec3(x - other.x, y - other.y, z - other.z);
    }

    constexpr Vec3 operator*(T scalar) const noexcept {
        return Vec3(x * scalar, y * scalar, z * scalar);
    }

    constexpr Vec3 operator/(T scalar) const noexcept {
        return Vec3(x / scalar, y / scalar, z / scalar);
    }

    Vec3& operator+=(const Vec3& other) noexcept {
        x += other.x; y += other.y; z += other.z; return *this;
    }

    Vec3& operator-=(const Vec3& other) noexcept {
        x -= other.x; y -= other.y; z -= other.z; return *this;
    }

    Vec3& operator*=(T scalar) noexcept {
        x *= scalar; y *= scalar; z *= scalar; return *this;
    }

    Vec3& operator/=(T scalar) noexcept {
        x /= scalar; y /= scalar; z /= scalar; return *this;
    }

    // Comparison
    constexpr bool operator==(const Vec3& other) const noexcept {
        return x == other.x && y == other.y && z == other.z;
    }

    constexpr bool operator!=(const Vec3& other) const noexcept {
        return !(*this == other);
    }

    // Utility
    constexpr T dot(const Vec3& other) const noexcept {
        return x * other.x + y * other.y + z * other.z;
    }

    constexpr Vec3 cross(const Vec3& other) const noexcept {
        return Vec3(
            y * other.z - z * other.y,
            z * other.x - x * other.z,
            x * other.y - y * other.x
        );
    }

    constexpr T lengthSquared() const noexcept {
        return dot(*this);
    }

    constexpr T length() const noexcept {
        return std::sqrt(lengthSquared());
    }

    Vec3& normalize() {
        T len = length();
        if (len > 0) {
            *this /= len;
        }
        return *this;
    }

    Vec3 normalized() const {
        Vec3 result = *this;
        result.normalize();
        return result;
    }
};

using Vec3f = Vec3<float>;
using Vec3d = Vec3<double>;
using Vec3i = Vec3<int>;

template<typename T>
std::ostream& operator<<(std::ostream& os, const Vec3<T>& v) {
    return os << "Vec3(" << v.x << ", " << v.y << ", " << v.z << ")";
}

} // namespace utils::math
