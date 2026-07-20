#ifndef AURA_CAMERA_H
#define AURA_CAMERA_H

#pragma once

#include <algorithm>
#include <cmath>

#include "aura/Core/AuraCore.h"

namespace aura3d {

/**
 * @class Camera
 * @brief Handles 3D camera calculations (View & Projection matrices).
 * * Supports two control modes:
 * 1. Target Mode: Locks onto a specific point in space using lookAt().
 * 2. Free-Look Mode: Rotates using FPS-style angles using setRotation().
 */
class Camera {
public:
    //! Settings for 3D Perspective view (objects get smaller farther away).
    struct PerspectiveDesc {
        float fovDeg = 45.0f; //! Field of view in degrees (lens angle width).
        float aspect = 16.0f / 9.0f; //! Screen width / height (prevents stretching).
        float nearZ  = 0.1f; //! Min render distance (must be > 0).
        float farZ   = 100.0f; //! Max render distance.
    };

    //! Settings for 2D/Isometric Orthographic view (no depth shrinking).
    struct OrthoDesc {
        float left   = -1.0f; //! Left boundary of camera view box.
        float right  =  1.0f; //! Right boundary of camera view box.
        float bottom = -1.0f; //! Bottom boundary of camera view box.
        float top    =  1.0f; //! Top boundary of camera view box.
        float nearZ  = -1.0f; //! Front boundary of camera view box.
        float farZ   =  1.0f; //! Back boundary of camera view box.
    };

    //! Factory method: Creates a 3D Perspective Camera.
    static Camera perspective(const PerspectiveDesc& desc)
    {
        Camera camera;
        camera._projection = glm::perspective(glm::radians(desc.fovDeg), desc.aspect, desc.nearZ, desc.farZ);

        // Vulkan's Y-axis points down (opposite of OpenGL), so flip Y scale.
#ifdef USE_VULKAN_API
        camera._projection[1][1] *= -1.0f;
#endif
        return camera;
    }

    //! Factory method: Creates a 2D/Isometric Orthographic Camera.
    static Camera ortho(const OrthoDesc& desc)
    {
        Camera camera;
        camera._projection = glm::ortho(desc.left, desc.right, desc.bottom, desc.top, desc.nearZ, desc.farZ);

#ifdef USE_VULKAN_API
        camera._projection[1][1] *= -1.0f;
#endif
        return camera;
    }

    //! Updates camera position in 3D world space.
    void setPosition(const glm::vec3& position) noexcept
    {
        _position = position;
        _viewDirty = true;
    }

    //! Target Mode: Points camera at a specific point in space.
    void lookAt(const glm::vec3& target, const glm::vec3& up = {0.0f, 1.0f, 0.0f}) noexcept
    {
        _target = target;
        _up = up;
        _useTarget = true;
        _viewDirty = true;
    }

    //! Free-Look Mode: Rotates camera using Yaw (left/right) and Pitch (up/down).
    void setRotation(float yawDeg, float pitchDeg) noexcept
    {
        _yaw = yawDeg;
        //! Clamp pitch to +/-89.9 deg to prevent upside-down camera flipping glitches.
        _pitch = std::clamp(pitchDeg, -89.9f, 89.9f);
        _useTarget = false;
        _viewDirty = true;
    }

    //! Gets current world position.
    const glm::vec3& position() const noexcept { return _position; }

    //! Calculates normalized 3D vector of where the camera is facing.
    glm::vec3 forward() const noexcept
    {
        //! Calculate forward vector based on active mode
        if (_useTarget) 
        {
            const glm::vec3 delta = _target - _position;
            const float len = glm::length(delta);
            return len > 0.0f ? delta / len : glm::vec3(0.0f, 0.0f, -1.0f);
        }

        const float yaw = glm::radians(_yaw);
        const float pitch = glm::radians(_pitch);
        return glm::normalize(glm::vec3(
            std::cos(pitch) * std::cos(yaw),
            std::sin(pitch),
            std::cos(pitch) * std::sin(yaw))
        );
    }

    //! Returns view matrix (recalculates only if position/rotation changed).
    glm::mat4 viewMatrix() const noexcept
    {
        if (_viewDirty) 
        {
            _view = glm::lookAt(_position, _position + forward(), _up);
            _viewDirty = false;
        }
        return _view;
    }

    /// Returns projection matrix.
    glm::mat4 projectionMatrix() const noexcept { return _projection; }

    /// Bundles matrices into a single structure ready for GPU shaders.
    gfx::TransformUBO buildUBO(const glm::mat4& modelMatrix = glm::mat4(1.0f)) const noexcept
    {
        gfx::TransformUBO ubo{};
        ubo.model = modelMatrix;
        ubo.view  = viewMatrix();
        ubo.proj  = _projection;
        return ubo;
    }

private:
    glm::mat4 _projection{1.0f}; //! Matrix converting 3D view space to 2D screen space.
    mutable glm::mat4 _view{1.0f}; //! Matrix orienting world relative to camera location.
    mutable bool _viewDirty = true; //! True if camera moved/rotated and view matrix needs rebuilding.

    glm::vec3 _position{0.0f, 0.0f, 2.0f}; //! Camera location in world space.
    glm::vec3 _target{0.0f, 0.0f, 0.0f}; //! Point camera looks at (Target Mode).
    glm::vec3 _up{0.0f, 1.0f, 0.0f}; //! World's upward direction vector (Y-Up).

    float _yaw = -90.0f; //! Horizontal look angle in degrees (-90 = forward).
    float _pitch = 0.0f; //! Vertical look angle in degrees (up/down).
    bool _useTarget = true; //! Toggle: true = Target Mode, false = Free-Look Mode.
};

} // namespace aura3d

#endif // AURA_CAMERA_H