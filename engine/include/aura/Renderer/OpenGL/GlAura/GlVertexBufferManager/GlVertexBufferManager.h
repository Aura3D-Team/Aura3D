#ifndef GLVERTEXBUFFERMANAGER_H
#define GLVERTEXBUFFERMANAGER_H

#pragma once

#include <unordered_map>
#include <vector>

#include "aura/Core/AuraCore.h"
#include "aura/Renderer/RenderHandles.h"

namespace aura3d {
namespace gl {

struct GlVertexBufferData {
    u32 vao = 0;
    u32 vbo = 0;
    u32 vertexCount = 0;
};

class GlVertexBufferManager {
public:
    GlVertexBufferManager();
    ~GlVertexBufferManager();

    VertexBufferHandle createVertexBuffer(std::vector<gfx::Vertex3D>&& vertices);

    void bind(VertexBufferHandle handle);
    void unbind();

    GlVertexBufferData* get(VertexBufferHandle handle);
    void cleanup();

private:
    std::unordered_map<VertexBufferHandle, GlVertexBufferData> _buffers;
    VertexBufferHandle _nextHandle = 1;
};

} // namespace gl
} // namespace aura3d

#endif // GLVERTEXBUFFERMANAGER_H
