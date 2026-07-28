#include "aura/Core/TextOverlay/TextOverlay.h"

#include <algorithm>
#include <cstdio>
#include <utility>

namespace aura3d {

namespace {

//! Four vertices and six indices per glyph; reserving up front keeps the
//! steady-state path free of reallocation.
constexpr size_t kVerticesPerGlyph = 4;
constexpr size_t kIndicesPerGlyph = 6;

} // namespace

TextOverlay::TextOverlay(IRenderer* renderer, const TextOverlayDesc& desc)
    : _renderer(renderer),
      _color(desc.color)
{
    FontAtlasDesc atlasDesc{};
    atlasDesc.width = desc.atlasSize;
    atlasDesc.height = desc.atlasSize;
    atlasDesc.pixelHeight = desc.pixelHeight;

    if (!desc.fontPath.empty())
    {
        auto loaded = FontAtlas::fromFile(desc.fontPath, atlasDesc);
        if (loaded)
        {
            _atlas = std::move(*loaded);
            _usingTrueType = true;
        }
        else
        {
            /*
             * A missing font is a degraded experience, not a fatal one: fall
             * back to the embedded bitmap font so the overlay still draws on
             * platforms where no asset was staged (WASM, Android).
             */
            INK_WARN << loaded.error() << "; falling back to the embedded bitmap font";
        }
    }

    if (!_atlas)
        _atlas = FontAtlas::builtinBitmap(atlasDesc);

    /*
     * Allocate the atlas texture once, here. Every glyph rasterized later is
     * patched into it with updateTextureRegion(), so the renderer never has to
     * create another texture for the overlay's whole life.
     */
    _atlasTexture = _renderer->createDynamicTexture(_atlas->width(), _atlas->height());
    if (!isValidHandle(_atlasTexture))
        INK_ERROR << "TextOverlay: could not allocate the glyph atlas texture";
}

void TextOverlay::drawText(std::string_view text, float x, float y, float scale)
{
    drawText(text, x, y, _color, scale);
}

void TextOverlay::drawText(std::string_view text, float x, float y,
                           const glm::vec4& color, float scale)
{
    if (text.empty() || !isValidHandle(_atlasTexture))
        return;

    buildBatch(text, x, y, color, scale);

    if (_indices.empty())
        return;

    //! Strictly after buildBatch(): that is what rasterizes the new glyphs this
    //! upload carries, and strictly before the draw that samples them.
    uploadAtlasChanges();

    _renderer->drawBatch2D(_vertices, _indices, _atlasTexture);
}

void TextOverlay::buildBatch(std::string_view text, float x, float y,
                             const glm::vec4& color, float scale)
{
    //! clear() keeps the capacity, so the reserve below is a no-op after the
    //! first few frames.
    _vertices.clear();
    _indices.clear();

    const size_t glyphBudget = text.size();
    _vertices.reserve(glyphBudget * kVerticesPerGlyph);
    _indices.reserve(glyphBudget * kIndicesPerGlyph);

    const float lineAdvance = _atlas->lineHeight() * scale;

    float penX = x;
    float penY = y;
    char32_t previous = 0;

    size_t offset = 0;
    while (offset < text.size())
    {
        const char32_t codepoint = decodeUtf8(text, offset);
        if (codepoint == 0)
            break;

        if (codepoint == U'\n')
        {
            penX = x;
            penY += lineAdvance;
            previous = 0; //! No kerning pair straddles a line break.
            continue;
        }

        const GlyphInfo* glyph = _atlas->glyph(codepoint);
        if (!glyph)
            continue;

        //! Kerning tightens specific pairs (e.g. "AV"); zero for most.
        if (previous != 0)
            penX += _atlas->kerning(previous, codepoint) * scale;

        //! Whitespace carries an advance but no cell, so it emits no quad.
        if (glyph->size.x > 0.0f && glyph->size.y > 0.0f)
        {
            const float left = penX + glyph->bearing.x * scale;
            const float top = penY + glyph->bearing.y * scale;
            const float right = left + glyph->size.x * scale;
            const float bottom = top + glyph->size.y * scale;

            const auto base = static_cast<u32>(_vertices.size());

            //! Corners wound top-left, top-right, bottom-right, bottom-left, so
            //! the UV rectangle maps straight onto the quad with no flip.
            _vertices.push_back({{left,  top},    {glyph->uvMin.x, glyph->uvMin.y}, color});
            _vertices.push_back({{right, top},    {glyph->uvMax.x, glyph->uvMin.y}, color});
            _vertices.push_back({{right, bottom}, {glyph->uvMax.x, glyph->uvMax.y}, color});
            _vertices.push_back({{left,  bottom}, {glyph->uvMin.x, glyph->uvMax.y}, color});

            //! Two triangles sharing the quad's diagonal.
            _indices.push_back(base + 0);
            _indices.push_back(base + 1);
            _indices.push_back(base + 2);
            _indices.push_back(base + 2);
            _indices.push_back(base + 3);
            _indices.push_back(base + 0);
        }

        penX += glyph->advance * scale;
        previous = codepoint;
    }
}

void TextOverlay::uploadAtlasChanges()
{
    /*
     * One sub-image copy covers every glyph rasterized since the last upload,
     * because FontAtlas unions their cells into a single dirty rectangle. In
     * the steady state nothing is pending and this costs a branch.
     */
    const auto pending = _atlas->takeDirtyUpload();
    if (!pending)
        return;

    _renderer->updateTextureRegion(_atlasTexture,
                                   pending->region.x, pending->region.y,
                                   pending->region.width, pending->region.height,
                                   pending->rgba.data());
}

glm::vec2 TextOverlay::measureText(std::string_view text, float scale)
{
    if (text.empty())
        return {0.0f, 0.0f};

    const float lineAdvance = _atlas->lineHeight() * scale;

    float widest = 0.0f;
    float lineWidth = 0.0f;
    float height = lineAdvance;
    char32_t previous = 0;

    size_t offset = 0;
    while (offset < text.size())
    {
        const char32_t codepoint = decodeUtf8(text, offset);
        if (codepoint == 0)
            break;

        if (codepoint == U'\n')
        {
            widest = std::max(widest, lineWidth);
            lineWidth = 0.0f;
            height += lineAdvance;
            previous = 0;
            continue;
        }

        const GlyphInfo* glyph = _atlas->glyph(codepoint);
        if (!glyph)
            continue;

        if (previous != 0)
            lineWidth += _atlas->kerning(previous, codepoint) * scale;

        lineWidth += glyph->advance * scale;
        previous = codepoint;
    }

    return {std::max(widest, lineWidth), height};
}

float TextOverlay::lineHeight(float scale) const noexcept
{
    return _atlas->lineHeight() * scale;
}

void TextOverlay::drawFPS(float x, float y, float scale)
{
    wma::IWindowManager* windowManager = _renderer->getWindowManager();
    const f64 fps = windowManager ? windowManager->getWindowFlags()->fps : 0.0;

    char buffer[32];
    //! +0.5 rounds to nearest rather than truncating, so 59.7 reads as 60.
    std::snprintf(buffer, sizeof(buffer), "FPS: %d", static_cast<int>(fps + 0.5));
    drawText(buffer, x, y, scale);
}

} // namespace aura3d
