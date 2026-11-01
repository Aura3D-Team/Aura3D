#ifndef AURACORE_H
#define AURACORE_H

#pragma once

#define GLM_FORCE_RADIANS

/*
 * Not GLM_FORCE_DEPTH_ZERO_TO_ONE: backends disagree on clip-space depth
 * (Vulkan wants [0,1], OpenGL/software want [-1,1]) and the choice is made at
 * runtime by switchBackend(), so a compile-time global can't be right for
 * both. Camera::setClipSpace() instead picks perspectiveRH_ZO vs _NO per
 * projection, from the live backend.
 */

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <vector>

#include "aura/aura.h"

namespace aura3d
{
namespace gfx
{

/// Geometry

/**
 * @struct TransformUBO
 * @brief Uniform buffer object (UBO).
 * Reference to 'layout(set = 0, binding = 0) uniform' in shader.
 */
struct TransformUBO
{
    glm::mat4 model;
    glm::mat4 view;
    glm::mat4 proj;
};

enum class LightType : u32
{
    Directional, //!< From infinitely far along direction: a sun or a moon.
    Point,
    Spot,
    Ambient, //!< A flat term on every surface: color * intensity times its albedo.
};

/// One light for IRenderer::setLights(). Each type reads only the fields it needs.
struct Light
{
    LightType type = LightType::Directional;
    glm::vec3 position{0.0f};               //! Point, spot.
    glm::vec3 direction{0.5f, -1.0f, 0.3f}; //! Directional, spot: the way the light travels.
    glm::vec3 color{1.0f};
    float intensity = 1.0f;
    float range = 10.0f;     //! Point, spot: distance at which it has faded to nothing.
    float innerAngle = 0.0f; //! Spot: radians from the axis at full strength.
    float outerAngle = 0.0f; //! Spot: radians from the axis where it ends.
};

inline constexpr u32 kMaxLights = 8;

/// The scene shaders' lighting block, std140.
struct LightingUBO
{
    u32 count = 0;
    u32 _pad[3] = {};
    struct GpuLight
    {
        glm::vec4 positionRange{0.0f};
        glm::vec4 colorIntensity{0.0f};
        glm::vec4 directionCosOuter{0.0f}; //! cos(outer); -1 for anything but a spot.
        glm::vec4 params{0.0f};            //! x cos(inner), y the LightType.
    } lights[kMaxLights];
};
static_assert(sizeof(LightingUBO) == 528, "std140 layout of the lighting block");

/// One material as the PBR shaders read it per draw; std140 and std430 agree on this layout.
struct MaterialUniforms
{
    static constexpr u32 kNormalMap = 1u << 0u;
    static constexpr u32 kMetallicRoughnessMap = 1u << 1u;
    static constexpr u32 kEmissiveMap = 1u << 2u;

    glm::vec4 tint{1.0f};
    glm::vec3 emissive{0.0f};
    float roughness = 1.0f;
    float metallic = 0.0f;
    u32 maps = 0; //! Which of the k*Map textures are bound.
    //! Bindless indices on Vulkan; the other backends bind these to texture units 1-3 instead.
    u32 normalMap = 0;
    u32 metallicRoughnessMap = 0;
    u32 emissiveMap = 0;
    u32 _pad[3] = {};
};
static_assert(sizeof(MaterialUniforms) == 64, "std430 layout of the material table");

/**
 * @struct Vertex3d
 * @brief Base struct for 3D geometry.
 */
struct Vertex3D
{
    glm::vec3 pos;      //! (x, y, z)
    glm::vec2 texCoord; //! (u, v)
    glm::vec4 color;    //! (r, g, b, a)
    glm::vec3 normal;   //! (nx, ny, nz)
};

/// Unlit geometry for IRenderer::drawBatch(), in either BatchSpace.
struct BatchVertex
{
    glm::vec3 pos;
    glm::vec2 texCoord;
    glm::vec4 color;
};

enum class BatchSpace : u8
{
    Screen, ///< Render-target pixels from the top-left; z is depth, 0 on the near plane, 1 on the far.
    World,  ///< Through the current model, view and projection.
};

/**
 * @struct Mesh
 * @brief CPU-side geometry storage.
 *
 * This can be used directly by the CPU renderer or uploaded to GPU buffers.
 */
template <typename VertexType> struct Mesh
{
    std::vector<VertexType> vertices;
    std::vector<u32> indices;

    [[nodiscard]]
    bool empty() const noexcept
    {
        return vertices.empty() || indices.empty();
    }

    void clear() noexcept
    {
        vertices.clear();
        indices.clear();
    }
};

using Mesh3D = Mesh<Vertex3D>;

/**
 * @struct FragmentInput2D
 * @brief Data passed to the CPU fragment shader stage for 2D rendering.
 */
struct FragmentInput2D
{
    glm::vec2 screenPos{};
    glm::vec2 texCoord{};
    glm::vec4 color{1.0f};
};

/**
 * @struct FragmentInput3D
 * @brief Data passed to the CPU fragment shader stage.
 */
struct FragmentInput3D
{
    glm::vec3 worldPos; //! Interpolated position for lighting calculations
    glm::vec2 texCoord; //! Interpolated U,V texturing channels
    glm::vec4 color;    //! Interpolated vertex colors
    glm::vec3 normal;   //! Interpolated surface normal vector
    float perspectiveW; //! 1/W element for perspective-correct interpolation
};

} // namespace gfx
} // namespace aura3d

#endif
