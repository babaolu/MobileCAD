#pragma once

#include "utils/math/Vec3.h"
#include "utils/math/Mat4.h"
#include <algorithm>
#include <cmath>

namespace rendering {

class Camera {
public:
    utils::math::Vec3f m_target{0.0f, 0.0f, 0.0f};
    float m_yaw{0.785398f};   // 45 deg in radians
    float m_pitch{0.523599f}; // 30 deg in radians
    float m_distance{4.0f};
    float m_fovY{0.785398f};  // 45 deg in radians
    float m_zNear{0.1f};
    float m_zFar{100.0f};

    Camera() = default;

    utils::math::Vec3f getEyePosition() const noexcept {
        float cosPitch = std::cos(m_pitch);
        float sinPitch = std::sin(m_pitch);
        float cosYaw = std::cos(m_yaw);
        float sinYaw = std::sin(m_yaw);

        float x = m_target.x + m_distance * cosPitch * sinYaw;
        float y = m_target.y + m_distance * sinPitch;
        float z = m_target.z + m_distance * cosPitch * cosYaw;
        return utils::math::Vec3f(x, y, z);
    }

    void orbit(float deltaX, float deltaY) noexcept {
        m_yaw -= deltaX * 0.005f;
        m_pitch += deltaY * 0.005f;
        m_pitch = std::clamp(m_pitch, -1.48f, 1.48f);
    }

    void zoom(float scaleFactor) noexcept {
        m_distance *= scaleFactor;
        m_distance = std::clamp(m_distance, 0.5f, 100.0f);
    }

    void setViewIso() noexcept {
        m_target = utils::math::Vec3f(0.0f, 0.5f, 0.0f);
        m_yaw = 0.785398f;   // 45 deg
        m_pitch = 0.523599f; // 30 deg
        m_distance = 4.0f;
    }

    void setViewTop() noexcept {
        m_yaw = 0.0f;
        m_pitch = 1.48f;
    }

    void setViewFront() noexcept {
        m_yaw = 0.0f;
        m_pitch = 0.0f;
    }

    void setViewRight() noexcept {
        m_yaw = 1.570796f;
        m_pitch = 0.0f;
    }

    void resetView() noexcept {
        setViewIso();
    }


    void pan(float deltaX, float deltaY, float viewportHeight) noexcept {
        if (viewportHeight <= 0.0f) return;

        utils::math::Vec3f eye = getEyePosition();
        utils::math::Vec3f forward = (m_target - eye).normalized();
        utils::math::Vec3f worldUp(0.0f, 1.0f, 0.0f);
        utils::math::Vec3f right = forward.cross(worldUp).normalized();
        utils::math::Vec3f up = right.cross(forward).normalized();

        float factor = m_distance / viewportHeight;
        utils::math::Vec3f shift = (right * (-deltaX) + up * deltaY) * factor;
        m_target += shift;
    }

    utils::math::Mat4f getViewProjectionMatrix(float aspect) const noexcept {
        utils::math::Vec3f eye = getEyePosition();
        utils::math::Vec3f up(0.0f, 1.0f, 0.0f);
        auto proj = utils::math::Mat4f::perspective(m_fovY, aspect, m_zNear, m_zFar);
        auto view = utils::math::Mat4f::lookAt(eye, m_target, up);
        return proj * view;
    }
};

} // namespace rendering
