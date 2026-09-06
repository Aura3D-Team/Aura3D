#include "aura/UI/Core/DrawList.h"

#include "aura/UI/Text/TextEngine.h"

namespace aura3d::ui {

void DrawList::begin(const Rect& surface)
{
    _commands.clear();
    _clips.assign(1, surface);
}

bool DrawList::_visible(const Rect& bounds) const noexcept
{
    return !bounds.empty() && !intersect(bounds, _clips.back()).empty();
}

void DrawList::fillRect(const Rect& bounds, const glm::vec4& color, Corners radius)
{
    if (color.a <= 0.0f || !_visible(bounds))
        return;

    _commands.push_back(DrawCommand{.type = DrawCommandType::Rect,
                                    .bounds = bounds,
                                    .clip = _clips.back(),
                                    .color = color,
                                    .radius = radius});
}

void DrawList::strokeRect(const Rect& bounds, const glm::vec4& color, f32 width, Corners radius)
{
    if (width <= 0.0f || color.a <= 0.0f || !_visible(bounds))
        return;

    _commands.push_back(DrawCommand{.type = DrawCommandType::Rect,
                                    .bounds = bounds,
                                    .clip = _clips.back(),
                                    .color = glm::vec4{0.0f},
                                    .borderColor = color,
                                    .borderWidth = width,
                                    .radius = radius});
}

void DrawList::drawRect(const Rect& bounds, const glm::vec4& fill, const glm::vec4& border,
                        f32 borderWidth, Corners radius)
{
    const bool hasBorder = borderWidth > 0.0f && border.a > 0.0f;

    if ((fill.a <= 0.0f && !hasBorder) || !_visible(bounds))
        return;

    _commands.push_back(DrawCommand{.type = DrawCommandType::Rect,
                                    .bounds = bounds,
                                    .clip = _clips.back(),
                                    .color = fill,
                                    .borderColor = hasBorder ? border : glm::vec4{0.0f},
                                    .borderWidth = hasBorder ? borderWidth : 0.0f,
                                    .radius = radius});
}

void DrawList::drawText(const ShapedText& text, glm::vec2 origin, const glm::vec4& color)
{
    if (color.a <= 0.0f || text.empty())
        return;

    const Rect bounds = Rect::fromSize(origin, text.size);
    if (!_visible(bounds))
        return;

    _commands.push_back(DrawCommand{.type = DrawCommandType::Text,
                                    .bounds = bounds,
                                    .clip = _clips.back(),
                                    .color = color,
                                    .text = &text,
                                    .origin = origin});
}

void DrawList::drawImage(const Rect& bounds, TextureHandle texture, const glm::vec4& tint,
                         Corners radius)
{
    if (tint.a <= 0.0f || !_visible(bounds))
        return;

    _commands.push_back(DrawCommand{.type = DrawCommandType::Image,
                                    .bounds = bounds,
                                    .clip = _clips.back(),
                                    .color = tint,
                                    .radius = radius,
                                    .texture = texture});
}

void DrawList::drawMask(const Rect& bounds, u32 page, glm::vec2 uvMin, glm::vec2 uvMax,
                        const glm::vec4& color)
{
    if (color.a <= 0.0f || !_visible(bounds))
        return;

    _commands.push_back(DrawCommand{.type = DrawCommandType::Mask,
                                    .bounds = bounds,
                                    .clip = _clips.back(),
                                    .color = color,
                                    .uvMin = uvMin,
                                    .uvMax = uvMax,
                                    .page = page});
}

void DrawList::pushClip(const Rect& bounds)
{
    _clips.push_back(intersect(bounds, _clips.back()));
}

void DrawList::popClip() noexcept
{
    //! The outermost entry is the surface itself, placed by begin(); an
    //! unbalanced pop must not take it, or every later primitive is clipped
    //! against a stale rectangle.
    if (_clips.size() > 1)
        _clips.pop_back();
}

} // namespace aura3d::ui
