#include "aura/UI/Core/DrawList.h"

#include <algorithm>

#include "aura/UI/Text/TextEngine.h"

namespace aura3d::ui
{

void DrawList::begin(const Rect &surface)
{
    _commands.clear();
    _clips.assign(1, surface);
    _effects.clear();
}

bool DrawList::_visible(const Rect &bounds) const noexcept
{
    return !bounds.empty() && !intersect(bounds, _clips.back()).empty();
}

void DrawList::fillRect(const Rect &bounds, const glm::vec4 &color, Corners radius)
{
    if (color.a <= 0.0f || !_visible(bounds))
        return;

    _commands.push_back(DrawCommand{
        .type = DrawCommandType::Rect, .bounds = bounds, .clip = _clips.back(), .color = color, .radius = radius});
}

void DrawList::strokeRect(const Rect &bounds, const glm::vec4 &color, f32 width, Corners radius)
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

void DrawList::drawRect(const Rect &bounds, const glm::vec4 &fill, const glm::vec4 &border, f32 borderWidth,
                        Corners radius)
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

void DrawList::drawText(const ShapedText &text, glm::vec2 origin, const glm::vec4 &color)
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

void DrawList::drawImage(const Rect &bounds, TextureHandle texture, const glm::vec4 &tint, Corners radius)
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

void DrawList::drawMask(const Rect &bounds, u32 page, glm::vec2 uvMin, glm::vec2 uvMax, const glm::vec4 &color)
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

void DrawList::pushClip(const Rect &bounds)
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

void DrawList::pushEffect(f32 opacity, glm::vec2 offset)
{
    _effects.push_back(Effect{.opacity = std::clamp(opacity, 0.0f, 1.0f),
                              .offset = offset,
                              .firstCommand = _commands.size(),
                              .clipDepth = _clips.size()});
    //! Recorded in the scope's own space, so culling and clips inside need to know nothing about the effect;
    //! popEffect() moves what was recorded. The clip in force is shifted into that space, and shifted back with the
    //! rest.
    _clips.push_back(_clips.back().translated(-offset));
}

void DrawList::popEffect() noexcept
{
    if (_effects.empty())
        return;

    const Effect effect = _effects.back();
    _effects.pop_back();
    _clips.erase(_clips.begin() + static_cast<std::ptrdiff_t>(effect.clipDepth), _clips.end());

    if (effect.opacity == 1.0f && effect.offset == glm::vec2{0.0f})
        return;

    for (usize i = effect.firstCommand; i < _commands.size(); ++i)
    {
        DrawCommand &command = _commands[i];
        command.bounds = command.bounds.translated(effect.offset);
        command.clip = command.clip.translated(effect.offset);
        if (command.type == DrawCommandType::Text)
            command.origin += effect.offset;
        command.color.a *= effect.opacity;
        command.borderColor.a *= effect.opacity;
    }
}

EffectScope::EffectScope(DrawList &list, f32 opacity, glm::vec2 offset) : _list(list)
{
    _list.pushEffect(opacity, offset);
}

EffectScope::~EffectScope()
{
    _list.popEffect();
}

} // namespace aura3d::ui
