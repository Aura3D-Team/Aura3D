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
    GlIndexBufferData* get(IndexBufferHandle handle);
    void cleanup();

private:
    std::unordered_map<IndexBufferHandle, GlIndexBufferData> _buffers;
    IndexBufferHandle _nextHandle = 1;
};

} // namespace gl
} // namespace aura3d

#endif // GLINDEXBUFFERMANAGER_H
