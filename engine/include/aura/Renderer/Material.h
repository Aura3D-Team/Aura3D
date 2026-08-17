#ifndef AURA_MATERIAL_H
#define AURA_MATERIAL_H

#pragma once

#include <glm/glm.hpp>

#include "aura/Renderer/RenderHandles.h"

namespace aura3d {

/**
 * @struct Material
 * @brief Defines how a surface looks and reflects light (Visuals only).
 *
 * @note The built-in shaders currently consume @c albedo together with the
 *       directional light; @c tint, @c roughness and @c metallic are carried
 *       through the renderer state and exposed to callers, but are not yet read
 *       by the default GLSL. They are here so the shading model can grow
 *       without another API break.
 */
struct Material {
    /// Albedo texture. An invalid handle leaves whatever texture is already bound.
    TextureHandle albedo; //! The base color image/texture.
    glm::vec4 tint = {1.0f, 1.0f, 1.0f, 1.0f}; //! Color multiplier applied over the texture.
    float roughness = 0.5f; //! How shiny it is (0.0 = mirror, 1.0 = matte).
    float metallic = 0.0f; //! What it's made of (0.0 = plastic/wood, 1.0 = pure metal).
};

} // namespace aura3d

#endif // AURA_MATERIAL_H
