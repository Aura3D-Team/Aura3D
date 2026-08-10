#ifndef GLINDEXBUFFERMANAGER_H
#define GLINDEXBUFFERMANAGER_H

#pragma once

#include <glad/glad.h>
#include <unordered_map>

#include "aura/Renderer/RenderHandles.h"

namespace aura3d {
namespace gl {

struct GlIndexBufferData {
    GLuint ebo = 0;
    u32    indexCount = 0;
    GLenum type = GL_UNSIGNED_SHORT;
};

class GlIndexBufferManager {
public:
    GlIndexBufferManager();
    ~GlIndexBufferManager();

    IndexBufferHandle createIndexBuffer(std::vector<u16>&& indices);
    IndexBufferHandle createIndexBuffer(std::vector<u32>&& indices);

    void bind(IndexBufferHandle handle);

    /**
     * @brief Forgets which element buffer is bound, forcing the next bind()
     *        to issue its glBindBuffer.
     *
     * Must be called whenever the vertex array changes. GL_ELEMENT_ARRAY_BUFFER
     * is per-VAO state, so binding a new VAO replaces the index binding without
     * this manager seeing it, the cache would otherwise skip a bind that the
     * new VAO genuinely needs and the draw would read the wrong indices.
     */
    void invalidateBinding() noexcept { _boundEbo = 0; }

    GlIndexBufferData* get(IndexBufferHandle handle);
    void cleanup();

private:
    std::unordered_map<IndexBufferHandle, GlIndexBufferData> _buffers;
    IndexBufferHandle _nextHandle = 1;

    //! Element buffer known to be bound in the *current* VAO; 0 means unknown.
    //! Only trustworthy between VAO switches -- see invalidateBinding().
    GLuint _boundEbo = 0;
};

} // namespace gl
} // namespace aura3d

#endif // GLINDEXBUFFERMANAGER_H
