#ifndef AURA_MATH_H
#define AURA_MATH_H

#pragma once

#include <cmath>

#include <glm/glm.hpp>

#include "aura/aura.h"

/**
 * @file AuraMath.h
 * @brief Math operations shared by every renderer backend.
 *
 * Header-only, free-function only, no state or device access -- so Vulkan,
 * Metal and the software path can all call it directly instead of reaching
 * into one another's headers. Companion to AuraCore.h, which owns the graphics
 * data (@c gfx::Vertex3D, @c gfx::TransformUBO) this operates on.
 */

namespace aura3d {
namespace gfx {

/**
 * @brief The matrix that carries a normal through @p model without shearing it.
 *
 * Equivalent to `transpose(inverse(mat3(model)))`, computed from the cofactor
 * form directly (three cross products, one dot) instead of glm's general
 * inverse + transpose. A non-invertible @p model (zero scale) yields the
 * identity rather than infinities.
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

#endif // AURA_MATH_H
