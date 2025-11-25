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

#include "aura.hpp"

namespace aura3d {

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
struct Vertex3d
{
    glm::vec3 pos;      // (x, y, z)
    glm::vec2 texCoord; // (u, v)
    glm::vec4 color;    // (r, g, b, a)
    glm::vec3 normal;   // (nx, ny, nz)
};

/**
 * @struct Vertex2d
 * @brief Base struct for 2D geometry (Sprites, UI).
 */
struct Vertex2d {
    glm::vec2 pos;      // (x, y)
    glm::vec2 texCoord; // (u, v)
    glm::vec4 color;    // (r, g, b, a)
};

/**
 * @struct Mesh
 * @brief Uma estrutura de dados de CPU que armazena geometria.
 * Pode ser usada para renderização de software ou enviada para a GPU.
 * @tparam VertexType O tipo de vértice (Vertex2d ou Vertex3d).
 */
template <typename VertexType>
struct Mesh {
    std::vector<VertexType> vertices;
    std::vector<u32> indices; // u32 para compatibilidade com Vulkan
};

}

#endif
