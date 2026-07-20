#ifndef RENDER_HANDLES_H
#define RENDER_HANDLES_H

#pragma once

#include <cstdint>
#include <unordered_map>
#include <vector>

#include "aura/aura.h"

namespace aura3d {

using VertexBufferHandle = u32;
using IndexBufferHandle = u32;
using TextureHandle = u32;
using MeshHandle = u32;
using MaterialHandle = u32;

/**
 * @brief Sentinel returned when a resource could not be created, and the
 *        default for "nothing bound".
 *
 * Backends index their pools 0-based but hand out `index + 1`, so a valid
 * handle is never 0 and never collides with this sentinel. UINT32_MAX is used
 * rather than 0 because 0 is a legitimate pool index.
 */
constexpr u32 INVALID_HANDLE = UINT32_MAX;

/**
 * @brief True when @p handle refers to a resource rather than the sentinel.
 */
[[nodiscard]]
constexpr bool isValidHandle(u32 handle) noexcept
{
    return handle != INVALID_HANDLE && handle != 0;
}

} // namespace aura3d

#endif // RENDER_HANDLES_H
