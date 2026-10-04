#ifndef AURA_UI_DRAWLISTRENDERER_H
#define AURA_UI_DRAWLISTRENDERER_H

#pragma once

#include <vector>

#include "aura/Core/AuraCore.h"
#include "aura/Renderer/RenderHandles.h"
#include "aura/UI/Core/DrawList.h"
#include "aura/Utils/AlignedVector.h"

/**
 * @file DrawListRenderer.h
 * @brief The only file in AuraUI that knows what a vertex is.
 *
 * Turns a @ref DrawList into IRenderer::drawBatch() calls, and keeps the
 * properties that made the engine's overlay path fast in the first place:
 *
 *  - **One draw call.** Solid rectangles sample the glyph atlas' opaque cell
 *    (FontAtlas::solidTexelUv()), so panels, borders and text share a texture
 *    and a batch. Font sizes packed on the same sheet also share that batch.
 *  - **No steady-state allocation.** Vertex and index storage is retained at
 *    its high-water mark across frames.
 *  - **Rounded corners in constant geometry.** A nine-slice against the
 *    atlas' antialiased corner mask: at most eleven quads whatever the radius,
 *    against the one-span-per-scanline a filled cap would cost.
 *  - **Clipping without state changes.** Quads are trimmed on the CPU, which
 *    is exact for axis-aligned geometry and costs nothing on the overwhelming
 *    majority of quads that are not cut at all.
 *  - **Nothing rebuilt that did not change.** build() is called only when the
 *    tree re-recorded; an idle UI re-submits the buffers it already has.
 */

namespace aura3d
{
class IRenderer;
} // namespace aura3d

namespace aura3d::ui
{

class ITextShaper;

/**
 * @class DrawListRenderer
 * @brief Draws a @ref DrawList through an @ref IRenderer.
 *
 * @note Uses one coverage texture per shared atlas sheet. Both the renderer and the
 *       shaper must outlive it.
 */
class DrawListRenderer
{
  public:
    DrawListRenderer(IRenderer &renderer, ITextShaper &shaper);
    ~DrawListRenderer();

    DrawListRenderer(const DrawListRenderer &) = delete;
    DrawListRenderer &operator=(const DrawListRenderer &) = delete;

    /**
     * @brief Rebuilds the vertex data for @p list.
     *
     * @param scale Device-pixel ratio. Positions are multiplied by it here and
     *        nowhere else, which is what keeps layout DPI-independent.
     */
    void build(const DrawList &list, f32 scale = 1.0f);

    /// Submits what the last build() produced. Call between beginRenderPass()
    /// and endRenderPass(), after the scene.
    void submit();

    /// @{
    /// What the last build() produced. The batch count is the draw call count,
    /// and the reason it is exposed: a change that quietly broke atlas sharing
    /// would still look correct on screen.
    [[nodiscard]] usize batchCount() const noexcept
    {
        return _batches.size();
    }
    [[nodiscard]] usize quadCount() const noexcept
    {
        return _quads;
    }
    /// @}

  private:
    /// One contiguous run of quads that share a texture.
    struct Batch
    {
        u32 firstQuad = 0;
        u32 quadCount = 0;
        TextureHandle texture{};
    };

    /// Creates the textures newly added glyph pages need, and pushes whatever
    /// the shaper rasterized since the last frame.
    void _syncPages(bool upload);

    /// Switches the batch being built to @p texture, closing the previous one.
    void _useTexture(TextureHandle texture);

    /// Switches to the glyph page @p index, so the solid cell the rectangles
    /// around it sample belongs to the texture that is bound.
    void _usePage(u32 index);

    /// A 1x1 opaque texture, created on first use.
    ///
    /// Solid rectangles normally sample the glyph atlas' opaque cell, which is
    /// what keeps them in the same batch as the text around them. A UI that
    /// draws no text has no atlas at all, and would otherwise draw nothing --
    /// so it gets this instead.
    [[nodiscard]] TextureHandle _whiteTexture();

    /// @{
    /// Emission. Every path funnels into _quad(), which is the one place
    /// clipping, scaling and vertex writing happen.
    void _quad(const Rect &bounds, const Rect &clip, glm::vec2 uvMin, glm::vec2 uvMax, const glm::vec4 &color);

    void _solid(const Rect &bounds, const Rect &clip, const glm::vec4 &color);

    //! Geometry on whole device pixels: an antialiased mask then lands texel for
    //! pixel, and a quad edge never resamples it against the atlas padding.
    [[nodiscard]] Rect _snap(const Rect &rect) const noexcept;
    //! A corner radius as whole device pixels, in logical units.
    [[nodiscard]] f32 _cornerRadius(f32 radius, const Rect &bounds) const noexcept;

    void _roundedRect(const Rect &bounds, const Rect &clip, Corners radius, const glm::vec4 &color);
    void _roundedBorder(const Rect &bounds, const Rect &clip, Corners radius, f32 width, const glm::vec4 &color);

    void _text(const DrawCommand &command);
    /// @}

    /// Four vertices for the next quad, growing the buffer if it is full.
    [[nodiscard]] gfx::BatchVertex *_quadSlot();

    /// Grows the shared index buffer to cover @p quads, if it does not already.
    void _reserveIndices(usize quads);

    IRenderer *_renderer = nullptr;
    ITextShaper *_shaper = nullptr;

    //! Size views that share atlas storage also share their texture.
    std::vector<TextureHandle> _pageTextures;

    struct AtlasTexture
    {
        const void *identity = nullptr;
        TextureHandle texture{};
        u64 revision = 0; //! FontAtlas::takeUpload()'s cursor for this texture.
    };
    std::vector<AtlasTexture> _atlasTextures;

    //! Cached solid-cell UV per page, so a rectangle costs no atlas lookup.
    std::vector<glm::vec2> _pageSolidUv;

    AlignedVector<gfx::BatchVertex> _vertices;

    /*
     * Quad i always uses vertices 4i..4i+3, so its six indices are implied by
     * its position. One 0-based pattern therefore serves every batch: a batch
     * is submitted with its own slice of the vertices and the first
     * quadCount*6 of these, which is why no index is ever written per frame.
     */
    std::vector<u32> _indices;

    std::vector<Batch> _batches;

    TextureHandle _white{};

    usize _quads = 0;
    u32 _currentPage = 0;

    //! False while no glyph page exists, so rectangles fall back to _white.
    bool _hasPages = false;
    f32 _scale = 1.0f;
};

} // namespace aura3d::ui

#endif // AURA_UI_DRAWLISTRENDERER_H
