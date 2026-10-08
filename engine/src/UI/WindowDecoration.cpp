#include "aura/UI/WindowDecoration.h"

#include <algorithm>
#include <array>

#include "aura/UI/Core/Icon.h"
#include "aura/UI/UIRoot.h"

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
/// Edges grab this far in, past the painted border: a surface without an invisible
/// outer margin otherwise leaves a target too thin to hit with a mouse.
constexpr f32 kResizeReach = 5.0f;
/// How far along an edge from a corner the hit test reports a diagonal resize.
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
        const Rect box =
            Rect::fromSize(glm::floor(bounds().center() - glm::vec2{kIconSize * 0.5f}), glm::vec2{kIconSize});
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

/// Whether a visible, hit-testable descendant of @p parent -- a button -- is at @p point.
[[nodiscard]] bool hitsChild(const Widget &parent, glm::vec2 point)
{
    for (const auto &child : parent.children())
    {
        if (child->visibility() == Visibility::Visible && (child->hitTest(point) || hitsChild(*child, point)))
            return true;
    }
    return false;
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

bool IWindowDecoration::overlayTakesPress(glm::vec2 point) const
{
    const UIRoot *ui = root();
    if (!ui)
        return false;
    const OverlayLayer &overlay = ui->overlay();
    const Widget *hit = overlay.widgetAt(point);
    return (hit && hit != &overlay) || overlay.hasLightDismissible();
}

wma::WindowHit IWindowDecoration::windowHit(glm::vec2 point) const
{
    if (contentRect().contains(point) || overlayTakesPress(point) || hitsChild(*this, point))
        return wma::WindowHit::Client;
    return wma::WindowHit::Caption;
}

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
// DefaultWindowDecoration
// -----------------------------------------------------------------------------

DefaultWindowDecoration::DefaultWindowDecoration()
{
    setName("Window decoration");
    setPart(Part::TitleBar);
    style().fill({0.006f, 0.006f, 0.006f, 1}).outline({0, 0, 0, 0}, 0).rounded(0).pad(0);

    _title = &add<Label>();
    //! The title is part of the draggable bar, not a click target of its own.
    _title->setHitTestVisible(false);
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

wma::WindowHit DefaultWindowDecoration::windowHit(glm::vec2 point) const
{
    using enum wma::WindowHit;
    const Rect frame = bounds();
    if (_resizable && frame.contains(point) && !overlayTakesPress(point))
    {
        //! The outermost pixels resize, even over Close, so every corner works.
        const bool top = point.y < frame.min.y + kResizeReach;
        const bool bottom = point.y >= frame.max.y - kResizeReach;
        const bool left = point.x < frame.min.x + kResizeReach;
        const bool right = point.x >= frame.max.x - kResizeReach;
        const bool nearTop = point.y < frame.min.y + kCornerReach;
        const bool nearBottom = point.y >= frame.max.y - kCornerReach;
        const bool nearLeft = point.x < frame.min.x + kCornerReach;
        const bool nearRight = point.x >= frame.max.x - kCornerReach;
        if ((top && nearLeft) || (left && nearTop))
            return TopLeft;
        if ((top && nearRight) || (right && nearTop))
            return TopRight;
        if ((bottom && nearLeft) || (left && nearBottom))
            return BottomLeft;
        if ((bottom && nearRight) || (right && nearBottom))
            return BottomRight;
        if (top)
            return Top;
        if (bottom)
            return Bottom;
        if (left)
            return Left;
        if (right)
            return Right;
    }
    return IWindowDecoration::windowHit(point);
}

void DefaultWindowDecoration::arrangeContent(const Rect &)
{
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
