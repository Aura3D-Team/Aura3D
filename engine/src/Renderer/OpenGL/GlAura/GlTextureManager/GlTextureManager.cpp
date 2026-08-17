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
        return {};
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
    //! Uploading made this texture current on the active unit and then unbound
    //! it, both behind bind()'s back; drop the cache rather than let it lie.
    invalidateBindings();

    _textures[handle] = data;
    return handle;
}

TextureHandle GlTextureManager::createDynamicTexture(u32 width, u32 height)
{
    if (width == 0 || height == 0)
    {
        INK_ERROR << "GlTextureManager: refusing to allocate a zero-sized dynamic texture";
        return {};
    }

    auto handle = _nextHandle++;
    GlTextureData data;
    data.width = width;
    data.height = height;

    glGenTextures(1, &data.texture);
    glBindTexture(GL_TEXTURE_2D, data.texture);

    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

    /*
     * A null pixel pointer allocates the storage without uploading anything.
     * The contents are undefined until updateRegion() writes them, which is
     * exactly the glyph-atlas usage: reserve the sheet once, fill cells later.
     */
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA,
                 static_cast<GLsizei>(width), static_cast<GLsizei>(height),
                 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glBindTexture(GL_TEXTURE_2D, 0);
    //! Uploading made this texture current on the active unit and then unbound
    //! it, both behind bind()'s back; drop the cache rather than let it lie.
    invalidateBindings();

    _textures[handle] = data;
    return handle;
}

void GlTextureManager::updateRegion(TextureHandle handle, u32 x, u32 y, u32 width, u32 height, const u8* rgba)
{
    if (!rgba || width == 0 || height == 0)
        return;

    auto it = _textures.find(handle);
    if (it == _textures.end())
    {
        INK_ERROR << "GlTextureManager: updateRegion on an unknown texture";
        return;
    }

    const GlTextureData& data = it->second;
    if (x + width > data.width || y + height > data.height)
    {
        INK_ERROR << "GlTextureManager: updateRegion rectangle exceeds the texture bounds";
        return;
    }

    glBindTexture(GL_TEXTURE_2D, data.texture);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexSubImage2D(GL_TEXTURE_2D, 0,
                    static_cast<GLint>(x), static_cast<GLint>(y),
                    static_cast<GLsizei>(width), static_cast<GLsizei>(height),
                    GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    glBindTexture(GL_TEXTURE_2D, 0);
    invalidateBindings();
}

void GlTextureManager::invalidateBindings() noexcept
{
    _boundToUnit.fill(0);
}

void GlTextureManager::bind(TextureHandle handle, GLuint unit)
{
    auto it = _textures.find(handle);
    if (it == _textures.end())
        return;

    const GLuint texture = it->second.texture;

    //! Beyond the tracked range there is nothing to compare against, so bind
    //! unconditionally rather than guess.
    if (unit >= kTrackedUnits)
    {
        glActiveTexture(GL_TEXTURE0 + unit);
        glBindTexture(GL_TEXTURE_2D, texture);
        _activeUnit = unit;
        return;
    }

    if (_boundToUnit[unit] == texture)
        return;

    if (_activeUnit != unit)
    {
        glActiveTexture(GL_TEXTURE0 + unit);
        _activeUnit = unit;
    }

    glBindTexture(GL_TEXTURE_2D, texture);
    _boundToUnit[unit] = texture;
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
    _nextHandle = TextureHandle{1};
    //! The names just went away; nothing cached about them is meaningful.
    invalidateBindings();
}

} // namespace gl
} // namespace aura3d