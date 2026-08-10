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

    /**
     * @brief Makes @p handle's vertex array current, skipping the call when it
     *        already is.
     *
     * @return true if a *different* VAO was bound. Callers must treat that as
     *         invalidating their element-buffer state: in OpenGL the
     *         GL_ELEMENT_ARRAY_BUFFER binding is stored *inside* the VAO, so
     *         switching VAO silently swaps the index buffer too, and an index
     *         binding cached across the switch would be a lie.
     */
    bool bind(VertexBufferHandle handle);
    void unbind();

    /**
     * @brief Forgets which vertex array is current.
     *
     * For callers that bind a VAO this manager does not own -- the 2D overlay
     * keeps its own -- so the next bind() re-issues rather than assuming its
     * last VAO survived.
     */
    void invalidateBinding() noexcept { _boundVao = 0; }

    GlVertexBufferData* get(VertexBufferHandle handle);
    void cleanup();

private:
    std::unordered_map<VertexBufferHandle, GlVertexBufferData> _buffers;
    VertexBufferHandle _nextHandle = 1;

    //! VAO name currently bound in the context, as last set by this manager;
    //! 0 means "none". Lets bind() drop a redundant glBindVertexArray, which
    //! on the per-object draw path is otherwise issued once per mesh per frame.
    u32 _boundVao = 0;
};

} // namespace gl
} // namespace aura3d

#endif // GLVERTEXBUFFERMANAGER_H
