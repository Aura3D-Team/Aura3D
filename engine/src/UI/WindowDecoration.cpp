#include "aura/UI/WindowDecoration.h"

#include <algorithm>
#include <array>

#include "aura/UI/Core/Icon.h"

namespace aura3d::ui
{
namespace
{
constexpr f32 kTitleHeight = 34.0f;
constexpr f32 kButtonWidth = 46.0f;
constexpr f32 kTitleInset = 16.0f;
constexpr f32 kTitleGap = 12.0f;
constexpr f32 kIconSize = 12.0f;
constexpr f32 kBorder = 1.5f;
/// How far along an edge from a corner a press resizes diagonally.
constexpr f32 kCornerReach = 16.0f;

/// Toolkit-reserved icon ids (below icon::kFirstUserId); triangle() uses 0-3.
constexpr u32 kCloseFallingId = 4;
constexpr u32 kCloseRisingId = 5;

enum class WindowIcon
{
    Minimize,
    Maximize,
    Restore,
    Close
};

/// Geometry keeps the controls legible with the fallback font as well as TTFs.
class DecorationIcon final : public Widget
{
  public:
    explicit DecorationIcon(WindowIcon icon) : _icon(icon)
    {
        setHitTestVisible(false);
    }

  protected:
    void paint(DrawList &out) override
    {
        const ClipScope clip(out, bounds());
        const glm::vec4 color = parent()->resolvedStyle().text;
        const Rect box = Rect::fromSize(glm::floor(bounds().center() - glm::vec2{kIconSize * 0.5f}), glm::vec2{kIconSize});
        switch (_icon)
        {
        case WindowIcon::Minimize:
            out.fillRect(Rect::fromSize({box.min.x, box.center().y}, {kIconSize, 1.0f}), color);
            break;
        case WindowIcon::Maximize:
            out.strokeRect(box, color, 1.0f);
            break;
        case WindowIcon::Restore:
            out.fillRect(Rect::fromSize({box.min.x + 2.0f, box.min.y}, {kIconSize - 2.0f, 1.0f}), color);
            out.fillRect(Rect::fromSize({box.max.x - 1.0f, box.min.y}, {1.0f, kIconSize - 2.0f}), color);
            out.strokeRect(Rect::fromSize({box.min.x, box.min.y + 2.0f}, glm::vec2{kIconSize - 2.0f}), color, 1.0f);
            break;
        case WindowIcon::Close:
            if (auto *text = shaper())
            {
                /// A one-pixel offset per side keeps the diagonals as thick at any size.
                static constexpr f32 t = 1.0f / kIconSize;
                static constexpr std::array<glm::vec2, 4> falling{{{0, t}, {t, 0}, {1, 1 - t}, {1 - t, 1}}};
                static constexpr std::array<glm::vec2, 4> rising{{{0, 1 - t}, {1 - t, 0}, {1, t}, {t, 1}}};
                icon::convex(out, *text, box, falling, kCloseFallingId, color);
                icon::convex(out, *text, box, rising, kCloseRisingId, color);
            }
            break;
        }
    }

  private:
    WindowIcon _icon;
};

Button &addControl(Widget &parent, const char *name, const ColorSet &colors)
{
    auto &button = parent.add<Button>();
    button.setName(name);
    button.setAccessibleName(name);
    button.style().fill(colors).textColor({1, 1, 1, 1}).outline({0, 0, 0, 0}, 0).rounded(0).pad(0);
    return button;
}

[[nodiscard]] constexpr wma::SystemCursor cursorFor(wma::ResizeEdge edge) noexcept
{
    switch (edge)
    {
    case wma::ResizeEdge::Top:
    case wma::ResizeEdge::Bottom:
        return wma::SystemCursor::NsResize;
    case wma::ResizeEdge::Left:
    case wma::ResizeEdge::Right:
        return wma::SystemCursor::EwResize;
    case wma::ResizeEdge::TopLeft:
    case wma::ResizeEdge::BottomRight:
        return wma::SystemCursor::NwseResize;
    case wma::ResizeEdge::TopRight:
    case wma::ResizeEdge::BottomLeft:
        return wma::SystemCursor::NeswResize;
    }
    return wma::SystemCursor::Default;
}

/// Insets wider than a tiny surface must leave an empty rectangle, not an inverted one.
[[nodiscard]] Rect clampInside(const Rect &rect, const Rect &outer) noexcept
{
    const glm::vec2 min = glm::clamp(rect.min, outer.min, outer.max);
    return {min, glm::clamp(rect.max, min, outer.max)};
}
} // namespace

// -----------------------------------------------------------------------------
// IWindowDecoration
// -----------------------------------------------------------------------------

void IWindowDecoration::paint(DrawList &out)
{
    /// Only the frame: the content rectangle may show a 3D scene underneath.
    const glm::vec4 fill = resolvedStyle().surface.normal;
    const Rect frame = bounds();
    const Rect inner = clampInside(contentRect(), frame);
    for (const Rect &strip : {Rect{frame.min, {frame.max.x, inner.min.y}}, Rect{{frame.min.x, inner.max.y}, frame.max},
                              Rect{{frame.min.x, inner.min.y}, {inner.min.x, inner.max.y}},
                              Rect{{inner.max.x, inner.min.y}, {frame.max.x, inner.max.y}}})
    {
        if (!strip.empty())
            out.fillRect(strip, fill);
    }
}

// -----------------------------------------------------------------------------
// Resize handles
// -----------------------------------------------------------------------------

/// An invisible hit target along one side; the frame paints the border under it.
class DefaultWindowDecoration::ResizeHandle final : public Widget
{
  public:
    ResizeHandle(DefaultWindowDecoration &frame, wma::ResizeEdge side) : _frame(frame), _side(side)
    {
        setVisibility(Visibility::Collapsed);
    }

