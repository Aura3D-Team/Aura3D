#include "aura/UI/Core/Icon.h"

#include <algorithm>
#include <array>
#include <cmath>

#include "aura/Core/AuraFont/FontAtlas.h"
#include "aura/UI/Core/DrawList.h"
#include "aura/UI/Text/TextEngine.h"

namespace aura3d::ui::icon {

namespace {

//! Cells are square and small; the shape is drawn at the size the widget asks
//! for, so a mask larger than this is a mask nobody can see the detail of.
constexpr u32 kMaxCell = 32;

/**
 * @brief The glyph page icons rasterize into, materializing it if need be.
 *
 * Page 0 exists as soon as anything has been shaped or measured. An icon drawn
 * before the first glyph -- a disclosure arrow on a header with no label yet --
 * would otherwise find no atlas at all, so asking for the default line height
 * places the page the same way the backend does.
 */
[[nodiscard]] FontAtlas* iconPage(ITextShaper& shaper)
{
    if (FontAtlas* page = shaper.page(0))
        return page;

    (void)shaper.lineHeight(TextStyle{});
    return shaper.page(0);
}

} // namespace

void convex(DrawList& out, ITextShaper& shaper, const Rect& bounds,
            std::span<const glm::vec2> unitPolygon, u32 id, const glm::vec4& color)
{
    if (color.a <= 0.0f || bounds.empty() || unitPolygon.size() < 3)
        return;

    FontAtlas* page = iconPage(shaper);
    if (!page)
        return;

    //! Square, and sized to the shape as drawn: a cell rasterized at the pixel
    //! size it is sampled at needs no minification, which is what keeps the
    //! edge exactly as smooth as the coverage says it is.
    const f32 extent = std::min(bounds.width(), bounds.height());
    const u32 cell = std::clamp(static_cast<u32>(std::lround(extent)), 3u, kMaxCell);

    std::array<glm::vec2, 16> scaled{};
    const auto count = std::min(unitPolygon.size(), scaled.size());

    for (usize i = 0; i < count; ++i)
        scaled[i] = unitPolygon[i] * static_cast<f32>(cell);

    //! The id folds in the cell size: the same shape at two sizes is two
    //! rasterizations, and sharing one cell between them would scale it.
    const FontAtlas::UvRect* mask =
        page->convexMask(id * (kMaxCell + 1) + cell, cell, {scaled.data(), count});

    if (!mask)
        return;

    //! Centred at its rasterized size rather than stretched to `bounds`, so a
    //! square shape stays square in a rectangle that is not.
    const glm::vec2 size{static_cast<f32>(cell), static_cast<f32>(cell)};
    const Rect quad = Rect::fromSize(bounds.center() - size * 0.5f, size);

    out.drawMask(quad, 0, mask->min, mask->max, color);
}

void triangle(DrawList& out, ITextShaper& shaper, const Rect& bounds, Direction direction,
              const glm::vec4& color)
{
    //! Inscribed in the unit cell, apex on the pointing side.
    static constexpr std::array<std::array<glm::vec2, 3>, 4> kShapes = {{
        {{{0.0f, 0.0f}, {1.0f, 0.5f}, {0.0f, 1.0f}}}, // Right
        {{{0.0f, 0.0f}, {1.0f, 0.0f}, {0.5f, 1.0f}}}, // Down
        {{{1.0f, 0.0f}, {0.0f, 0.5f}, {1.0f, 1.0f}}}, // Left
        {{{0.5f, 0.0f}, {1.0f, 1.0f}, {0.0f, 1.0f}}}, // Up
    }};

    const auto index = static_cast<usize>(direction);
    convex(out, shaper, bounds, kShapes[index], static_cast<u32>(index), color);
}

} // namespace aura3d::ui::icon
