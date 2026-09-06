#include "aura/UI/Backend/DrawListRenderer.h"

#include <algorithm>

#include "aura/Core/AuraFont/FontAtlas.h"
#include "aura/Renderer/IRenderer.h"
#include "aura/UI/Text/TextEngine.h"

namespace aura3d::ui {

namespace {

constexpr usize kVerticesPerQuad = 4;
constexpr usize kIndicesPerQuad = 6;

//! Radius past which the atlas has no mask cell. Also a sane ceiling: a bigger
//! one is a pill, which the clamp to half the shorter side already produces.
constexpr f32 kMaxRadius = static_cast<f32>(FontAtlas::kMaxCornerRadius);

} // namespace

DrawListRenderer::DrawListRenderer(IRenderer& renderer, ITextShaper& shaper)
    : _renderer(&renderer), _shaper(&shaper)
{
}

DrawListRenderer::~DrawListRenderer() = default;

// -----------------------------------------------------------------------------
// Glyph pages
// -----------------------------------------------------------------------------

void DrawListRenderer::_syncPages()
{
    /*
     * A UI that draws no text has no glyph page, and would then have no opaque
     * cell for its rectangles and no corner mask for their radii -- so a panel
     * with rounded corners would come out square for want of a font. Asking
     * for the default line height materialises the page (they are created on
     * first use), which costs one atlas on a UI that never draws a glyph and
     * keeps every other path in this file down to one case.
     */
    if (_shaper->pageCount() == 0)
        (void)_shaper->lineHeight(TextStyle{});

    const u32 pages = _shaper->pageCount();

    _pageTextures.resize(pages);
    _pageSolidUv.resize(pages, glm::vec2{0.0f});

    for (u32 i = 0; i < pages; ++i)
    {
        FontAtlas* atlas = _shaper->page(i);
        if (!atlas)
            continue;

        if (!isValidHandle(_pageTextures[i]))
        {
            _pageTextures[i] = _renderer->createDynamicTexture(atlas->width(), atlas->height());

            if (const auto solid = atlas->solidTexelUv())
                _pageSolidUv[i] = *solid;
        }

        //! Glyphs are rasterized during shaping, which happened in the measure
        //! pass, so by the time a frame is submitted the atlas is already
        //! carrying everything this frame will draw.
        if (const auto pending = atlas->takeDirtyUpload())
            _renderer->updateTextureRegion(_pageTextures[i], pending->region.x, pending->region.y,
                                           pending->region.width, pending->region.height,
                                           pending->rgba.data());
    }
}

TextureHandle DrawListRenderer::_whiteTexture()
{
    if (!isValidHandle(_white))
        _white = _renderer->createSolidColorTexture(255, 255, 255, 255);

    return _white;
}

void DrawListRenderer::_useTexture(TextureHandle texture)
{
    if (!_batches.empty() && _batches.back().texture == texture)
        return;

    if (!_batches.empty() && _batches.back().quadCount == 0)
    {
        //! Nothing was emitted into the batch that was open, so it is
        //! retargeted rather than closed: an empty draw call is still a
        //! driver round trip.
        _batches.back().texture = texture;
        return;
    }

    _batches.push_back(Batch{static_cast<u32>(_quads), 0, texture});
}

void DrawListRenderer::_usePage(u32 index)
{
    if (!_hasPages)
    {
        _useTexture(_whiteTexture());
        return;
    }

    if (index >= _pageTextures.size())
        index = 0;

    _currentPage = index;
    _useTexture(_pageTextures[index]);
}

// -----------------------------------------------------------------------------
// Emission
// -----------------------------------------------------------------------------

void DrawListRenderer::_reserveIndices(usize quads)
{
    const usize needed = quads * kIndicesPerQuad;
    if (_indices.size() >= needed)
        return;

    const usize first = _indices.size() / kIndicesPerQuad;
    _indices.resize(needed);

    for (usize quad = first; quad < quads; ++quad)
    {
        const auto base = static_cast<u32>(quad * kVerticesPerQuad);
        u32* out = _indices.data() + quad * kIndicesPerQuad;

        out[0] = base + 0;
        out[1] = base + 1;
        out[2] = base + 2;
        out[3] = base + 2;
        out[4] = base + 3;
        out[5] = base + 0;
    }
}

gfx::Vertex2D* DrawListRenderer::_quadSlot()
{
    const usize needed = (_quads + 1) * kVerticesPerQuad;

    if (needed > _vertices.size())
    {
        //! Doubling, and never shrinking: the buffer settles at the busiest
        //! frame's size and no frame after it allocates.
        _vertices.resize(std::max(needed, _vertices.size() * 2));
    }

    _reserveIndices(_quads + 1);

    gfx::Vertex2D* slot = _vertices.data() + _quads * kVerticesPerQuad;

    ++_quads;
    if (!_batches.empty())
        ++_batches.back().quadCount;

    return slot;
}

void DrawListRenderer::_quad(const Rect& bounds, const Rect& clip, glm::vec2 uvMin,
                             glm::vec2 uvMax, const glm::vec4& color)
{
    const glm::vec2 size = bounds.size();
    if (size.x <= 0.0f || size.y <= 0.0f || color.a <= 0.0f)
        return;

    const Rect visible = intersect(bounds, clip);
    if (visible.empty())
        return;

    glm::vec2 uv0 = uvMin;
    glm::vec2 uv1 = uvMax;

    /*
     * Trimming a rectangle and moving its UVs by the same fractions is exact
     * for axis-aligned geometry: the surviving texels are what a scissor
     * would have produced, with no pipeline state change to pay for.
     *
     * The untrimmed case -- everything except the quads straddling a scroll
     * view's edge -- is taken early, because the arithmetic below is the
     * identity there and four dependent divisions per quad is the single
     * biggest cost in this function.
     */
    if (visible.min != bounds.min || visible.max != bounds.max)
    {
        const glm::vec2 inverse{1.0f / size.x, 1.0f / size.y};

        uv0 = {lerp(uvMin.x, uvMax.x, (visible.min.x - bounds.min.x) * inverse.x),
               lerp(uvMin.y, uvMax.y, (visible.min.y - bounds.min.y) * inverse.y)};
        uv1 = {lerp(uvMin.x, uvMax.x, (visible.max.x - bounds.min.x) * inverse.x),
               lerp(uvMin.y, uvMax.y, (visible.max.y - bounds.min.y) * inverse.y)};
    }

    gfx::Vertex2D* out = _quadSlot();

    //! Top-left, top-right, bottom-right, bottom-left: the order the shared
    //! index pattern assumes.
    out[0] = {visible.min * _scale, {uv0.x, uv0.y}, color};
    out[1] = {glm::vec2{visible.max.x, visible.min.y} * _scale, {uv1.x, uv0.y}, color};
    out[2] = {visible.max * _scale, {uv1.x, uv1.y}, color};
    out[3] = {glm::vec2{visible.min.x, visible.max.y} * _scale, {uv0.x, uv1.y}, color};
}

void DrawListRenderer::_solid(const Rect& bounds, const Rect& clip, const glm::vec4& color)
{
    //! Without a glyph page the bound texture is the 1x1 white one, whose
    //! every texel is opaque -- so any UV does.
    const glm::vec2 uv =
        _hasPages && _currentPage < _pageSolidUv.size() ? _pageSolidUv[_currentPage]
                                                        : glm::vec2{0.0f};

    _quad(bounds, clip, uv, uv, color);
}

void DrawListRenderer::_roundedRect(const Rect& bounds, const Rect& clip, Corners radius,
                                    const glm::vec4& color)
{
    const f32 limit = std::min(bounds.width(), bounds.height()) * 0.5f;

    const auto cap = [limit](f32 value) {
        return std::floor(std::clamp(value, 0.0f, std::min(limit, kMaxRadius)));
    };

    const f32 tl = cap(radius.topLeft);
    const f32 tr = cap(radius.topRight);
    const f32 br = cap(radius.bottomRight);
    const f32 bl = cap(radius.bottomLeft);

    if (tl < 1.0f && tr < 1.0f && br < 1.0f && bl < 1.0f)
    {
        _solid(bounds, clip, color);
        return;
    }

    FontAtlas* atlas = _shaper->page(_currentPage);
    if (!atlas)
    {
        _solid(bounds, clip, color);
        return;
    }

    const f32 left = bounds.min.x;
    const f32 right = bounds.max.x;
    const f32 top = bounds.min.y;
    const f32 bottom = bounds.max.y;

    /*
     * A corner is one quad against an antialiased coverage mask, and the body
     * is three bands plus a filler beside any corner shorter than its
     * neighbour. Eleven quads at worst, seven when the radii are equal, and
     * constant in the radius either way -- which is the whole reason the
     * shape is described by a number rather than rasterized here.
     */
    const auto corner = [&](f32 x, f32 y, f32 size, bool flipX, bool flipY) {
        if (size < 1.0f)
            return;

        const FontAtlas::UvRect* mask = atlas->cornerMask(static_cast<u32>(size));
        if (!mask)
        {
            _solid(Rect::fromSize({x, y}, {size, size}), clip, color);
            return;
        }

        //! One cell serves all four corners: swapping its UV bounds on an axis
        //! mirrors it, because _quad assigns them to corners in a fixed order.
        const glm::vec2 uv0{flipX ? mask->max.x : mask->min.x, flipY ? mask->max.y : mask->min.y};
        const glm::vec2 uv1{flipX ? mask->min.x : mask->max.x, flipY ? mask->min.y : mask->max.y};

        _quad(Rect::fromSize({x, y}, {size, size}), clip, uv0, uv1, color);
    };

    corner(left, top, tl, false, false);
    corner(right - tr, top, tr, true, false);
    corner(right - br, bottom - br, br, true, true);
    corner(left, bottom - bl, bl, false, true);

    const f32 topBand = std::max(tl, tr);
    const f32 bottomBand = std::max(bl, br);

    _solid({{left, top + topBand}, {right, bottom - bottomBand}}, clip, color);
    _solid({{left + tl, top}, {right - tr, top + topBand}}, clip, color);
    _solid({{left + bl, bottom - bottomBand}, {right - br, bottom}}, clip, color);

    //! A corner shorter than the band its side reaches leaves a notch beside
    //! it; these four fill it. All empty when the radii match.
    _solid({{left, top + tl}, {left + tl, top + topBand}}, clip, color);
    _solid({{right - tr, top + tr}, {right, top + topBand}}, clip, color);
    _solid({{left, bottom - bottomBand}, {left + bl, bottom - bl}}, clip, color);
    _solid({{right - br, bottom - bottomBand}, {right, bottom - br}}, clip, color);
}

void DrawListRenderer::_roundedBorder(const Rect& bounds, const Rect& clip, Corners radius,
                                      f32 width, const glm::vec4& color)
{
    const f32 limit = std::min({bounds.width() * .5f, bounds.height() * .5f, kMaxRadius});
    const auto cap = [limit](f32 r) { return std::floor(std::clamp(r, 0.f, limit)); };
    const f32 tl = cap(radius.topLeft), tr = cap(radius.topRight);
    const f32 bl = cap(radius.bottomLeft), br = cap(radius.bottomRight);
    const f32 l = bounds.min.x, r = bounds.max.x, t = bounds.min.y, b = bounds.max.y;
    const f32 ctl = std::max(tl, width), ctr = std::max(tr, width);
    const f32 cbl = std::max(bl, width), cbr = std::max(br, width);
    _solid({{l + ctl, t}, {r - ctr, t + width}}, clip, color);
    _solid({{l + cbl, b - width}, {r - cbr, b}}, clip, color);
    _solid({{l, t + ctl}, {l + width, b - cbl}}, clip, color);
    _solid({{r - width, t + ctr}, {r, b - cbr}}, clip, color);

    FontAtlas* atlas = _shaper->page(_currentPage);
    const auto corner = [&](f32 x, f32 y, f32 radius, f32 side, bool flipX, bool flipY) {
        const auto rectangle = [&](f32 x0, f32 y0, f32 x1, f32 y1) {
            return Rect{{x + (flipX ? side - x1 : x0), y + (flipY ? side - y1 : y0)},
                        {x + (flipX ? side - x0 : x1), y + (flipY ? side - y0 : y1)}};
        };
        if (radius < 1.f || !atlas) { _solid(Rect::fromSize({x, y}, {side, side}), clip, color); return; }
        const u32 pixels = std::clamp(u32(std::ceil(radius * _scale)), 1u, FontAtlas::kMaxCornerRadius);
        const auto* mask = atlas->cornerRingMask(pixels, width * pixels / radius);
        if (mask) {
            const glm::vec2 uv0{flipX ? mask->max.x : mask->min.x, flipY ? mask->max.y : mask->min.y};
            const glm::vec2 uv1{flipX ? mask->min.x : mask->max.x, flipY ? mask->min.y : mask->max.y};
            _quad(rectangle(0, 0, radius, radius), clip, uv0, uv1, color);
        }
        // A border wider than this corner fills the rest of its corner block.
        _solid(rectangle(radius, 0, side, side), clip, color);
        _solid(rectangle(0, radius, radius, side), clip, color);
    };
    corner(l, t, tl, ctl, false, false);
    corner(r - ctr, t, tr, ctr, true, false);
    corner(l, b - cbl, bl, cbl, false, true);
    corner(r - cbr, b - cbr, br, cbr, true, true);
}

void DrawListRenderer::_text(const DrawCommand& command)
{
    const ShapedText& text = *command.text;

    if (!_hasPages || _shaper->page(text.page) == nullptr)
        return;

    _usePage(text.page);

    for (const ShapedGlyph& glyph : text.glyphs)
    {
        const Rect quad = glyph.bounds.translated(command.origin);

        //! Whole glyphs outside the clip are dropped here rather than inside
        //! _quad, so a long string scrolled out of view costs one rejection
        //! per glyph and no arithmetic.
        if (intersect(quad, command.clip).empty())
            continue;

        _quad(quad, command.clip, glyph.uvMin, glyph.uvMax, command.color);
    }
}

// -----------------------------------------------------------------------------
// Frame
// -----------------------------------------------------------------------------

void DrawListRenderer::build(const DrawList& list, f32 scale)
{
    _syncPages();

    _quads = 0;
    _batches.clear();
    _scale = scale;

    _hasPages = !_pageTextures.empty();
    _currentPage = 0;
    _usePage(0);

    for (const DrawCommand& command : list.commands())
    {
        switch (command.type)
        {
        case DrawCommandType::Rect:
        {
            const bool outlined = command.borderWidth > 0.0f && command.borderColor.a > 0.0f;

            if (outlined && command.color.a >= .999f)
                _roundedRect(command.bounds, command.clip, command.radius, command.borderColor);
            else if (outlined)
                _roundedBorder(command.bounds, command.clip, command.radius,
                    std::min(command.borderWidth, std::min(command.bounds.width(), command.bounds.height()) * .5f),
                    command.borderColor);

            if (command.color.a <= 0.0f)
                break;

            if (!outlined)
            {
                _roundedRect(command.bounds, command.clip, command.radius, command.color);
                break;
            }

            const f32 inset = command.borderWidth;
            const Corners inner{std::max(0.0f, command.radius.topLeft - inset),
                                std::max(0.0f, command.radius.topRight - inset),
                                std::max(0.0f, command.radius.bottomRight - inset),
                                std::max(0.0f, command.radius.bottomLeft - inset)};

            _roundedRect(command.bounds.inset(inset), command.clip, inner, command.color);
            break;
        }

        case DrawCommandType::Text:
            if (command.text)
                _text(command);
            break;

        case DrawCommandType::Mask:
        {
            //! Same page as the text around it, so an icon never costs a batch
            //! of its own -- which is the whole reason icons live in the glyph
            //! atlas rather than in a texture apiece.
            if (!_hasPages || _shaper->page(command.page) == nullptr)
                break;

            _usePage(command.page);
            _quad(command.bounds, command.clip, command.uvMin, command.uvMax, command.color);
            break;
        }

        case DrawCommandType::Image:
        {
            //! An image is its own texture, so it is its own batch. Rectangles
            //! after it go back to the glyph page, which is what stops one
            //! icon from splitting the rest of the UI into two draw calls.
            _useTexture(command.texture);
            _quad(command.bounds, command.clip, {0.0f, 0.0f}, {1.0f, 1.0f}, command.color);
            _usePage(_currentPage);
            break;
        }
        }
    }

    //! An empty trailing batch is left behind by the _usePage() after the last
    //! image, and by a list that ended on a clipped-away command.
    if (!_batches.empty() && _batches.back().quadCount == 0)
        _batches.pop_back();
    //! Rounded corners allocate their mask while this frame's quads are being
    //! emitted, so the upload has to happen after the walk, not before it.
    _syncPages();
}

void DrawListRenderer::submit()
{
    if (_batches.empty() || _quads == 0)
        return;

    for (const Batch& batch : _batches)
    {
        if (batch.quadCount == 0)
            continue;

        const std::span vertices{_vertices.data() + batch.firstQuad * kVerticesPerQuad,
                                 batch.quadCount * kVerticesPerQuad};

        //! The shared index pattern is 0-based, and every batch is a
        //! contiguous run of quads, so the same prefix of it addresses any
        //! batch's own slice of the vertices.
        const std::span indices{_indices.data(), batch.quadCount * kIndicesPerQuad};

        _renderer->drawBatch2D(vertices, indices, batch.texture);
    }
}

} // namespace aura3d::ui
