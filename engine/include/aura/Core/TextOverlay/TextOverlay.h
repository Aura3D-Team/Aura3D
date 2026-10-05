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

namespace aura3d
{

/**
 * @brief Construction parameters for a @ref TextOverlay.
 *
 * At namespace scope rather than nested in TextOverlay so it can be used as a
 * defaulted constructor parameter; see @ref FontAtlasDesc for the same reason.
 */
struct TextOverlayDesc
{
    /**
     * Path to a .ttf/.otf file. Leave empty -- or name a file that cannot be
     * loaded -- and the overlay falls back to the engine's embedded bitmap font,
     * which needs no asset on disk and therefore always works.
     */
    std::string fontPath;

    float pixelHeight = 32.0f;                  //! Rasterization size of the glyph cache.
    u32 atlasSize = 2048;                       //! Edge length of the (square) glyph atlas.
    glm::vec4 color = {1.0f, 1.0f, 1.0f, 1.0f}; //! Default text colour.
};

/**
 * @brief Draws text over the current frame through the renderer's unlit batch
 * pipeline. Behaves identically on every backend.
 *
 * Glyphs are cached in a @ref FontAtlas (one GPU texture, allocated once);
 * each character rasterizes on first use, costing a small sub-image upload
 * rather than a new texture. addText() queues; draw() submits everything
 * queued as one IRenderer::drawBatch() call.
 *
 * Positions are window pixels, (0,0) top-left; no camera is involved. Text is
 * UTF-8, unsupported codepoints render as '?', '\n' starts a new line.
 *
 * @note Call draw() between beginRenderPass() and endRenderPass(), after the
 *       scene's own draws.
 * @note Not thread-safe: the glyph cache and the batch buffers are mutable state.
 */
class TextOverlay
{
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
    explicit TextOverlay(IRenderer *renderer, const TextOverlayDesc &desc = TextOverlayDesc{});

    /**
     * @brief Queues @p text with its top-left corner at window pixel (x, y).
     *
     * @param text UTF-8 text; '\n' starts a new line.
     * @param x Left edge, in window pixels.
     * @param y Top edge of the first line's box, in window pixels.
     * @param scale Multiplier on the atlas' rasterization size. Values far from
     *        1.0 magnify or minify an already-rasterized bitmap, so prefer
     *        raising TextOverlayDesc::pixelHeight for permanently larger text.
     */
    void addText(std::string_view text, float x, float y, float scale = 1.0f);

    /// As above, overriding the default colour for this call only.
    void addText(std::string_view text, float x, float y, const glm::vec4 &color, float scale = 1.0f);

    /**
     * @brief Queues the live frame rate as @c "FPS: <n>  (<ms> ms)".
     *
     * Smoothed, deliberately: wma reports 1000/deltaTime for the frame that
     * just ended, which on an unlocked loop swings by tens of frames between
     * one frame and the next and reads as noise rather than a number. This
     * keeps an exponential moving average with a ~0.5s time constant, so the
     * figure is one you can act on. The raw frame time is printed beside it
     * because that is what stays linear when you are chasing a regression --
     * 120 to 60 fps and 8.3 to 16.7 ms are the same fact, and only the second
     * pair reads as "twice the work".
     *
     * @note Advances the average once per call, so call it once a frame. To
     *       show the same figure somewhere else, read @ref fps() rather than
     *       calling this again.
     */
    void addFPS(float x, float y, float scale = 1.0f);

    /// Draws everything queued since the last call in one batch, then empties the queue.
    void draw();

    /// The smoothed frame rate @ref addFPS last computed; zero before the
    /// first call. For showing the same number in a UI panel or a HUD.
    [[nodiscard]] float fps() const noexcept
    {
        return _fps;
    }

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
    [[nodiscard]] const glm::vec4 &color() const noexcept
    {
        return _color;
    }
    void setColor(const glm::vec4 &color) noexcept
    {
        _color = color;
    }

    /// False when the requested font could not be loaded and the embedded
    /// bitmap font is standing in for it.
    [[nodiscard]] bool usingTrueType() const noexcept
    {
        return _usingTrueType;
    }

  private:
    //! Appends @p text's quads to the queue. Rasterizes any glyph not yet in
    //! the atlas, so the atlas upload must follow this rather than precede it.
    void appendText(std::string_view text, float x, float y, const glm::vec4 &color, float scale);

    /// Pushes the atlas' pending dirty rectangle to the GPU, if any.
    void uploadAtlasChanges();

    IRenderer *_renderer;
    std::unique_ptr<FontAtlas> _atlas;
    TextureHandle _atlasTexture;
    u64 _atlasRevision = 0; //! FontAtlas::takeUpload()'s cursor for _atlasTexture.
    glm::vec4 _color{1.0f};
    bool _usingTrueType = false;

    //! Smoothed frame rate; see addFPS(), which is the only thing that
    //! advances it.
    float _fps = 0.0f;
    char _cachedFpsString[48] = "FPS: 0  (0.00 ms)";

    //! Queued text; keeps its capacity, so a steady frame does not allocate.
    gfx::Canvas _batch;
};

} // namespace aura3d

#endif // AURA_TEXTOVERLAY_H
