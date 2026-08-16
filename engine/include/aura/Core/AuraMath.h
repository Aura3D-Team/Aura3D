#ifndef AURA_MATH_H
#define AURA_MATH_H

#pragma once

#include <cmath>

#include <glm/glm.hpp>

#include "aura/aura.h"

/**
 * @file AuraMath.h
 * @brief Engine math shared across the renderer backends.
 *
 * The companion to AuraCore.h, and deliberately beside it in Core rather than
 * under Utils: what lives here is not incidental convenience code but part of
 * what the backends are built on. AuraCore.h owns the graphics *data* every
 * backend passes around (@c gfx::Vertex3D, @c gfx::TransformUBO); this owns the
 * operations on it that more than one backend needs and none of them should
 * own. Both contribute to @c aura3d::gfx for that reason.
 *
 * Header-only and free-function only. Nothing here holds state or touches a
 * device, which is what lets the Vulkan, Metal and software paths all call it
 * without any of them reaching into another's headers.
 */

namespace aura3d {
namespace gfx {

/**
 * @brief The matrix that carries a normal through @p model without shearing it.
 *
 * Mathematically identical to `transpose(inverse(mat3(model)))` but built from
 * the cofactor form directly: three cross products and one dot, instead of
 * glm's general inverse followed by a full transpose copy. Non-invertible input
 * (a zero-scaled object) yields the identity rather than infinities, which keeps
 * a degenerate transform from poisoning whatever uniform block it is written into.
 *
 * Backend-neutral on purpose: the Vulkan and Metal backends both compute this on
 * the CPU and hand the result to their vertex stage (push constants and
 * setVertexBytes respectively), while the GLSL of the OpenGL path does the
 * equivalent in-shader. Two byte-identical copies of this function once existed
 * in two Vulkan translation units -- the kind of duplication that stays correct
 * only until someone fixes one of them -- so it lives here instead, where every
 * backend reaches it without reaching into another backend's headers.
 */
[[nodiscard]] inline glm::mat3 normalMatrixOf(const glm::mat4& model) noexcept
{
    const glm::vec3 c0(model[0]);
    const glm::vec3 c1(model[1]);
    const glm::vec3 c2(model[2]);

    //! Columns of the cofactor matrix == columns of transpose(inverse(A)) * det.
    const glm::vec3 cof0 = glm::cross(c1, c2);
    const glm::vec3 cof1 = glm::cross(c2, c0);
    const glm::vec3 cof2 = glm::cross(c0, c1);

    const f32 determinant = glm::dot(c0, cof0);
    if (std::abs(determinant) < 1e-8f)
        return glm::mat3(1.0f);

    const f32 invDeterminant = 1.0f / determinant;
    return glm::mat3(cof0 * invDeterminant, cof1 * invDeterminant, cof2 * invDeterminant);
}

} // namespace gfx
} // namespace aura3d

#endif // MATH_UTILS_H
