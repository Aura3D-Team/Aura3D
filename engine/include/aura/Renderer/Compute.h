#ifndef AURA_RENDERER_COMPUTE_H
#define AURA_RENDERER_COMPUTE_H

#pragma once

#include <cstddef>
#include <functional>
#include <span>
#include <string>

#include <glm/glm.hpp>

#include "aura/aura.h"

namespace aura3d
{

/// One indexed draw's arguments, laid out as Vulkan, OpenGL and Metal all read them from a buffer.
struct DrawIndexedIndirect
{
    u32 indexCount = 0;
    u32 instanceCount = 0;
    u32 firstIndex = 0;
    i32 vertexOffset = 0;
    u32 firstInstance = 0;
};
static_assert(sizeof(DrawIndexedIndirect) == 20);

/// One instance as IRenderer::drawMeshInstancedIndirect() reads it: the model matrix, and its normal matrix in the
/// first three columns of the second.
struct InstanceTransform
{
    glm::mat4 model{1.0f};
    glm::mat4 normal{1.0f};
};
static_assert(sizeof(InstanceTransform) == 128);

/// What one workgroup of a compute dispatch sees on the software backend.
struct ComputeInvocation
{
    std::span<const std::span<std::byte>> buffers; //! The dispatch's buffers, in binding order.
    glm::uvec3 group{0};                           //! This workgroup's id.
    std::span<const std::byte> constants;          //! The dispatch's constants.
};

/**
 * @brief A compute kernel in each backend's language. Each reads its storage buffers at bindings 0..buffers-1 and
 *        up to 128 bytes of constants:
 *
 *   - Vulkan: @c spirv, buffers at set 0 bindings 0.., constants as push constants.
 *   - OpenGL 4.3: @c glsl, buffers at SSBO bindings 0.., constants in a std140 uniform block at binding 3.
 *   - Metal: @c msl's @c mslEntry, buffers at [[buffer(0..)]], constants at [[buffer(buffers)]].
 *   - Software: @c cpu, once per workgroup, the groups spread over the job system, so like a GPU kernel it must
 *     use atomics (std::atomic_ref) where groups share a value.
 *
 * Workgroup sizes are the shaders' own; the C++ kernel loops over its group's items itself.
 */
struct ComputeDesc
{
    u32 buffers = 1;
    std::span<const std::byte> spirv;
    std::string glsl;
    std::string msl;
    std::string mslEntry = "main0";
    glm::uvec3 mslThreadsPerGroup{64, 1, 1}; //! Metal takes the workgroup size at dispatch, not from the kernel.
    std::function<void(const ComputeInvocation &)> cpu;
};

} // namespace aura3d

#endif // AURA_RENDERER_COMPUTE_H
