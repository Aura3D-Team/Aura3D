#include "aura/UI/Widgets/Basic.h"

#include <algorithm>

#include "aura/UI/UIRoot.h"

namespace aura3d::ui {

Label::Label(std::string value) : text(std::move(value))
{
    _part = Part::Label;
    setHitTestVisible(false);

    //! Text drives the widget's size, so a change is a layout change, not a
    //! repaint. Wired here rather than in a setter so an assignment through
    //! the property behaves the same way.
    text.changed().connect([this](const std::string&) {
        _textDirty = true;
        invalidateLayout();
    });
}

void Label::setFontSize(f32 pixels)
{
    if (_textStyle.pixelSize == pixels)
        return;

    _textStyle.pixelSize = std::max(0.0f, pixels);
    invalidateLayout();
}

void Label::setWrap(TextWrap wrap)
{
    if (_textStyle.wrap == wrap)
        return;

    _textStyle.wrap = wrap;
    invalidateLayout();
}

void Label::setAlign(Align align)
{
    if (_textStyle.align == align)
        return;

    _textStyle.align = align;
    invalidateLayout();
}

void Label::setLineSpacing(f32 multiple)
{
    if (_textStyle.lineSpacing == multiple)
        return;

    _textStyle.lineSpacing = std::max(0.5f, multiple);
    invalidateLayout();
}

void Label::setTextStyle(const TextStyle& style)
{
    if (_textStyle == style)
        return;

    _textStyle = style;
    invalidateLayout();
}

void Label::shapeText(f32 maxWidth)
{
    ITextShaper* shaper = this->shaper();
    if (!shaper)
        return;

    TextStyle style = _textStyle;
    if (style.pixelSize <= 0.0f)
        style.pixelSize = theme().metrics.fontSize;

    //! Only a wrapping label depends on the width it is given; for every other
    //! one the width is not part of the cache key, so a resize re-shapes
    //! nothing.
    const bool wraps = style.wrap != TextWrap::None;
    const f32 key = wraps ? maxWidth : -1.0f;

    if (!_textDirty && style == _shapedStyle && key == _shapedWidth)
        return;

    shaper->shape(text.get(), style, wraps ? maxWidth : kUnbounded, _shaped);

    _shapedStyle = style;
    _shapedWidth = key;
    _textDirty = false;
}

glm::vec2 Label::measureContent(const Constraints& available)
{
    shapeText(available.max.x);
    return _shaped.size;
}

glm::vec2 Label::textOrigin(const Rect& content) const noexcept
{
    const Align align = resolvedStyle().align;

    const Alignment horizontal = align == Align::Center  ? Alignment::Center
                                 : align == Align::Right ? Alignment::End
                                                         : Alignment::Start;

    return {content.min.x + alignOffset(horizontal, content.width(), _shaped.size.x),
            content.min.y + alignOffset(Alignment::Center, content.height(), _shaped.size.y)};
}

void Label::paint(DrawList& out)
{
    Widget::paint(out);

    if (_shaped.empty())
        return;

    const f32 alpha = effectivelyEnabled() ? 1.0f : theme().metrics.disabledAlpha;

    out.drawText(_shaped, textOrigin(contentRect()),
                 withAlpha(resolvedStyle().text, alpha));
}

void Label::accessibility(AccessibilityInfo& out) const
{
    Widget::accessibility(out);

    out.role = Role::Label;
    if (out.name.empty())
        out.name = text.get();
}

Image::Image(TextureHandle texture, glm::vec2 nativeSize)
    : _texture(texture), _native(glm::max(nativeSize, glm::vec2{0.0f}))
{
    setHitTestVisible(false);
}

void Image::setTexture(TextureHandle texture, glm::vec2 nativeSize)
{
    _texture = texture;
    _native = glm::max(nativeSize, glm::vec2{0.0f});
    invalidateLayout();
}

void Image::setFit(Fit fit)
{
    if (_fit == fit)
        return;

    _fit = fit;
    invalidatePaint();
}

void Image::setTint(const glm::vec4& tint)
{
    _tint = tint;
    invalidatePaint();
}

void Image::setRadius(Corners radius)
{
    _radius = radius;
    invalidatePaint();
}

glm::vec2 Image::measureContent(const Constraints& available)
{
    if (_native.x <= 0.0f || _native.y <= 0.0f)
        return {0.0f, 0.0f};

    if (_fit == Fit::None)
        return _native;

    //! Aspect-preserving fits report the native size scaled down to whatever
    //! bound is real, so an image in a narrow column shrinks instead of
    //! overflowing it.
    const f32 scale = std::min({1.0f, isUnbounded(available.max.x) ? 1.0f
                                                                  : available.max.x / _native.x,
                                isUnbounded(available.max.y) ? 1.0f
                                                             : available.max.y / _native.y});

    return _native * scale;
}

Rect Image::_destination(const Rect& content) const noexcept
{
    if (_native.x <= 0.0f || _native.y <= 0.0f || _fit == Fit::Stretch)
        return content;

    const glm::vec2 box = content.size();

    const f32 sx = box.x / _native.x;
    const f32 sy = box.y / _native.y;

    const f32 scale = _fit == Fit::None    ? 1.0f
                      : _fit == Fit::Cover ? std::max(sx, sy)
                                           : std::min(sx, sy);

    const glm::vec2 size = _native * scale;

    return Rect::fromSize(content.min + (box - size) * 0.5f, size);
}

void Image::paint(DrawList& out)
{
    Widget::paint(out);

    if (!isValidHandle(_texture))
        return;

    const f32 alpha = effectivelyEnabled() ? 1.0f : theme().metrics.disabledAlpha;
    const Rect destination = _destination(contentRect());

    //! Cover overflows its box by design; the clip is what turns that overflow
    //! into a crop rather than into a neighbour's problem.
    if (_fit == Fit::Cover)
    {
        const ClipScope clipped(out, contentRect());
        out.drawImage(destination, _texture, withAlpha(_tint, alpha), _radius);
        return;
    }

    out.drawImage(destination, _texture, withAlpha(_tint, alpha), _radius);
}

void Image::accessibility(AccessibilityInfo& out) const
{
    Widget::accessibility(out);
    out.role = Role::Image;
}

Separator::Separator(Axis axis) : _axis(axis)
{
    _part = Part::Separator;
    setHitTestVisible(false);

    //! Thin on its own axis, stretched on the other: one line whichever way it
    //! is turned.
    if (_axis == Axis::Horizontal)
        layout().height = Length::px(1.0f);
    else
        layout().width = Length::px(1.0f);
}

glm::vec2 Separator::measureContent(const Constraints&)
{
    return _axis == Axis::Horizontal ? glm::vec2{0.0f, 1.0f} : glm::vec2{1.0f, 0.0f};
}

void Separator::accessibility(AccessibilityInfo& out) const
{
    Widget::accessibility(out);
    out.role = Role::Separator;
}

} // namespace aura3d::ui
