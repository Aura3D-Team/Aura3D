#ifndef RENDER_HANDLES_H
#define RENDER_HANDLES_H

#pragma once

#include <unordered_map>
#include <vector>

#include "aura/aura.h"

namespace aura3d {

using VertexBufferHandle = u32;
using IndexBufferHandle  = u32;
using TextureHandle      = u32;

constexpr u32 INVALID_HANDLE = 0;

} // namespace aura3d

#endif // RENDER_HANDLES_H
