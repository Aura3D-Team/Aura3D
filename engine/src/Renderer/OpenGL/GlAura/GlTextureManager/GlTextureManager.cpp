#include "aura/Renderer/OpenGL/GlAura/GlTextureManager/GlTextureManager.h"

namespace aura3d {
namespace gl {

GlTextureManager::GlTextureManager() = default;
GlTextureManager::~GlTextureManager() { cleanup(); }

TextureHandle GlTextureManager::createSolidColorTexture(u8 r, u8 g, u8 b, u8 a)
{
    auto handle = _nextHandle++;
    GlTextureData data;
    data.width = 1;
    data.height = 1;

    glGenTextures(1, &data.texture);
    glBindTexture(GL_TEXTURE_2D, data.texture);
    unsigned char pixel[] = { r, g, b, a };
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glBindTexture(GL_TEXTURE_2D, 0);

    _textures[handle] = data;
    return handle;
}

void GlTextureManager::bind(TextureHandle handle, GLuint unit)
{
    auto it = _textures.find(handle);
    if (it != _textures.end()) {
        glActiveTexture(GL_TEXTURE0 + unit);
        glBindTexture(GL_TEXTURE_2D, it->second.texture);
    }
}

GlTextureData* GlTextureManager::get(TextureHandle handle)
{
    auto it = _textures.find(handle);
    return (it != _textures.end()) ? &it->second : nullptr;
}

void GlTextureManager::cleanup()
{
    for (auto& [handle, data] : _textures) {
        glDeleteTextures(1, &data.texture);
    }
    _textures.clear();
    _nextHandle = 1;
}

} // namespace gl
} // namespace aura3d