#ifndef AURACORE_H
#define AURACORE_H

#pragma once

#define GLM_FORCE_RADIANS
#ifdef USE_VULKAN_API
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#endif
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <vector>

#include "aura/aura.h"

namespace aura3d {
namespace gfx {

////////////////////////////////////
//// Geometry
////////////////////////////////////
/**
 * @struct TransformUBO
 * @brief Uniform buffer object (UBO).
 * Reference to 'layout(set = 0, binding = 0) uniform' in shader.
 */
struct TransformUBO {
    glm::mat4 model;
    glm::mat4 view;
    glm::mat4 proj;
};

/**
 * @struct Vertex3d
 * @brief Base struct for 3D geometry.
 */
struct Vertex3D {
    glm::vec3 pos;      // (x, y, z)
    glm::vec2 texCoord; // (u, v)
    glm::vec4 color;    // (r, g, b, a)
    glm::vec3 normal;   // (nx, ny, nz)
};

/**
 * @struct Vertex2d
 * @brief Base struct for 2D geometry (Sprites, UI).
 */
struct Vertex2D {
    glm::vec2 pos;      // (x, y)
    glm::vec2 texCoord; // (u, v)
    glm::vec4 color;    // (r, g, b, a)
};

/**
 * @struct Mesh
 * @brief CPU-side geometry storage.
 *
 * This can be used directly by the CPU renderer or uploaded to GPU buffers.
 */
template <typename VertexType>
struct Mesh {
    std::vector<VertexType> vertices;
    std::vector<u32> indices;

    [[nodiscard]]
    bool empty() const noexcept {
        return vertices.empty() || indices.empty();
    }

    void clear() noexcept {
        vertices.clear();
        indices.clear();
    }
};

using Mesh2D = Mesh<Vertex2D>;
using Mesh3D = Mesh<Vertex3D>;

/**
 * @struct FragmentInput2D
 * @brief Data passed to the CPU fragment shader stage for 2D rendering.
 */
struct FragmentInput2D {
    glm::vec2 screenPos{};
    glm::vec2 texCoord{};
    glm::vec4 color{1.0f};
};

/**
 * @struct FragmentInput3D
 * @brief Data passed to the CPU fragment shader stage.
 */
struct FragmentInput3D {
    glm::vec3 worldPos;      // Interpolated position for lighting calculations
    glm::vec2 texCoord;      // Interpolated U,V texturing channels
    glm::vec4 color;         // Interpolated vertex colors
    glm::vec3 normal;        // Interpolated surface normal vector
    float perspectiveW;      // 1/W element for perspective-correct interpolation
};

}
}

#endif