    bool onPointerMove(const PointerEvent &event) override
    {
        _frame._setCursor(cursorFor(_edgeAt(event.position)));
        return true;
    }

    void onPointerLeave() override
    {
        _frame._setCursor(wma::SystemCursor::Default);
    }

    bool onPointerDown(const PointerEvent &event) override
    {
        if (event.button != PointerButton::Left || !_frame._window)
            return false;
        //! Consumed even where unsupported, so a border press never becomes a title drag.
        _frame._window->beginResize(_edgeAt(event.position));
        return true;
    }

  protected:
    void paint(DrawList &) override
    {
    }

  private:
    [[nodiscard]] wma::ResizeEdge _edgeAt(glm::vec2 point) const noexcept
    {
        using enum wma::ResizeEdge;
        const Rect frame = _frame.bounds();
        const bool horizontal = _side == Top || _side == Bottom;
        const f32 along = horizontal ? point.x - frame.min.x : point.y - frame.min.y;
        const f32 length = horizontal ? frame.width() : frame.height();
        const bool start = along < kCornerReach;
        if (!start && along < length - kCornerReach)
            return _side;

        switch (_side)
        {
        case Top:
            return start ? TopLeft : TopRight;
        case Bottom:
            return start ? BottomLeft : BottomRight;
        case Left:
            return start ? TopLeft : BottomLeft;
        case Right:
            return start ? TopRight : BottomRight;
        default:
            return _side;
        }
    }

