#ifndef GLTEXTUREMANAGER_H
#define GLTEXTUREMANAGER_H

#pragma once

#include <glad/glad.h>
#include <array>
#include <string>
#include <unordered_map>

#include "aura/Renderer/RenderHandles.h"

namespace aura3d {
namespace gl {

struct GlTextureData {
    GLuint texture = 0;
    u32 width = 0;
    u32 height = 0;
};

class GlTextureManager {
public:
    GlTextureManager();
    ~GlTextureManager();

    TextureHandle createSolidColorTexture(u8 r, u8 g, u8 b, u8 a = 255);

    /**
     * @brief Uploads tightly packed RGBA8 pixels via glTexImage2D.
     *
     * @param rgba   width * height * 4 bytes, RGBA order.
     * @param smooth Linear filtering when true, nearest when false. Nearest
     *               keeps 1x1 and checkerboard patterns crisp.
     */
    TextureHandle createTextureFromPixels(const u8* rgba, u32 width, u32 height, bool smooth = true);

    /**
     * @brief Allocates an uninitialised RGBA8 texture meant to be refreshed in
     *        place by updateRegion().
     *
     * Clamp-to-edge wrapping is used rather than the repeat the static path
     * picks: an atlas is addressed by sub-rectangle, and repeat would wrap a
     * filtered edge texel round to the opposite side of the sheet.
     *
     * @param width  Texture width in pixels.
     * @param height Texture height in pixels.
     */
    TextureHandle createDynamicTexture(u32 width, u32 height);

    /**
     * @brief Uploads @p rgba into the given sub-rectangle via glTexSubImage2D.
     *
     * Rejects (rather than clamps) a rectangle that would reach outside the
     * texture, since a partial upload would silently corrupt the atlas.
     */
    void updateRegion(TextureHandle handle, u32 x, u32 y, u32 width, u32 height, const u8* rgba);

    /**
     * @brief Binds @p handle to texture @p unit, skipping the pair of GL calls
     *        when that texture is already bound there.
     *
     * The scene draws bind a texture per object, and a scene overwhelmingly
     * shares a handful of materials between many objects, so most of those
     * binds ask for the texture that is already current.
     */
    void bind(TextureHandle handle, GLuint unit = 0);

    GlTextureData* get(TextureHandle handle);
    void cleanup();

private:
    //! Texture units this manager tracks bindings for. The renderer samples
    //! from unit 0 only; the spare slots cost nothing and keep the cache
    //! correct if a second sampler is ever added.
    static constexpr GLuint kTrackedUnits = 8;

    std::unordered_map<TextureHandle, GlTextureData> _textures;
    TextureHandle _nextHandle = 1;

    //! GL texture name bound to each tracked unit; 0 means none/unknown.
    std::array<GLuint, kTrackedUnits> _boundToUnit{};
    //! Last unit made current with glActiveTexture, so a run of binds to the
    //! same unit issues that call once rather than per bind.
    GLuint _activeUnit = 0;

    /**
     * @brief Drops every cached binding.
     *
     * Called from the paths that bind a texture behind bind()'s back (creation
     * and updateRegion(), which must make their target current to upload into
     * it) and from cleanup(), where the names become invalid outright.
     */
    void invalidateBindings() noexcept;
};

} // namespace gl
} // namespace aura3d

#endif // GLTEXTUREMANAGER_H