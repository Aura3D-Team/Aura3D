# 6. Lighting

Aura3D's built-in shading model is a single directional light — think
sunlight: one direction, no falloff with distance, applied uniformly across
the whole scene. It's implemented identically in spirit on all three
backends: Gouraud (per-vertex) shading on the CPU rasterizer, a uniform
block on OpenGL, a descriptor set on Vulkan.

## `gfx::LightUBO`

```cpp
namespace aura3d::gfx {
struct LightUBO {
    glm::vec3 direction = {0.5f, -1.0f, 0.3f}; // direction the light travels
    float intensity = 1.0f;                    // diffuse multiplier
    glm::vec4 color = {1, 1, 1, 1};
    float ambient = 0.15f;                     // flat term added everywhere
};
}
```

`direction` is the direction light *travels* (e.g. `{0, -1, 0}` is a sun
directly overhead), not the direction toward the light — this matches GLSL
convention and means you don't need to negate it yourself.

## Setting it

```cpp
gfx::LightUBO light;
light.direction = {-0.4f, -1.0f, -0.5f};
light.color     = {1.0f, 0.98f, 0.92f, 1.0f}; // warm white
light.intensity = 1.0f;
light.ambient   = 0.18f;
r->setLight(light);

// later, if you need to read it back:
const gfx::LightUBO& current = r->getLight();
```

Set it once at startup, or any time it should change — an update pushes the
new values to whatever the backend uses to store it (a GPU buffer on
Vulkan/OpenGL; a plain member the CPU rasterizer reads per-vertex). There's
no per-object override — every draw in a frame is lit by the same light.

## The "flat/unlit" trick

Because shading is `albedo * (ambient + diffuse * intensity)`, setting
`intensity = 0` and `ambient = 1` makes every surface show its albedo
texture exactly as authored, with no lighting influence at all. This is how
`apps/OrgLogo` displays its procedurally baked texture at its true colors:

```cpp
gfx::LightUBO light;
light.intensity = 0.0f;
light.ambient   = 1.0f;
r->setLight(light);
```

Useful for UI-like or fully pre-lit content where you don't want the scene
light touching it.

## Tuning ambient vs. intensity

- **`ambient`** raises the floor everywhere, including surfaces facing away
  from the light — raise it so shadowed faces aren't pure black.
- **`intensity`** scales how much the diffuse (facing-the-light) term
  contributes — this is what makes lit faces noticeably brighter than
  shadowed ones.

`apps/Sandbox`'s scene lighting (`ambient = 0.18`, `intensity = 1.0`) is a
reasonable starting point for a normally-lit outdoor scene.

Next: **[07-2d-rendering-and-text.md](07-2d-rendering-and-text.md)**.
