#include "aura/Renderer/OpenGL/GlAura/GlTextureManager/GlTextureManager.h"

#include <algorithm>
#include <cstring>
#include <limits>
#include <vector>

//! Core in GL 4.6, the same values as EXT_texture_filter_anisotropic, which GLES and WebGL headers may lack.
#ifndef GL_TEXTURE_MAX_ANISOTROPY
#define GL_TEXTURE_MAX_ANISOTROPY 0x84FE
#define GL_MAX_TEXTURE_MAX_ANISOTROPY 0x84FF
#endif

namespace aura3d
{
namespace gl
{

namespace
{

[[nodiscard]] GLint glWrap(TextureWrap wrap) noexcept
{
    switch (wrap)
    {
    case TextureWrap::Repeat:
        return GL_REPEAT;
    case TextureWrap::MirroredRepeat:
        return GL_MIRRORED_REPEAT;
    case TextureWrap::ClampToEdge:
        return GL_CLAMP_TO_EDGE;
    }
    return GL_REPEAT;
}

[[nodiscard]] GLint glMinFilter(const SamplerDesc &sampler, bool mipmapped) noexcept
{
    const bool linear = sampler.minFilter == TextureFilter::Linear;
    if (!mipmapped)
        return linear ? GL_LINEAR : GL_NEAREST;
    if (sampler.mipFilter == TextureFilter::Linear)
        return linear ? GL_LINEAR_MIPMAP_LINEAR : GL_NEAREST_MIPMAP_LINEAR;
    return linear ? GL_LINEAR_MIPMAP_NEAREST : GL_NEAREST_MIPMAP_NEAREST;
}

//! Sized internal format and upload type for @p format; the client format is always GL_RGBA.
void glFormat(TextureFormat format, GLint &internal, GLenum &type) noexcept
{
    internal = format == TextureFormat::Rgba8Srgb     ? GL_SRGB8_ALPHA8
               : format == TextureFormat::Rgba16Float ? GL_RGBA16F
                                                      : GL_RGBA8;
    type = format == TextureFormat::Rgba16Float ? GL_HALF_FLOAT : GL_UNSIGNED_BYTE;
}

} // namespace

GlTextureManager::GlTextureManager()
{
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &_maxTextureSize);
#ifdef AURA_GLES
    //! GLES and WebGL only through the extension, listed one name at a time.
    GLint extensions = 0;
    glGetIntegerv(GL_NUM_EXTENSIONS, &extensions);
    bool anisotropic = false;
    for (GLint i = 0; i < extensions && !anisotropic; ++i)
        anisotropic = std::strcmp(reinterpret_cast<const char *>(glGetStringi(GL_EXTENSIONS, static_cast<GLuint>(i))),
                                  "GL_EXT_texture_filter_anisotropic") == 0;
#else
    const bool anisotropic =
        GLAD_GL_VERSION_4_6 || GLAD_GL_ARB_texture_filter_anisotropic || GLAD_GL_EXT_texture_filter_anisotropic;
#endif
    if (anisotropic)
        glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY, &_maxAnisotropy);
}
GlTextureManager::~GlTextureManager()
{
    cleanup();
}

TextureHandle GlTextureManager::createSolidColorTexture(u8 r, u8 g, u8 b, u8 a)
{
    const u8 pixel[4] = {r, g, b, a};
    return createTextureFromPixels(pixel, 1, 1, /*smooth=*/false);
}

TextureHandle GlTextureManager::createTextureFromPixels(const u8 *rgba, u32 width, u32 height, bool smooth)
{
    if (!rgba || width == 0 || height == 0 || width > static_cast<u32>(_maxTextureSize) ||
        height > static_cast<u32>(_maxTextureSize))
    {
        INK_ERROR << "GlTextureManager: refusing to upload an empty texture";
        return {};
    }

    GlTextureData data;
    data.width = width;
    data.height = height;

    glGenTextures(1, &data.texture);
    glBindTexture(GL_TEXTURE_2D, data.texture);

    //! Rows are tightly packed; the default 4-byte unpack alignment would skew
    //! any image whose row length is not a multiple of 4.
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, static_cast<GLsizei>(width), static_cast<GLsizei>(height), 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, rgba);

    const GLint filter = smooth ? GL_LINEAR : GL_NEAREST;
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);

    restoreBinding();

    _textures.push_back(data);
    return TextureHandle{static_cast<u32>(_textures.size())};
}

