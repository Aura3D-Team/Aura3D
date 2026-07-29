#ifndef AURA_TEXTOVERLAY_H
#define AURA_TEXTOVERLAY_H

#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <glm/glm.hpp>

#include "aura/Core/AuraFont/FontAtlas.h"
#include "aura/Renderer/IRenderer.h"

namespace aura3d {

/**
 * @brief Construction parameters for a @ref TextOverlay.
 *
 * At namespace scope rather than nested in TextOverlay so it can be used as a
 * defaulted constructor parameter; see @ref FontAtlasDesc for the same reason.
 */
struct TextOverlayDesc {
    /**
     * Path to a .ttf/.otf file. Leave empty -- or name a file that cannot be
     * loaded -- and the overlay falls back to the engine's embedded bitmap font,
     * which needs no asset on disk and therefore always works.
     */
    std::string fontPath;

    float pixelHeight = 32.0f;   //! Rasterization size of the glyph cache.
    u32 atlasSize = 2048;        //! Edge length of the (square) glyph atlas.
    glm::vec4 color = {1.0f, 1.0f, 1.0f, 1.0f}; //! Default text colour.
};

/**
 * @brief Draws text over the current frame through the renderer's dedicated
 * unlit 2D pipeline.
 *
 * Built only on the public IRenderer API, so it behaves identically on every
 * backend (Vulkan, OpenGL and the CPU/software renderer).
 *
 * @par How a string reaches the screen
 * Glyphs are cached in a @ref FontAtlas: one large GPU texture, allocated once,
 * into which each character is rasterized the first time it is actually drawn.
 * A new character costs one sub-image upload of its own few hundred bytes --
 * never a new texture, mesh or material. That distinction matters on the Vulkan
 * backend in particular, where every texture permanently consumes descriptor-pool
 * slots that are never freed, so creating one per frame exhausts the pool within
 * minutes.
 *
 * Drawing then walks the string once, appends a quad per glyph into two vectors
 * that are reused across frames, and hands the whole thing to
 * IRenderer::drawBatch2D() as a **single draw call** -- a 500-character string
 * costs one call, not 500.
 *
 * @par Coordinates and colour
 * Positions are window pixels with (0,0) at the top-left corner; the renderer's
 * 2D pipeline supplies the orthographic projection itself, so no camera is
 * involved and the overlay is unaffected by wherever the scene's camera happens
 * to be pointing. Shading is unlit, so text keeps exactly the colour it was
 * given no matter what the scene's light is doing.
 *
 * Text is UTF-8. Codepoints the font does not carry render as '?'; '\n' starts
 * a new line.
 *
 * @note Draw calls must be issued between beginRenderPass() and endRenderPass(),
 *       after the scene's own draws.
 * @note Not thread-safe: the glyph cache and the batch buffers are mutable state.
 */
class TextOverlay {
public:
    /**
     * @brief Builds the overlay and its glyph atlas.
     *
     * Loading never throws and never leaves the overlay unusable: a missing or
     * malformed font logs a warning and falls back to the embedded bitmap font.
     *
     * @param renderer Renderer to draw through; must outlive this object.
     * @param desc Font, size and default colour; see @ref TextOverlayDesc.
     */
    explicit TextOverlay(IRenderer* renderer, const TextOverlayDesc& desc = TextOverlayDesc{});

    /**
     * @brief Draws @p text with its top-left corner at window pixel (x, y).
     *
     * @param text UTF-8 text; '\n' starts a new line.
     * @param x Left edge, in window pixels.
     * @param y Top edge of the first line's box, in window pixels.
     * @param scale Multiplier on the atlas' rasterization size. Values far from
     *        1.0 magnify or minify an already-rasterized bitmap, so prefer
     *        raising TextOverlayDesc::pixelHeight for permanently larger text.
     */
    void drawText(std::string_view text, float x, float y, float scale = 1.0f);

    /// As above, overriding the default colour for this call only.
    void drawText(std::string_view text, float x, float y, const glm::vec4& color, float scale = 1.0f);

    /**
     * @brief Convenience: formats and draws "FPS: <n>" from the renderer's live
     * frame timing (wma::WindowFlags::fps) at window pixel (x, y).
     */
    void drawFPS(float x, float y, float scale = 1.0f);

    /**
     * @brief Pixel dimensions @p text would occupy if drawn at @p scale.
     *
     * Width is that of the widest line, height a whole number of line boxes.
     * Rasterizes any glyph not yet cached, so it is not @c const.
     */
    [[nodiscard]] glm::vec2 measureText(std::string_view text, float scale = 1.0f);

    /// Baseline-to-baseline distance at @p scale, in pixels.
    [[nodiscard]] float lineHeight(float scale = 1.0f) const noexcept;

    /// The colour used when a draw call does not name one.
    [[nodiscard]] const glm::vec4& color() const noexcept { return _color; }
    void setColor(const glm::vec4& color) noexcept { _color = color; }

    /// False when the requested font could not be loaded and the embedded
    /// bitmap font is standing in for it.
    [[nodiscard]] bool usingTrueType() const noexcept { return _usingTrueType; }

private:
    /**
     * @brief Fills the reusable vertex/index buffers with @p text's quads.
     *
     * Rasterizes any glyph not yet in the atlas as a side effect, which is why
     * the atlas upload has to follow this rather than precede it.
     */
    void buildBatch(std::string_view text, float x, float y, const glm::vec4& color, float scale);

    /// Pushes the atlas' pending dirty rectangle to the GPU, if any.
    void uploadAtlasChanges();

    IRenderer* _renderer;
    std::unique_ptr<FontAtlas> _atlas;
    TextureHandle _atlasTexture = INVALID_HANDLE;
    glm::vec4 _color{1.0f};
    bool _usingTrueType = false;

    //! Retained across calls so a steady-state overlay never reallocates.
    std::vector<gfx::Vertex2D> _vertices;
    std::vector<u32> _indices;
};

} // namespace aura3d

#endif // AURA_TEXTOVERLAY_H
