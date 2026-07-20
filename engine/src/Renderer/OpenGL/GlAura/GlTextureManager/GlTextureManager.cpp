#include "aura/Renderer/OpenGL/GlAura/GlTextureManager/GlTextureManager.h"

namespace aura3d {
namespace gl {

GlTextureManager::GlTextureManager() = default;
GlTextureManager::~GlTextureManager() { cleanup(); }

TextureHandle GlTextureManager::createSolidColorTexture(u8 r, u8 g, u8 b, u8 a)
{
    const u8 pixel[4] = { r, g, b, a };
    return createTextureFromPixels(pixel, 1, 1, /*smooth=*/false);
}

TextureHandle GlTextureManager::createTextureFromPixels(const u8* rgba, u32 width, u32 height, bool smooth)
{
    if (!rgba || width == 0 || height == 0)
    {
        INK_ERROR << "GlTextureManager: refusing to upload an empty texture";
        return INVALID_HANDLE;
    }

    auto handle = _nextHandle++;
    GlTextureData data;
    data.width = width;
    data.height = height;

    glGenTextures(1, &data.texture);
    glBindTexture(GL_TEXTURE_2D, data.texture);

    //! Rows are tightly packed; the default 4-byte unpack alignment would skew
    //! any image whose row length is not a multiple of 4.
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA,
                 static_cast<GLsizei>(width), static_cast<GLsizei>(height),
                 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);

    const GLint filter = smooth ? GL_LINEAR : GL_NEAREST;
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);

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