TextureHandle GlTextureManager::createTexture(const TextureDesc &desc, std::span<const std::byte> pixels)
{
    const u32 width = desc.width, height = desc.height;
    if (width == 0 || height == 0 || width > static_cast<u32>(_maxTextureSize) ||
        height > static_cast<u32>(_maxTextureSize) ||
        pixels.size() != static_cast<size_t>(width) * height * bytesPerTexel(desc.format))
    {
        INK_ERROR << "GlTextureManager: createTexture pixels do not match the descriptor's size and format";
        return {};
    }

    GLint internal = GL_RGBA8;
    GLenum type = GL_UNSIGNED_BYTE;
    glFormat(desc.format, internal, type);
    const bool mipmapped = desc.mipmaps && mipLevelCount(width, height) > 1;

    GlTextureData data;
    data.width = width;
    data.height = height;
    data.updatable = !mipmapped && desc.format != TextureFormat::Rgba16Float;

    glGenTextures(1, &data.texture);
    glBindTexture(GL_TEXTURE_2D, data.texture);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, internal, static_cast<GLsizei>(width), static_cast<GLsizei>(height), 0, GL_RGBA,
                 type, pixels.data());
    if (mipmapped)
        glGenerateMipmap(GL_TEXTURE_2D);
    else
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);

    const SamplerDesc &sampler = desc.sampler;
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, glMinFilter(sampler, mipmapped));
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER,
                    sampler.magFilter == TextureFilter::Linear ? GL_LINEAR : GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, glWrap(sampler.wrapU));
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, glWrap(sampler.wrapV));
    if (_maxAnisotropy > 1.0f)
        glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY,
                        std::clamp(sampler.maxAnisotropy, 1.0f, _maxAnisotropy));

    restoreBinding();

    _textures.push_back(data);
    return TextureHandle{static_cast<u32>(_textures.size())};
}

TextureHandle GlTextureManager::createAttachment(u32 width, u32 height, TextureFormat format, TextureHandle replace)
{
    GLint internal = GL_RGBA8;
    GLenum type = GL_UNSIGNED_BYTE;
    glFormat(format, internal, type);
    return createStorage(width, height, internal, GL_RGBA, type, GL_LINEAR, replace);
}

TextureHandle GlTextureManager::createDepthAttachment(u32 width, u32 height, TextureHandle replace)
{
    //! Read back as plain depth, so no compare mode, and nearest: PCF filters it in the shader.
    return createStorage(width, height, GL_DEPTH_COMPONENT24, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, GL_NEAREST, replace);
}

