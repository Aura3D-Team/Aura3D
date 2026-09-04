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
 * Turns a @ref DrawList into IRenderer::drawBatch2D() calls, and keeps the
 * properties that made the engine's overlay path fast in the first place:
 *
 *  - **One draw call.** Solid rectangles sample the glyph atlas' opaque cell
 *    (FontAtlas::solidTexelUv()), so panels, borders and text share a texture
 *    and a batch. A UI at one font size is a single drawBatch2D() whatever it
 *    contains.
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

namespace aura3d {
class IRenderer;
} // namespace aura3d

namespace aura3d::ui {

class ITextShaper;

/**
 * @class DrawListRenderer
 * @brief Draws a @ref DrawList through an @ref IRenderer.
 *
 * @note Owns one dynamic texture per glyph page. Both the renderer and the
 *       shaper must outlive it.
 */
class DrawListRenderer {
public:
    DrawListRenderer(IRenderer& renderer, ITextShaper& shaper);
    ~DrawListRenderer();

    DrawListRenderer(const DrawListRenderer&) = delete;
    DrawListRenderer& operator=(const DrawListRenderer&) = delete;

    /**
     * @brief Rebuilds the vertex data for @p list.
     *
     * @param scale Device-pixel ratio. Positions are multiplied by it here and
     *        nowhere else, which is what keeps layout DPI-independent.
     */
    void build(const DrawList& list, f32 scale = 1.0f);

    /// Submits what the last build() produced. Call between beginRenderPass()
    /// and endRenderPass(), after the scene.
    void submit();

    /// @{
    /// What the last build() produced. The batch count is the draw call count,
    /// and the reason it is exposed: a change that quietly broke atlas sharing
    /// would still look correct on screen.
    [[nodiscard]] usize batchCount() const noexcept { return _batches.size(); }
    [[nodiscard]] usize quadCount() const noexcept { return _quads; }
    /// @}

private:
    /// One contiguous run of quads that share a texture.
    struct Batch {
        u32 firstQuad = 0;
        u32 quadCount = 0;
        TextureHandle texture{};
    };

    /// Creates the textures newly added glyph pages need, and pushes whatever
    /// the shaper rasterized since the last frame.
    void _syncPages();

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
    void _quad(const Rect& bounds, const Rect& clip, glm::vec2 uvMin, glm::vec2 uvMax,
               const glm::vec4& color);

    void _solid(const Rect& bounds, const Rect& clip, const glm::vec4& color);

    void _roundedRect(const Rect& bounds, const Rect& clip, Corners radius,
                      const glm::vec4& color);

    void _text(const DrawCommand& command);
    /// @}

    /// Four vertices for the next quad, growing the buffer if it is full.
    [[nodiscard]] gfx::Vertex2D* _quadSlot();

    /// Grows the shared index buffer to cover @p quads, if it does not already.
    void _reserveIndices(usize quads);

    IRenderer* _renderer = nullptr;
    ITextShaper* _shaper = nullptr;

    //! One dynamic texture per glyph page, parallel to the shaper's pages.
    std::vector<TextureHandle> _pageTextures;

    //! Cached solid-cell UV per page, so a rectangle costs no atlas lookup.
    std::vector<glm::vec2> _pageSolidUv;

    AlignedVector<gfx::Vertex2D> _vertices;

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
