#include "aura/Renderer/OpenGL/GlAura/GlVertexBufferManager/GlVertexBufferManager.h"

#include <glad/glad.h>

#include "aura/aura.h"

namespace aura3d {
namespace gl {

GlVertexBufferManager::GlVertexBufferManager() = default;
GlVertexBufferManager::~GlVertexBufferManager() { cleanup(); }

static void setupVertexAttributes()
{
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(gfx::Vertex3D), (void*)offsetof(gfx::Vertex3D, pos));
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(gfx::Vertex3D), (void*)offsetof(gfx::Vertex3D, texCoord));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(gfx::Vertex3D), (void*)offsetof(gfx::Vertex3D, color));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, sizeof(gfx::Vertex3D), (void*)offsetof(gfx::Vertex3D, normal));
    glEnableVertexAttribArray(3);
}

VertexBufferHandle GlVertexBufferManager::createVertexBuffer(std::vector<gfx::Vertex3D>&& vertices)
{
    auto handle = _nextHandle++;
    GlVertexBufferData data;
    data.vertexCount = static_cast<u32>(vertices.size());

    glGenVertexArrays(1, &data.vao);
    glGenBuffers(1, &data.vbo);

    glBindVertexArray(data.vao);
    glBindBuffer(GL_ARRAY_BUFFER, data.vbo);
    glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(gfx::Vertex3D), vertices.data(), GL_STATIC_DRAW);
    setupVertexAttributes();
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    _buffers[handle] = data;
    return handle;
}

void GlVertexBufferManager::bind(VertexBufferHandle handle)
{
    auto it = _buffers.find(handle);
    if (it != _buffers.end()) {
        glBindVertexArray(it->second.vao);
    }
}

void GlVertexBufferManager::unbind()
{
    glBindVertexArray(0);
}

GlVertexBufferData* GlVertexBufferManager::get(VertexBufferHandle handle)
{
    auto it = _buffers.find(handle);
    return (it != _buffers.end()) ? &it->second : nullptr;
}

void GlVertexBufferManager::cleanup()
{
    for (auto& [handle, data] : _buffers) {
        glDeleteVertexArrays(1, &data.vao);
        glDeleteBuffers(1, &data.vbo);
    }
    _buffers.clear();
    _nextHandle = 1;
}

} // namespace gl
} // namespace aura3d