TextureHandle GlTextureManager::createStorage(u32 width, u32 height, GLint internal, GLenum format, GLenum type,
                                              GLint filter, TextureHandle replace)
{
    if (width == 0 || height == 0 || width > static_cast<u32>(_maxTextureSize) ||
        height > static_cast<u32>(_maxTextureSize))
    {
        INK_ERROR << "GlTextureManager: refusing invalid render target dimensions";
        return {};
    }
    //! Respecified in place: the handle stays, and so does every framebuffer the texture is attached to.
    const bool reuse = isValidHandle(replace) && replace.value() <= _textures.size();
    GlTextureData data = reuse ? _textures[replace.value() - 1] : GlTextureData{};
    data.width = width;
    data.height = height;
    data.updatable = false;
    if (!reuse)
        glGenTextures(1, &data.texture);
    glBindTexture(GL_TEXTURE_2D, data.texture);
    glTexImage2D(GL_TEXTURE_2D, 0, internal, static_cast<GLsizei>(width), static_cast<GLsizei>(height), 0, format, type,
                 nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    restoreBinding();

    if (reuse)
    {
        _textures[replace.value() - 1] = data;
        return replace;
    }
    _textures.push_back(data);
    return TextureHandle{static_cast<u32>(_textures.size())};
}

TextureHandle GlTextureManager::createDynamicTexture(u32 width, u32 height)
{
    return createDynamic(width, height, false);
}

TextureHandle GlTextureManager::createCoverageTexture(u32 width, u32 height)
{
    return createDynamic(width, height, true);
}

TextureHandle GlTextureManager::createDynamic(u32 width, u32 height, bool coverageOnly)
{
    const size_t bytesPerTexel = coverageOnly ? 1u : 4u;
    if (width == 0 || height == 0 || width > static_cast<u32>(std::numeric_limits<GLsizei>::max()) ||
        height > static_cast<u32>(std::numeric_limits<GLsizei>::max()) || width > static_cast<u32>(_maxTextureSize) ||
        height > static_cast<u32>(_maxTextureSize) ||
        static_cast<size_t>(width) > std::numeric_limits<size_t>::max() / height / bytesPerTexel)
    {
        INK_ERROR << "GlTextureManager: refusing invalid dynamic texture dimensions";
        return {};
    }

    //! Allocate before changing GL state: a failed host allocation must not leak
    //! a texture name or invalidate the binding cache behind its back.
    const std::vector<u8> zeros(static_cast<size_t>(width) * height * bytesPerTexel, 0);
    GlTextureData data;
    data.width = width;
    data.height = height;
    data.coverageOnly = coverageOnly;

    glGenTextures(1, &data.texture);
    glBindTexture(GL_TEXTURE_2D, data.texture);

    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

    //! Both formats promise transparent initial contents, including glyph padding.
    glTexImage2D(GL_TEXTURE_2D, 0, coverageOnly ? GL_R8 : GL_RGBA, static_cast<GLsizei>(width),
                 static_cast<GLsizei>(height), 0, coverageOnly ? GL_RED : GL_RGBA, GL_UNSIGNED_BYTE, zeros.data());

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    restoreBinding();

    _textures.push_back(data);
    return TextureHandle{static_cast<u32>(_textures.size())};
}

void GlTextureManager::updateRegion(TextureHandle handle, u32 x, u32 y, u32 width, u32 height, const u8 *rgba)
{
    updatePixels(handle, x, y, width, height, rgba, false);
}

void GlTextureManager::updateCoverageRegion(TextureHandle handle, u32 x, u32 y, u32 width, u32 height,
                                            const u8 *coverage)
{
    updatePixels(handle, x, y, width, height, coverage, true);
}

void GlTextureManager::updatePixels(TextureHandle handle, u32 x, u32 y, u32 width, u32 height, const u8 *pixels,
                                    bool coverageOnly)
{
    if (!pixels || width == 0 || height == 0)
        return;

    const GlTextureData *found = get(handle);
    if (!found)
    {
        INK_ERROR << "GlTextureManager: updateRegion on an unknown texture";
        return;
    }

    const GlTextureData &data = *found;
    if (data.coverageOnly != coverageOnly || !data.updatable)
    {
        INK_ERROR << "GlTextureManager: update format does not match texture format, or the texture is mipmapped";
        return;
    }
    if (!regionFits(x, y, width, height, data.width, data.height))
    {
        INK_ERROR << "GlTextureManager: updateRegion rectangle exceeds the texture bounds";
        return;
    }

    glBindTexture(GL_TEXTURE_2D, data.texture);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexSubImage2D(GL_TEXTURE_2D, 0, static_cast<GLint>(x), static_cast<GLint>(y), static_cast<GLsizei>(width),
                    static_cast<GLsizei>(height), coverageOnly ? GL_RED : GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    restoreBinding();
}

void GlTextureManager::restoreBinding() noexcept
{
    glBindTexture(GL_TEXTURE_2D, _activeUnit < kTrackedUnits ? _boundToUnit[_activeUnit] : 0);
}

void GlTextureManager::invalidateBindings() noexcept
{
    _boundToUnit.fill(0);
    //! Out of range: the next bind() issues glActiveTexture.
    _activeUnit = kTrackedUnits;
}

const GlTextureData *GlTextureManager::bind(TextureHandle handle, GLuint unit)
{
    const GlTextureData *data = get(handle);
    if (!data)
        return nullptr;

    const GLuint texture = data->texture;

    //! Beyond the tracked range there is nothing to compare against, so bind
    //! unconditionally rather than guess.
    if (unit >= kTrackedUnits)
    {
        glActiveTexture(GL_TEXTURE0 + unit);
        glBindTexture(GL_TEXTURE_2D, texture);
        _activeUnit = unit;
        return data;
    }

    if (_boundToUnit[unit] == texture)
        return data;

    if (_activeUnit != unit)
    {
        glActiveTexture(GL_TEXTURE0 + unit);
        _activeUnit = unit;
    }

    glBindTexture(GL_TEXTURE_2D, texture);
    _boundToUnit[unit] = texture;
    return data;
}

GlTextureData *GlTextureManager::get(TextureHandle handle)
{
    const usize index = usize{handle.value()} - 1; // 0 wraps out of range
    return index < _textures.size() ? &_textures[index] : nullptr;
}

void GlTextureManager::cleanup()
{
    for (auto &data : _textures)
    {
        glDeleteTextures(1, &data.texture);
    }
    _textures.clear();
    //! The names just went away; nothing cached about them is meaningful.
    invalidateBindings();
}

} // namespace gl
} // namespace aura3d
