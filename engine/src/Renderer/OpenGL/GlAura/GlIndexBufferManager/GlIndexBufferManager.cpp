#include "aura/Renderer/OpenGL/GlAura/GlIndexBufferManager/GlIndexBufferManager.h"

namespace aura3d {
namespace gl {

GlIndexBufferManager::GlIndexBufferManager() = default;
GlIndexBufferManager::~GlIndexBufferManager() { cleanup(); }

IndexBufferHandle GlIndexBufferManager::createIndexBuffer(std::vector<u16>&& indices)
{
    auto handle = _nextHandle++;
    GlIndexBufferData data;
    data.indexCount = static_cast<u32>(indices.size());
    data.type = GL_UNSIGNED_SHORT;

    glGenBuffers(1, &data.ebo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, data.ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(u16), indices.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    //! Creation leaves the element target unbound; keep the cache honest about
    //! that rather than letting it claim a buffer that is no longer current.
    _boundEbo = 0;

    _buffers[handle] = data;
    return handle;
}

IndexBufferHandle GlIndexBufferManager::createIndexBuffer(std::vector<u32>&& indices)
{
    auto handle = _nextHandle++;
    GlIndexBufferData data;
    data.indexCount = static_cast<u32>(indices.size());
    data.type = GL_UNSIGNED_INT;

    glGenBuffers(1, &data.ebo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, data.ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(u32), indices.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    //! Creation leaves the element target unbound; keep the cache honest about
    //! that rather than letting it claim a buffer that is no longer current.
    _boundEbo = 0;

    _buffers[handle] = data;
    return handle;
}

void GlIndexBufferManager::bind(IndexBufferHandle handle)
{
    auto it = _buffers.find(handle);
    if (it == _buffers.end())
        return;

    const GLuint ebo = it->second.ebo;
    if (ebo == _boundEbo)
        return;

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
    _boundEbo = ebo;
}

GlIndexBufferData* GlIndexBufferManager::get(IndexBufferHandle handle)
{
    auto it = _buffers.find(handle);
    return (it != _buffers.end()) ? &it->second : nullptr;
}

void GlIndexBufferManager::cleanup()
{
    for (auto& [handle, data] : _buffers) {
        glDeleteBuffers(1, &data.ebo);
    }
    _buffers.clear();
    _nextHandle = IndexBufferHandle{1};
    _boundEbo = 0;
}

} // namespace gl
} // namespace aura3d
