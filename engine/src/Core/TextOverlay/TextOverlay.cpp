#include "aura/Core/TextOverlay/TextOverlay.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <utility>

namespace aura3d
{
namespace
{
constexpr std::array<u32, 6> kGlyphIndices{0, 1, 2, 2, 3, 0};

} // namespace

TextOverlay::TextOverlay(IRenderer *renderer, const TextOverlayDesc &desc) : _renderer(renderer), _color(desc.color)
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

    _atlasTexture = _renderer->createCoverageTexture(_atlas->width(), _atlas->height());
    if (!isValidHandle(_atlasTexture))
        INK_ERROR << "TextOverlay: could not allocate the glyph atlas texture";
}

void TextOverlay::addText(std::string_view text, float x, float y, float scale)
{
    addText(text, x, y, _color, scale);
}

void TextOverlay::addText(std::string_view text, float x, float y, const glm::vec4 &color, float scale)
{
    if (!text.empty() && isValidHandle(_atlasTexture))
        appendText(text, x, y, color, scale);
}

void TextOverlay::draw()
{
    if (!_batch.empty())
    {
        uploadAtlasChanges();
        _renderer->drawBatch(_batch, _atlasTexture);
    }
    _batch.clear();
}

void TextOverlay::appendText(std::string_view text, float x, float y, const glm::vec4 &color, float scale)
{
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

        const GlyphInfo *glyph = _atlas->glyph(codepoint);
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

            const std::array<gfx::BatchVertex, 4> quad{{{{left, top, 0}, {glyph->uvMin.x, glyph->uvMin.y}, color},
                                                        {{right, top, 0}, {glyph->uvMax.x, glyph->uvMin.y}, color},
                                                        {{right, bottom, 0}, {glyph->uvMax.x, glyph->uvMax.y}, color},
                                                        {{left, bottom, 0}, {glyph->uvMin.x, glyph->uvMax.y}, color}}};
            _batch.append(quad, kGlyphIndices);
        }

        penX += glyph->advance * scale;
        previous = codepoint;
    }
}

void TextOverlay::uploadAtlasChanges()
{
    const auto pending = _atlas->takeUpload(_atlasRevision);
    if (!pending)
        return;

    _renderer->updateCoverageTextureRegion(_atlasTexture, pending->region.x, pending->region.y, pending->region.width,
                                           pending->region.height, pending->coverage.data());
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

        const GlyphInfo *glyph = _atlas->glyph(codepoint);
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

void TextOverlay::addFPS(float x, float y, float scale)
{
    wma::IWindowManager *windowManager = _renderer->getWindowManager();
    if (!windowManager)
        return;

    const f64 currentFps = windowManager->getWindowFlags()->fps;

    // Only run snprintf when the core timer updates the smoothed FPS metric
    if (currentFps != _fps)
    {
        _fps = currentFps;
        std::snprintf(_cachedFpsString, sizeof(_cachedFpsString), "FPS: %.0f", currentFps);
    }

    addText(_cachedFpsString, x, y, scale);
}

} // namespace aura3d
