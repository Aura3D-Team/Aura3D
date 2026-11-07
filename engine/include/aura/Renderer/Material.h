#ifndef AURA_MATERIAL_H
#define AURA_MATERIAL_H

#pragma once

#include <glm/glm.hpp>

#include "aura/Renderer/RenderHandles.h"

namespace aura3d
{

/**
 * @struct Material
 * @brief Defines how a surface looks and reflects light (Visuals only).
 *
 * Metallic-roughness PBR on the GPU backends, glTF's model: a map multiplies its factor. The
 * software backend lights albedo with the directional light alone (see RendererCaps::Pbr).
 * The defaults, rough and non-metallic, light an albedo much as plain Lambert would.
 */
struct Material
{
    /// Albedo texture. An invalid handle leaves whatever texture is already bound.
    TextureHandle albedo;                      //! The base color image/texture.
    glm::vec4 tint = {1.0f, 1.0f, 1.0f, 1.0f}; //! Color multiplier applied over the texture.
    float roughness = 1.0f;                    //! How shiny it is (0.0 = mirror, 1.0 = matte).
    float metallic = 0.0f;                     //! What it's made of (0.0 = plastic/wood, 1.0 = pure metal).
    TextureHandle normalMap;                   //! Tangent space, +Y up the image (glTF, OpenGL), Rgba8Unorm.
    TextureHandle metallicRoughness;           //! Green roughness, blue metallic, scaling the factors (glTF).
    TextureHandle emissiveMap;                 //! Scales @c emissive.
    glm::vec3 emissive{0.0f};                  //! Light the surface gives off, unaffected by the lights.
};

} // namespace aura3d

#endif // AURA_MATERIAL_H