    DefaultWindowDecoration &_frame;
    wma::ResizeEdge _side;
};

// -----------------------------------------------------------------------------
// DefaultWindowDecoration
// -----------------------------------------------------------------------------

DefaultWindowDecoration::DefaultWindowDecoration()
{
    setName("Window decoration");
    setPart(Part::TitleBar);
    style().fill({0.006f, 0.006f, 0.006f, 1}).outline({0, 0, 0, 0}, 0).rounded(0).pad(0);

    _title = &add<Label>();
    _title->setFontSize(16);
    _title->style().textColor({0.82f, 0.82f, 0.82f, 1});

    /// Linear colors retain the intended dark surface on an sRGB swapchain.
    _minimize =
        &addControl(*this, "Minimize", {{0, 0, 0, 0}, {0.055f, 0.055f, 0.055f, 1}, {0.032f, 0.032f, 0.032f, 1}});
    _maximize = &addControl(*this, "Maximize", {{0, 0, 0, 0}, {0.015f, 0.19f, 0.62f, 1}, {0.01f, 0.12f, 0.38f, 1}});
    _close = &addControl(*this, "Close", {{0, 0, 0, 0}, {0.65f, 0.018f, 0.035f, 1}, {0.43f, 0.009f, 0.018f, 1}});
    _minimize->add<DecorationIcon>(WindowIcon::Minimize);
    _maximizeIcon = &_maximize->add<DecorationIcon>(WindowIcon::Maximize);
    _restoreIcon = &_maximize->add<DecorationIcon>(WindowIcon::Restore);
    _restoreIcon->setVisibility(Visibility::Collapsed);
    _close->add<DecorationIcon>(WindowIcon::Close);

    /// Added last so they are hit first: the outermost pixels resize, even over Close.
    using enum wma::ResizeEdge;
    _handles = {&add<ResizeHandle>(*this, Top), &add<ResizeHandle>(*this, Bottom), &add<ResizeHandle>(*this, Left),
                &add<ResizeHandle>(*this, Right)};

    _minimize->clicked.connect(
        [this]
        {
            if (_window)
                _window->minimize();
        });
    _maximize->clicked.connect(
        [this]
        {
            _toggleMaximized();
        });
    _close->clicked.connect(
        [this]
        {
            if (_window)
                _window->close();
        });
}

DefaultWindowDecoration::~DefaultWindowDecoration()
{
    _setCursor(wma::SystemCursor::Default);
}

void DefaultWindowDecoration::update(wma::IWindowManager &window, std::string_view title)
{
    _window = &window;
    if (_title->text.get() != title)
        _title->text = std::string(title);

    const bool maximized = window.isMaximized();
    if (_maximized != maximized)
    {
        _maximized = maximized;
        _maximizeIcon->setVisibility(maximized ? Visibility::Collapsed : Visibility::Visible);
        _restoreIcon->setVisibility(maximized ? Visibility::Visible : Visibility::Collapsed);
        _maximize->setAccessibleName(maximized ? "Restore" : "Maximize");
    }

    const wma::WindowDetails *details = window.getWindowDetails();
    const bool resizable = details && details->resizable && !maximized;
    if (_resizable != resizable)
    {
        _resizable = resizable;
        for (ResizeHandle *handle : _handles)
            handle->setVisibility(resizable ? Visibility::Visible : Visibility::Collapsed);
        if (!resizable)
            _setCursor(wma::SystemCursor::Default);
        invalidateLayout();
    }
}

Thickness DefaultWindowDecoration::contentInsets() const noexcept
{
    const f32 border = _resizable ? kBorder : 0.0f;
    return {border, kTitleHeight, border, border};
}

Rect DefaultWindowDecoration::_titleBar() const noexcept
{
    return Rect::fromSize(bounds().min, {bounds().width(), std::min(kTitleHeight, bounds().height())});
}

void DefaultWindowDecoration::_toggleMaximized()
{
    if (_window)
    {
        if (_window->isMaximized())
            _window->restore();
        else
            _window->maximize();
    }
}

void DefaultWindowDecoration::_setCursor(wma::SystemCursor cursor)
{
    const bool resizing = cursor != wma::SystemCursor::Default;
    if (!_window || (!resizing && !_cursorSet))
        return;
    _window->getMouseListener().setSystemCursor(cursor);
    _cursorSet = resizing;
}

bool DefaultWindowDecoration::onPointerDown(const PointerEvent &event)
{
    if (!_window || event.button != PointerButton::Left || !_titleBar().contains(event.position))
        return false;
    for (Button *button : {_minimize, _maximize, _close})
        if (button->bounds().contains(event.position))
            return false;
    if (event.clickCount % 2 == 0)
        _toggleMaximized();
    else
        _window->beginMove();
    return true;
}

void DefaultWindowDecoration::arrangeContent(const Rect &)
{
    const Rect frame = bounds();
    const Rect bar = _titleBar();

    /// Full-height and flush right, like native caption buttons, so a maximized
    /// window's corner still hits Close. Narrow surfaces shrink them, never reorder.
    const f32 buttonWidth = std::min(kButtonWidth, bar.width() / 3.0f);
    const f32 controlsStart = bar.max.x - 3.0f * buttonWidth;
    f32 x = controlsStart;
    for (Button *button : {_minimize, _maximize, _close})
    {
        button->arrange(Rect::fromSize({x, bar.min.y}, {buttonWidth, bar.height()}));
        x += buttonWidth;
    }

    /// Centred on the window, sliding left only when it would reach the controls.
    const f32 left = bar.min.x + kTitleInset;
    const f32 right = controlsStart - kTitleGap;
    const f32 width = std::clamp(_title->desiredSize().x, 0.0f, std::max(0.0f, right - left));
    const f32 titleX = std::max(left, std::min(bar.center().x - width * 0.5f, right - width));
    _title->arrange(Rect::fromSize({titleX, bar.min.y}, {width, bar.height()}));

    const f32 grip = std::min({kBorder, frame.width(), frame.height()});
    const std::array<Rect, 4> sides{
        Rect::fromSize(frame.min, {frame.width(), grip}),
        Rect::fromSize({frame.min.x, frame.max.y - grip}, {frame.width(), grip}),
        Rect::fromSize(frame.min, {grip, frame.height()}),
        Rect::fromSize({frame.max.x - grip, frame.min.y}, {grip, frame.height()}),
    };
    for (usize i = 0; i < sides.size(); ++i)
        _handles[i]->arrange(sides[i]);
}

void DefaultWindowDecoration::paintChildren(DrawList &out)
{
    for (const auto &child : children())
    {
        /// Keep long titles clear of controls while leaving room for focus rings.
        const ClipScope clip(out,
                             intersect(bounds(), child.get() == _title ? child->bounds() : child->bounds().grown(1)));
        child->paintTree(out);
    }
}
} // namespace aura3d::ui
