#ifndef AURA_CAMERA_H
#define AURA_CAMERA_H

#pragma once

#include <algorithm>
#include <cmath>

#include <glm/ext/matrix_clip_space.hpp>

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
    /**
     * @brief Clip-space convention of the active graphics backend.
     *
     * Vulkan uses a [0,1] depth range and a Y-down NDC; OpenGL uses [-1,1] depth
     * and Y-up. The projection matrix must match the backend actually rendering,
     * so the convention is a process-wide runtime setting (the engine binary can
     * carry several backends and switch between them) rather than a compile-time
     * choice. glm's explicit @c *ZO / @c *NO builders let a single binary produce
     * the right matrix at runtime without the global GLM_FORCE_DEPTH_ZERO_TO_ONE
     * macro, which could only ever encode one convention.
     */
    enum class ClipSpace { OpenGL, Vulkan };

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

    /**
     * @brief Selects the clip-space convention every future camera will target.
     *
     * Call this once at startup to match the active backend (see @ref ClipSpace).
     * It only affects the depth remap chosen inside @c perspective / @c ortho:
     * Vulkan maps depth to @c [0,1], OpenGL to @c [-1,1]. Getting it wrong leaves
     * geometry either clipped away or depth-tested backwards.
     *
     * @param clipSpace The convention of the backend currently rendering.
     */
    static void setClipSpace(ClipSpace clipSpace) noexcept
    {
        _clipSpace = clipSpace;
    }

    /**
     * @brief Factory method: builds a 3D perspective camera.
     *
     * @par The math
     * A perspective projection mimics a real lens: things farther away look
     * smaller. It maps the camera's view frustum (a truncated pyramid) into the
     * cube-shaped clip space the GPU rasterises. The key trick is the *perspective
     * divide*: the matrix writes the view-space depth @c -z into the @c w output
     * component, and after the vertex shader the GPU divides x, y and z by @c w.
     * Dividing by depth is exactly what makes distant objects shrink.
     *
     * With @c f = 1 / tan(fovY / 2) (the "focal length" from the vertical field
     * of view) and aspect @c a = width / height, the right-handed matrix is:
     * @code
     *   | f/a   0        0            0     |
     *   |  0    f        0            0     |   // (row for OpenGL [-1,1] depth)
     *   |  0    0   (n+f)/(n-f)   2nf/(n-f) |
     *   |  0    0       -1            0     |   // <- puts -z into w
     * @endcode
     * @c f/a in the top-left is what corrects for a non-square window: a wider
     * screen (larger @c a) divides the horizontal scale down so circles stay
     * circular instead of stretching into ellipses. The bottom row's @c -1 is the
     * source of the perspective divide. The Vulkan variant differs only in the
     * depth row, remapping @c z into @c [0,1] instead of @c [-1,1] (see
     * @ref ClipSpace and @c setClipSpace).
     *
     * @param desc Frustum description (field of view, aspect ratio, near/far).
     * @return A camera whose projection matrix is configured; the view matrix is
     *         still identity until you position/orient it.
     *
     * @note @c nearZ must be strictly > 0. As @c nearZ -> 0 the @c 2nf/(n-f) term
     *       blows up, wrecking depth-buffer precision (the classic "z-fighting").
     *
     * @par Example
     * @code
     * // A 60-degree lens for a 1920x1080 window, seeing from 0.1 to 500 units.
     * auto cam = Camera::perspective({.fovDeg = 60.0f,
     *                                 .aspect = 1920.0f / 1080.0f,
     *                                 .nearZ  = 0.1f,
     *                                 .farZ   = 500.0f});
     * // A point 10 units straight ahead projects to w = 10; after the divide its
     * // on-screen size is scaled by 1/10 relative to a point 1 unit ahead.
     * @endcode
     */
    static Camera perspective(const PerspectiveDesc& desc)
    {
        Camera camera;

        if (_clipSpace == ClipSpace::Vulkan)
            //! Vulkan: [0,1] depth
            camera._projection = glm::perspectiveRH_ZO(glm::radians(desc.fovDeg), desc.aspect, desc.nearZ, desc.farZ);
        else
            //! OpenGL: [-1,1] depth, Y up.
            camera._projection = glm::perspectiveRH_NO(glm::radians(desc.fovDeg), desc.aspect, desc.nearZ, desc.farZ);

        return camera;
    }

    /**
     * @brief Rebuilds the projection matrix in place (e.g. on a window resize).
     *
     * Same math as @c perspective, but mutates this camera's @c _projection
     * instead of returning a new one -- position/rotation/view state are
     * untouched, so a live camera can pick up a new aspect ratio without
     * losing where it is or which way it's looking.
     *
     * @param desc Frustum description (field of view, aspect ratio, near/far).
     */
    void setPerspective(const PerspectiveDesc& desc) noexcept
    {
        if (_clipSpace == ClipSpace::Vulkan)
            _projection = glm::perspectiveRH_ZO(glm::radians(desc.fovDeg), desc.aspect, desc.nearZ, desc.farZ);
        else
            _projection = glm::perspectiveRH_NO(glm::radians(desc.fovDeg), desc.aspect, desc.nearZ, desc.farZ);
    }

    /**
     * @brief Factory method: builds a 2D / isometric orthographic camera.
     *
     * @par The math
     * An orthographic projection has *no* perspective divide, so parallel lines
     * stay parallel and depth never shrinks anything — ideal for 2D, UI, CAD and
     * isometric games. It simply takes the axis-aligned box
     * @c [left,right] x [bottom,top] x [near,far] and linearly rescales it onto
     * clip space. Each axis is a "shift then scale": subtract the box centre, then
     * divide by the half-extent so the range lands on @c [-1,1]:
     * @code
     *   | 2/(r-l)     0        0      -(r+l)/(r-l) |
     *   |   0      2/(t-b)     0      -(t+b)/(t-b) |   // OpenGL [-1,1] depth
     *   |   0         0    -2/(f-n)   -(f+n)/(f-n) |
     *   |   0         0        0            1      |   // w stays 1 -> no divide
     * @endcode
     * The @c 2/(r-l) terms are the *scale* (box width -> 2 units of clip space);
     * the last column is the *translation* that recentres the box on the origin.
     * Because the bottom row is @c (0 0 0 1), @c w is always 1 and nothing is
     * divided by depth. The Vulkan variant only changes the depth row to map
     * @c z into @c [0,1].
     *
     * @param desc The six planes of the view box.
     * @return A camera configured for orthographic projection.
     *
     * @par Example
     * @code
     * // Map an 800x600 screen so 1 world unit == 1 pixel, origin at bottom-left.
     * auto ui = Camera::ortho({.left = 0.0f, .right = 800.0f,
     *                          .bottom = 0.0f, .top = 600.0f,
     *                          .nearZ = -1.0f, .farZ = 1.0f});
     * // World point (400,300) lands dead centre of the screen (clip-space 0,0);
     * // its distance from the camera has no effect on its size.
     * @endcode
     */
    static Camera ortho(const OrthoDesc& desc)
    {
        Camera camera;

        if (_clipSpace == ClipSpace::Vulkan)
            camera._projection = glm::orthoRH_ZO(desc.left, desc.right, desc.bottom, desc.top, desc.nearZ, desc.farZ);
        else
            camera._projection = glm::orthoRH_NO(desc.left, desc.right, desc.bottom, desc.top, desc.nearZ, desc.farZ);

        return camera;
    }

    /**
     * @brief Moves the camera (its eye point @c P) in world space.
     *
     * @par The math
     * Only the translation part of the view transform depends on @c P (see the
     * @f$-\hat{r}\!\cdot\!P@f$ column in @c viewMatrix), so this just stores the
     * new eye and flags the cached view matrix stale. In Target Mode moving the
     * eye also changes the facing direction, since @c forward is derived from
     * @f$T - P@f$; in Free-Look Mode the orientation is unaffected.
     *
     * @param position New eye location in world space.
     */
    void setPosition(const glm::vec3& position) noexcept
    {
        _position = position;
        _viewDirty = true; //! Invalidate cache; view matrix rebuilds on next query.
    }

    /**
     * @brief Target Mode: aim the camera at a fixed world-space point.
     *
     * @par The math
     * This stores a target point @c T; the facing direction is derived on demand
     * (see @c forward) as the normalised vector from the eye @c P to the target:
     * @f$ \hat{f} = (T - P) / \lVert T - P \rVert @f$. Because the direction is
     * recomputed from @c P and @c T each frame, the camera keeps staring at @c T
     * even as it moves around it — perfect for orbit/inspection cameras.
     *
     * The @p up hint does *not* have to be exactly perpendicular to the view
     * direction: the view-matrix build (see @c viewMatrix) re-orthogonalises it
     * with two cross products, so any @p up that is not parallel to @c (T - P)
     * works. It only chooses which way is "up" on screen (i.e. the roll).
     *
     * @param target World-space point to look at.
     * @param up     Approximate world up direction; defaults to +Y.
     *
     * @par Example
     * @code
     * cam.setPosition({0, 5, 10});
     * cam.lookAt({0, 0, 0});          // stare at the origin from above/behind
     * // forward() now returns normalize((0,0,0)-(0,5,10)) = (0,-0.447,-0.894).
     * @endcode
     */
    void lookAt(const glm::vec3& target, const glm::vec3& up = {0.0f, 1.0f, 0.0f}) noexcept
    {
        _target = target;
        _up = up;
        _useTarget = true;
        _viewDirty = true;
    }

    /**
     * @brief Free-Look Mode: orient the camera with FPS-style yaw and pitch.
     *
     * @par The math
     * Instead of a target point, the facing direction is described with two
     * angles in a spherical coordinate system:
     *  - @b yaw   (@f$\psi@f$): rotation around the world Y (up) axis — turning
     *    left/right, like shaking your head "no".
     *  - @b pitch (@f$\theta@f$): rotation around the horizontal axis — looking
     *    up/down, like nodding "yes".
     *
     * @c forward converts these spherical angles into a Cartesian unit vector:
     * @f[
     *   \hat{f} = \big(\cos\theta\cos\psi,\; \sin\theta,\; \cos\theta\sin\psi\big)
     * @f]
     * Intuition: @c cos(pitch) is the length of the direction's shadow on the XZ
     * (ground) plane, and yaw spins that shadow around; @c sin(pitch) lifts it up
     * or down. At @c pitch=0 the vector is perfectly horizontal.
     *
     * Pitch is clamped to @f$\pm89.9^\circ@f$ on purpose: at exactly @f$\pm90^\circ@f$
     * the forward vector becomes @c (0,\pm1,0), i.e. parallel to the up axis, and
     * the cross products inside @c viewMatrix collapse (gimbal lock), flipping the
     * image. Stopping just short avoids that singularity.
     *
     * @param yawDeg   Horizontal angle in degrees (@c -90 faces down -Z by default).
     * @param pitchDeg Vertical angle in degrees; clamped to @c [-89.9, 89.9].
     *
     * @par Example
     * @code
     * cam.setRotation(-90.0f, 0.0f);   // forward = (cos0*cos(-90), sin0, cos0*sin(-90))
     *                                  //         = (0, 0, -1)  -> looking down -Z
     * cam.setRotation(0.0f, 0.0f);     // forward = (1, 0, 0)   -> looking down +X
     * cam.setRotation(-90.0f, 45.0f);  // forward = (0, 0.707, -0.707) -> 45 deg up
     * @endcode
     */
    void setRotation(float yawDeg, float pitchDeg) noexcept
    {
        _yaw = yawDeg;
        //! Clamp pitch to +/-89.9 deg to prevent upside-down camera flipping glitches.
        _pitch = std::clamp(pitchDeg, -89.9f, 89.9f);
        _up = {0.0f, 1.0f, 0.0f};
        _useTarget = false;
        _viewDirty = true;
    }

    //! Gets the current eye position @c P in world space (the point @c viewMatrix
    //! translates to the origin).
    [[nodiscard]] const glm::vec3& position() const noexcept { return _position; }

    /**
     * @brief Unit vector describing where the camera is facing.
     *
     * @par The math
     * The result is always a *normalised* (length-1) direction, computed two ways
     * depending on the active mode:
     *
     * - @b Target @b Mode: subtract eye from target and divide by the distance,
     *   @f$ \hat{f} = (T - P) / \lVert T - P \rVert @f$. Normalising matters
     *   because later cross products assume a unit basis; skipping it would scale
     *   the whole view matrix. If @c T and @c P coincide the length is ~0 and
     *   there is no meaningful direction, so we fall back to @c (0,0,-1) instead
     *   of dividing by zero (which would yield NaNs).
     *
     * - @b Free-Look @b Mode: convert the spherical (yaw, pitch) angles to
     *   Cartesian, @f$ \hat{f} = (\cos\theta\cos\psi, \sin\theta,
     *   \cos\theta\sin\psi) @f$. This is already unit-length analytically; the
     *   @c glm::normalize call just cleans up floating-point drift.
     *
     * @return A unit-length forward direction in world space.
     *
     * @par Example
     * @code
     * cam.setPosition({0,0,0});
     * cam.lookAt({0,0,-5});   // forward() == (0, 0, -1)
     * @endcode
     */
    [[nodiscard]] glm::vec3 forward() const noexcept
    {
        //! Calculate forward vector based on active mode
        if (_useTarget)
        {
            //! Target mode: direction = (target - eye), then scale to unit length.
            const glm::vec3 delta = _target - _position;
            const float len = glm::length(delta);
            //! Guard the divide-by-zero when eye and target coincide (len ~ 0).
            return len > std::numeric_limits<float>::epsilon() ? delta / len : glm::vec3(0.0f, 0.0f, -1.0f);
        }

        //! Free-look mode: spherical (yaw, pitch) -> Cartesian unit vector.
        const float yaw = glm::radians(_yaw);
        const float pitch = glm::radians(_pitch);
        return glm::normalize(glm::vec3(
            std::cos(pitch) * std::cos(yaw), //! X: horizontal shadow, swung by yaw.
            std::sin(pitch),                 //! Y: vertical lift from pitch.
            std::cos(pitch) * std::sin(yaw)) //! Z: horizontal shadow, swung by yaw.
        );
    }

    /**
     * @brief Returns the view matrix, rebuilding it lazily when the camera moved.
     *
     * @par The math
     * The view matrix transforms world-space coordinates into *camera space*: the
     * frame where the eye sits at the origin looking down its own axis. It is the
     * inverse of the camera's world transform, but @c glm::lookAt builds it
     * directly and cheaply. Given eye @c P, forward @f$\hat{f}@f$ and world up
     * @c u, it constructs an orthonormal basis with two cross products:
     * @f[
     *   \hat{r} = \hat{f} \times u, \quad
     *   \hat{u} = \hat{r} \times \hat{f}
     * @f]
     * (@f$\hat{r}@f$ = right, @f$\hat{u}@f$ = true up, re-derived so it is exactly
     * perpendicular even if the supplied @c u was only approximate). The matrix
     * then packs these axes as rows and appends @f$-\hat{r}\!\cdot\!P@f$ etc. — the
     * rotation aligns the world to the camera's axes, and the dot-product column
     * translates the eye to the origin:
     * @code
     *   |  r.x   r.y   r.z   -dot(r,P) |
     *   |  u.x   u.y   u.z   -dot(u,P) |
     *   | -f.x  -f.y  -f.z    dot(f,P) |   // -f: RH cameras look down -Z
     *   |   0     0     0        1     |
     * @endcode
     *
     * @par Caching
     * Rebuilding is only done when @c _viewDirty is set (by @c setPosition,
     * @c lookAt or @c setRotation), so repeated calls in a frame are free. The
     * method is @c const yet mutates the cached matrix — hence @c _view and
     * @c _viewDirty are @c mutable; observers see no logical state change.
     *
     * @return Reference to the (up-to-date) view matrix.
     */
    [[nodiscard]] glm::mat4& viewMatrix() const noexcept
    {
        if (_viewDirty) 
        {
            _view = glm::lookAt(_position, _position + forward(), _up);
            _viewDirty = false;
        }
        return _view;
    }

    /**
     * @brief Returns the projection matrix built by @c perspective / @c ortho.
     *
     * This matrix is fixed once the camera is created (it only depends on the
     * lens/frustum, not on where the camera is), so it is returned directly with
     * no recomputation. It is the second half of the classic
     * @f$ clip = P_{proj} \cdot V_{view} \cdot M_{model} \cdot vertex @f$ pipeline.
     *
     * @return Const reference to the projection matrix.
     */
    [[nodiscard]] const glm::mat4& projectionMatrix() const noexcept { return _projection; }

    /**
     * @brief Bundles model, view and projection into one GPU-ready struct.
     *
     * @par The math
     * The GPU transforms each vertex by chaining three matrices, applied
     * right-to-left:
     * @f[
     *   v_{clip} = P_{proj} \cdot V_{view} \cdot M_{model} \cdot v_{local}
     * @f]
     *  - @b model  @c M: places the object in the world (its own transl/rotate/scale);
     *  - @b view   @c V: moves the world into camera space (from @c viewMatrix);
     *  - @b proj   @c P: projects camera space into clip space (from @c projectionMatrix).
     * The shader typically stores this as three separate matrices (rather than a
     * pre-multiplied one) so lighting can be done in world or view space.
     *
     * @param modelMatrix Per-object model matrix; defaults to identity (object at
     *                    the world origin, unrotated, unit scale).
     * @return A @c gfx::TransformUBO ready to upload to a uniform buffer.
     *
     * @par Example
     * @code
     * auto model = glm::translate(glm::mat4(1.0f), {2, 0, 0}); // shift 2 on +X
     * gfx::TransformUBO ubo = cam.buildUBO(model);
     * renderer.upload(ubo);
     * @endcode
     */
    [[nodiscard]] gfx::TransformUBO buildUBO(const glm::mat4& modelMatrix = glm::mat4(1.0f)) const noexcept
    {
        gfx::TransformUBO ubo{};
        ubo.model = modelMatrix;
        ubo.view = viewMatrix();
        ubo.proj = _projection;
        return ubo;
    }

private:
    Camera() = default;

    glm::mat4 _projection{1.0f}; //! Matrix converting 3D view space to 2D screen space.
    mutable glm::mat4 _view{1.0f}; //! Matrix orienting world relative to camera location.
    mutable bool _viewDirty = true; //! True if camera moved/rotated and view matrix needs rebuilding.

    glm::vec3 _position{0.0f, 0.0f, 2.0f}; //! Camera location in world space.
    glm::vec3 _target{0.0f, 0.0f, 0.0f}; //! Point camera looks at (Target Mode).
    glm::vec3 _up{0.0f, 1.0f, 0.0f}; //! World's upward direction vector (Y-Up).

    float _yaw = -90.0f; //! Horizontal look angle in degrees (-90 = forward).
    float _pitch = 0.0f; //! Vertical look angle in degrees (up/down).
    bool _useTarget = true; //! Toggle: true = Target Mode, false = Free-Look Mode.

    //! Backend clip-space convention
   static inline ClipSpace _clipSpace = ClipSpace::OpenGL;
};

} // namespace aura3d

#endif // AURA_CAMERA_H