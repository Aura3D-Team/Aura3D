# 4. Camera

`aura3d::Camera` (`aura/Core/Camera/Camera.h`) is a small, header-only,
value-type class that builds view and projection matrices. It knows nothing
about input or the renderer beyond one thing: which backend's clip-space
convention to target.

## Clip space — handled automatically

Vulkan uses `[0,1]` depth and Y-down NDC; OpenGL uses `[-1,1]` depth and
Y-up. `Engine` calls `Camera::setClipSpace(...)` for you right after the
renderer is created, matching whatever backend actually ended up active —
you don't need to call this yourself in normal usage. It's a process-wide
static setting because every `Camera` you construct afterward reads it, and
because `Engine::switchBackend` needs to be able to flip it later.

## Creating a camera

Cameras are built through two factory functions, not a constructor —
`Camera`'s default constructor is private:

```cpp
// 3D: perspective (things shrink with distance)
Camera camera = Camera::perspective({
    .fovDeg = 60.0f,
    .aspect = static_cast<float>(width) / static_cast<float>(height),
    .nearZ  = 0.1f,
    .farZ   = 100.0f,
});

// 2D / UI / isometric: orthographic (no perspective shrink)
Camera ui = Camera::ortho({
    .left = 0.0f, .right = 800.0f,
    .bottom = 0.0f, .top = 600.0f,
    .nearZ = -1.0f, .farZ = 1.0f,
});
```

`nearZ` must be strictly greater than 0 for a perspective camera — as it
approaches 0 the depth-buffer precision degrades sharply ("z-fighting").

## Positioning: two mutually exclusive modes

**Target mode** (default) — the camera always faces a fixed point, however
it moves. Good for orbit/inspection cameras, cutscenes, following an object:

```cpp
camera.setPosition({0.0f, 5.0f, 10.0f});
camera.lookAt({0.0f, 0.0f, 0.0f});        // stare at the origin
```

**Free-look mode** — FPS-style yaw/pitch angles, independent of any target
point. This is what `apps/Sandbox` uses for its mouse-look camera:

```cpp
camera.setRotation(-90.0f, 0.0f);  // yaw, pitch (degrees) — (-90, 0) faces -Z
```

Calling `setRotation` switches the camera into free-look mode; calling
`lookAt` switches it back to target mode. Pitch is clamped to `±89.9°`
internally to avoid the gimbal-lock singularity at exactly ±90°.

`setPosition` works in both modes — in target mode it also changes the
facing direction (since forward is re-derived from `target - position` every
time), in free-look mode it doesn't.

```cpp
const glm::vec3& pos = camera.position();
glm::vec3 fwd = camera.forward();   // unit vector, works in either mode
```

## Feeding the renderer

```cpp
r->setTransform(camera.buildUBO(modelMatrix));
```

`buildUBO` packages `model` (defaults to identity), the camera's `view`
matrix (lazily rebuilt only when the camera actually moved), and its fixed
`projection` matrix into a `gfx::TransformUBO` — exactly what
`IRenderer::setTransform` expects. Call it once per object per frame with
that object's own model matrix; see `apps/Sandbox/main.cpp` for the pattern
of one `setTransform` + `drawMesh` pair per scene object.

## A moving camera (from `apps/Sandbox`)

```cpp
mouse.setCursorEnabled(false);
mouse.setMoveAction(wma::MouseAction{[&](const wma::WMAMousePosition& pos) {
    camYaw   += static_cast<float>(pos.deltaX) * kMouseSensitivity;
    camPitch -= static_cast<float>(pos.deltaY) * kMouseSensitivity;
    camera.setRotation(camYaw, camPitch);
}});

// in the per-frame callback:
const glm::vec3 forward = camera.forward();
const glm::vec3 right = glm::normalize(glm::cross(forward, glm::vec3(0, 1, 0)));
glm::vec3 moveDir{0.0f};
if (moveForward) moveDir += forward;
if (moveRight)   moveDir += right;
// ...
if (glm::length(moveDir) > 0.0f)
    camera.setPosition(camera.position() + glm::normalize(moveDir) * kMoveSpeed * dt);
```

See [08-input.md](08-input.md) for the keyboard/mouse binding API this
relies on.

Next: **[05-meshes-materials-textures.md](05-meshes-materials-textures.md)**.
