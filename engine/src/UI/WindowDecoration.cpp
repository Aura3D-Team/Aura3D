#include "aura/UI/WindowDecoration.h"

#include <algorithm>
#include <array>

#include "aura/UI/Core/Icon.h"
#include "aura/UI/UIRoot.h"

namespace aura3d::ui
{
namespace
{
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
    DecorationIcon(WindowIcon icon, const WindowChrome &chrome) : _icon(icon), _chrome(chrome)
    {
        setHitTestVisible(false);
    }

  protected:
    void paint(DrawList &out) override
    {
        const ClipScope clip(out, bounds());
        const glm::vec4 color = parent()->resolvedStyle().text;
        const f32 size = _chrome.iconSize;
        const Rect box = Rect::fromSize(glm::floor(bounds().center() - glm::vec2{size * 0.5f}), glm::vec2{size});
        switch (_icon)
        {
        case WindowIcon::Minimize:
            out.fillRect(Rect::fromSize({box.min.x, box.center().y}, {size, 1.0f}), color);
            break;
        case WindowIcon::Maximize:
            out.strokeRect(box, color, 1.0f);
            break;
        case WindowIcon::Restore:
            out.fillRect(Rect::fromSize({box.min.x + 2.0f, box.min.y}, {size - 2.0f, 1.0f}), color);
            out.fillRect(Rect::fromSize({box.max.x - 1.0f, box.min.y}, {1.0f, size - 2.0f}), color);
            out.strokeRect(Rect::fromSize({box.min.x, box.min.y + 2.0f}, glm::vec2{size - 2.0f}), color, 1.0f);
            break;
        case WindowIcon::Close:
            if (auto *text = shaper())
            {
                /// A twelfth of the cell: a pixel at the default size, and the same shape at any other, which is
                /// what lets the atlas share one cell per (id, size) however the icon is sized.
                static constexpr f32 t = 1.0f / 12.0f;
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
    const WindowChrome &_chrome;
};

Button &addControl(Widget &parent, const char *name)
{
    auto &button = parent.add<Button>();
    button.setName(name);
    button.setAccessibleName(name);
    button.setCursor(wma::SystemCursor::Default);
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

    _title = &add<Label>();
    //! The title is part of the draggable bar, not a click target of its own.
    _title->setHitTestVisible(false);

    _minimize = &addControl(*this, "Minimize");
    _maximize = &addControl(*this, "Maximize");
    _close = &addControl(*this, "Close");
    _minimize->add<DecorationIcon>(WindowIcon::Minimize, _chrome);
    _maximizeIcon = &_maximize->add<DecorationIcon>(WindowIcon::Maximize, _chrome);
    _restoreIcon = &_maximize->add<DecorationIcon>(WindowIcon::Restore, _chrome);
    _restoreIcon->setVisibility(Visibility::Collapsed);
    _close->add<DecorationIcon>(WindowIcon::Close, _chrome);
    _applyChrome();

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

void DefaultWindowDecoration::setChrome(const WindowChrome &chrome)
{
    _chrome = chrome;
    _applyChrome();
}

const WindowChrome &DefaultWindowDecoration::chrome() const noexcept
{
    return _chrome;
}

void DefaultWindowDecoration::_applyChrome()
{
    style().fill(_chrome.frame).outline({0, 0, 0, 0}, 0).rounded(0).pad(0);
    _title->setFontSize(_chrome.titleFontSize);
    _title->style().textColor(_chrome.titleText);

    const auto look = [this](Button &button, const ColorSet &colors)
    {
        button.style().fill(colors).textColor(_chrome.icon).outline({0, 0, 0, 0}, 0).rounded(0).pad(0);
    };
    look(*_minimize, _chrome.minimize);
    look(*_maximize, _chrome.maximize);
    look(*_close, _chrome.close);

    invalidateLayout();
}

Thickness DefaultWindowDecoration::contentInsets() const noexcept
{
    const f32 border = _resizable ? _chrome.border : 0.0f;
    return {border, _chrome.titleHeight, border, border};
}

Rect DefaultWindowDecoration::_titleBar() const noexcept
{
    return Rect::fromSize(bounds().min, {bounds().width(), std::min(_chrome.titleHeight, bounds().height())});
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
        const bool top = point.y < frame.min.y + _chrome.resizeReach;
        const bool bottom = point.y >= frame.max.y - _chrome.resizeReach;
        const bool left = point.x < frame.min.x + _chrome.resizeReach;
        const bool right = point.x >= frame.max.x - _chrome.resizeReach;
        const bool nearTop = point.y < frame.min.y + _chrome.cornerReach;
        const bool nearBottom = point.y >= frame.max.y - _chrome.cornerReach;
        const bool nearLeft = point.x < frame.min.x + _chrome.cornerReach;
        const bool nearRight = point.x >= frame.max.x - _chrome.cornerReach;
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
    const f32 buttonWidth = std::min(_chrome.buttonWidth, bar.width() / 3.0f);
    const f32 controlsStart = bar.max.x - 3.0f * buttonWidth;
    f32 x = controlsStart;
    for (Button *button : {_minimize, _maximize, _close})
    {
        button->arrange(Rect::fromSize({x, bar.min.y}, {buttonWidth, bar.height()}));
        x += buttonWidth;
    }

    /// Centred on the window, sliding left only when it would reach the controls.
    const f32 left = bar.min.x + _chrome.titleInset;
    const f32 right = controlsStart - _chrome.titleGap;
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
