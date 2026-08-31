#include "aura/Core/TextOverlay/TextOverlay.h"

#include <algorithm>
#include <cstdio>
#include <utility>

namespace aura3d {
namespace {
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
            INK_WARN << loaded.error() << "; falling back to the embedded bitmap font";
        }
    }

    if (!_atlas)
        _atlas = FontAtlas::builtinBitmap(atlasDesc);

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

    uploadAtlasChanges();

    _renderer->drawBatch2D(_vertices, _indices, _atlasTexture);
}

void TextOverlay::buildBatch(std::string_view text, float x, float y,
                             const glm::vec4& color, float scale)
{
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
            previous = 0;
            continue;
        }

        const GlyphInfo* glyph = _atlas->glyph(codepoint);
        if (!glyph)
            continue;

        if (previous != 0)
            penX += _atlas->kerning(previous, codepoint) * scale;

        if (glyph->size.x > 0.0f && glyph->size.y > 0.0f)
        {
            const float left = penX + glyph->bearing.x * scale;
            const float top = penY + glyph->bearing.y * scale;
            const float right = left + glyph->size.x * scale;
            const float bottom = top + glyph->size.y * scale;

            const auto base = static_cast<u32>(_vertices.size());

            _vertices.push_back({{left,  top},    {glyph->uvMin.x, glyph->uvMin.y}, color});
            _vertices.push_back({{right, top},    {glyph->uvMax.x, glyph->uvMin.y}, color});
            _vertices.push_back({{right, bottom}, {glyph->uvMax.x, glyph->uvMax.y}, color});
            _vertices.push_back({{left,  bottom}, {glyph->uvMin.x, glyph->uvMax.y}, color});

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
    if (!windowManager) return;

    const f64 currentFps = windowManager->getWindowFlags()->fps;

    // Only run snprintf when the core timer updates the smoothed FPS metric
    if (currentFps != _fps)
    {
        _fps = currentFps;
        std::snprintf(_cachedFpsString, sizeof(_cachedFpsString), 
                      "FPS: %.0f", currentFps);
    }

    drawText(_cachedFpsString, x, y, scale);
}

} // namespace aura3d
