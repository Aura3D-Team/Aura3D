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
    unsigned int vao = 0;
    unsigned int vbo = 0;
    u32   vertexCount = 0;
    bool  is2d = true;
};

class GlVertexBufferManager {
public:
    GlVertexBufferManager();
    ~GlVertexBufferManager();

    VertexBufferHandle createVertexBuffer(std::vector<gfx::Vertex2D>&& vertices);
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
