# 6. Lighting

On Vulkan, OpenGL and Metal, scenes are lit with metallic-roughness PBR, the
glTF model: GGX highlights, height-correlated Smith visibility and Schlick
Fresnel. The software rasterizer lights albedo with its directional and
ambient lights only, Gouraud-style; `INK_FLAG_CHECK(r->caps(), RendererCaps::Pbr)`
says which you have. These lights belong to the built-in shaders: a pipeline of
your own ([chapter 16](16-custom-pipelines.md)) lights however it likes.

## Lights: one list

```cpp
namespace aura3d::gfx {
enum class LightType : u32 { Directional, Point, Spot, Ambient };
struct Light {
    LightType type = LightType::Directional;
    glm::vec3 position{0.0f};                // point, spot
    glm::vec3 direction{0.5f, -1.0f, 0.3f};  // directional, spot: the way light travels
    glm::vec3 color{1.0f};
    float intensity = 1.0f;
    float range = 10.0f;                     // point, spot
    float innerAngle = 0.0f;                 // spot, radians from direction
    float outerAngle = 0.0f;
};
}
```

```cpp
constexpr glm::vec3 kWarmWhite{1.0f, 0.98f, 0.92f};
const std::array lights{
    gfx::Light{.direction = {-0.4f, -1.0f, -0.5f}, .color = kWarmWhite},             // a sun
    gfx::Light{.type = gfx::LightType::Point, .position = {-1.5f, 1.2f, 1.5f},
               .color = {1.0f, 0.4f, 0.2f}, .intensity = 3.0f, .range = 6.0f},
    gfx::Light{.type = gfx::LightType::Spot, .position = {2.0f, 2.5f, -1.0f},
               .direction = glm::normalize(glm::vec3{-1.0f, -1.5f, 0.6f}),
               .color = {0.3f, 0.6f, 1.0f}, .intensity = 6.0f, .range = 8.0f,
               .innerAngle = 0.25f, .outerAngle = 0.45f},
    gfx::Light{.type = gfx::LightType::Ambient, .color = kWarmWhite, .intensity = 0.18f}};
r->setLights(lights);
```

| Note | |
|---|---|
| `setLights()` replaces every light | no sun unless you put a directional light in the list |
| default, before any call | one default `Light` and a white ambient at 0.15 |
| capacity | the first `gfx::kMaxLights` (8) count |
| `direction` | where light *travels*: `{0, -1, 0}` shines straight down |
| point and spot falloff | inverse square, windowed to zero at `range`; a spot eases from `innerAngle` to `outerAngle` |
| directional intensity 1 | lights a rough, non-metallic surface facing it to its albedo, as the old Lambert shader did |

`r->lights()` returns the list in effect. Every draw in a frame sees the same lights.

## Materials

`Material` carries the PBR inputs (see
[05-meshes-materials-textures.md](05-meshes-materials-textures.md#materials)):
`tint`, `roughness`, `metallic` and `emissive` as factors, and the albedo,
normal, metallic-roughness and emissive maps that scale them.

## Unlit

An ambient light alone, at intensity 1, shows every albedo as authored.
`apps/OrgLogo` displays its baked texture this way:

```cpp
r->setLights(std::array{gfx::Light{.type = gfx::LightType::Ambient}});
```

## Tuning ambient vs. direct light

- **Ambient** raises the floor everywhere, including surfaces facing away
  from every light.
- **Directional, point and spot** intensity scales direct light.

`apps/Sandbox`'s lighting (ambient 0.18, sun 1.0) is a reasonable start for
an outdoor scene. Shadows and post-processing are not part of the engine:
[chapter 17](17-shadows-and-post-processing.md).

Next: **[07-2d-rendering-and-text.md](07-2d-rendering-and-text.md)**.
