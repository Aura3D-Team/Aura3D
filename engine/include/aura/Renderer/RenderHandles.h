#ifndef RENDER_HANDLES_H
#define RENDER_HANDLES_H

#pragma once

#include "aura/Core/Handle.h"
#include "aura/aura.h"

namespace aura3d {

//! Tag types distinguishing one resource family from another at compile
//! time. Never defined -- see Handle<Tag>'s class comment.
struct VertexBufferTag;
struct IndexBufferTag;
struct TextureTag;
struct MeshTag;
struct MaterialTag;

using VertexBufferHandle = Handle<VertexBufferTag>;
using IndexBufferHandle = Handle<IndexBufferTag>;
using TextureHandle = Handle<TextureTag>;
using MeshHandle = Handle<MeshTag>;
using MaterialHandle = Handle<MaterialTag>;

} // namespace aura3d

#endif // RENDER_HANDLES_H
