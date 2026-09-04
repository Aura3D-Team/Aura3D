#include "aura/UI/Widgets/Menus.h"

#include <algorithm>

#include "aura/UI/Core/Icon.h"
#include "aura/UI/UIRoot.h"

namespace aura3d::ui {

namespace {

/// Styles a floating column as a surface: the look every popup in the toolkit
/// shares, taken from the theme rather than restated per call site.
void styleAsPopup(Widget& popup, const Theme& theme)
{
    const WidgetStyle& style = theme[Part::Tooltip];

    popup.style().fill(style.surface.normal).rounded(style.rounding);
    popup.layout().padding = Thickness::all(style.padding);

    if (style.borderWidth > 0.0f)
        popup.style().outline(style.border.normal, style.borderWidth);
}

} // namespace

// =============================================================================
// Menu
// =============================================================================

Menu::Menu() : Box(Axis::Vertical)
{
    setSpacing(1.0f);
}

void Menu::onAttach()
{
    styleAsPopup(*this, theme());
}

Selectable& Menu::addItem(std::string text, std::string shortcut)
{
    auto& item = add<Selectable>(std::move(text));
    item.setPart(Part::DropdownItem);

    if (!shortcut.empty())
        item.setDetail(std::move(shortcut));

    //! Every command closes the menu it was chosen from. Connected before the
    //! caller's own handler, so the menu is already on its way out by the time
    //! the command runs -- and a command that opens another menu still works,
    //! because closing is deferred to the frame boundary.
    item.activated.connect([this] { dismiss(); });

    return item;
}

void Menu::addSeparator()
{
    auto& rule = add<Separator>();
    rule.layout().margin = Thickness::symmetric(0.0f, 3.0f);
}

Selectable& Menu::addSubmenu(std::string text, std::function<void(Menu&)> build)
{
    auto& item = add<Selectable>(std::move(text));
    item.setPart(Part::DropdownItem);
    item.setDetail(">");

    //! Anchored to the row, so the child opens beside its parent and flips to
    //! the other side when there is no room -- Placement::Right's own job.
    item.activated.connect([this, &item, build = std::move(build)] {
        OverlayLayer* layer = overlay();
        if (!layer || !build)
            return;

        build(layer->open<Menu>(OverlayDesc{.anchor = item.bounds(),
                                            .placement = Placement::Right}));
    });

    return item;
}

void Menu::dismiss()
{
    //! Closes this menu and anything opened over it, which for a chain of
    //! submenus is the whole stack -- picking a command should not leave its
    //! parent menus standing.
    if (OverlayLayer* layer = overlay())
        layer->closeLightDismissible();
}

// =============================================================================
// Dropdown
// =============================================================================

Dropdown::Dropdown(std::vector<std::string> items)
    : selected(-1), _items(std::move(items))
{
    _part = Part::Dropdown;

    selected.changed().connect([this](int) {
        _shapedSource.clear(); // force a re-shape of the caption
        invalidateLayout();
    });
}

Dropdown::~Dropdown() = default;

void Dropdown::setItems(std::vector<std::string> items)
{
    _items = std::move(items);

    close();
    selected.set(std::clamp(selected.get(), -1, static_cast<int>(_items.size()) - 1));

    _shapedSource.clear();
    invalidateLayout();
}

void Dropdown::setPlaceholder(std::string text)
{
    _placeholder = std::move(text);
    _shapedSource.clear();
    invalidateLayout();
}

const std::string& Dropdown::text() const noexcept
{
    const int index = selected.get();

    return index >= 0 && index < static_cast<int>(_items.size()) ? _items[static_cast<usize>(index)]
                                                                 : _placeholder;
}

void Dropdown::_reshape()
{
    ITextShaper* shaper = this->shaper();
    if (!shaper)
        return;

    TextStyle style{};
    style.pixelSize = theme().metrics.fontSize;

    if (_shapedSource == text() && _shapedFontSize == style.pixelSize)
        return;

    shaper->shape(text(), style, kUnbounded, _shaped);

    _shapedSource = text();
    _shapedFontSize = style.pixelSize;
}

glm::vec2 Dropdown::measureContent(const Constraints& available)
{
    _reshape();

    const WidgetStyle style = resolvedStyle();
    const f32 row = style.height.value_or(theme().metrics.rowHeight);

    //! Room for the caption, the gap and the chevron. Measured from the widest
    //! item rather than the current one, so choosing a longer option does not
    //! resize the control and reflow the form around it.
    f32 widest = _shaped.size.x;

    if (ITextShaper* shaper = this->shaper())
    {
        TextStyle textStyle{};
        textStyle.pixelSize = theme().metrics.fontSize;

        ShapedText probe;
        for (const std::string& item : _items)
        {
            shaper->shape(item, textStyle, kUnbounded, probe);
            widest = std::max(widest, probe.size.x);
        }
    }

    (void)available;
    return {widest + style.padding * 2.0f + row, row};
}

void Dropdown::paint(DrawList& out)
{
    const WidgetStyle style = resolvedStyle();
    const f32 alpha = effectivelyEnabled() ? 1.0f : theme().metrics.disabledAlpha;

    out.drawRect(bounds(), withAlpha(surfaceColor(style), alpha),
                 withAlpha(style.border.pick(isOpen(), isHovered()), alpha), style.borderWidth,
                 Corners::all(style.rounding));

    const Rect content = contentRect();
    const f32 chevron = content.height() * 0.4f;

    //! The caption is dimmed while it is the placeholder, so "nothing chosen"
    //! reads differently from a choice that happens to be first in the list.
    const bool empty = selected.get() < 0;
    const glm::vec4 text = withAlpha(style.text, alpha * (empty ? 0.5f : 1.0f));

    if (!_shaped.empty())
    {
        const ClipScope clipped(out, content);
        out.drawText(_shaped, {content.min.x, content.center().y - _shaped.size.y * 0.5f}, text);
    }

    if (ITextShaper* shaper = this->shaper())
    {
        const Rect box = Rect::fromSize(
            {content.max.x - chevron, content.center().y - chevron * 0.5f}, {chevron, chevron});

        icon::triangle(out, *shaper, box, icon::Direction::Down, withAlpha(style.text, alpha));
    }

    paintFocusRing(out, style, bounds());
}

void Dropdown::open()
{
    OverlayLayer* layer = overlay();
    if (!layer || isOpen() || _items.empty() || !effectivelyEnabled())
        return;

    auto& list = layer->open<Column>(OverlayDesc{
        .anchor = bounds(),
        .placement = Placement::Below,
        .onClosed = [this] {
            _popup = OverlayLayer::kNone;
            _list = nullptr;
            _highlighted = -1;
            invalidatePaint();
        },
    });

    _popup = layer->lastId();
    _list = &list;

    styleAsPopup(list, theme());

    //! At least as wide as the trigger: a list narrower than the control it
    //! drops from reads as belonging to something else.
    list.layout().minWidth = bounds().width();

    for (usize i = 0; i < _items.size(); ++i)
    {
        auto& row = list.add<Selectable>(_items[i]);
        row.setPart(Part::DropdownItem);
        row.selected = static_cast<int>(i) == selected.get();

        row.activated.connect([this, i] { _commit(static_cast<int>(i)); });
    }

    _highlight(selected.get());
    invalidatePaint();
}

void Dropdown::close()
{
    if (OverlayLayer* layer = overlay(); layer && isOpen())
        layer->close(_popup);
}

void Dropdown::onDetach()
{
    //! A dropdown removed from the tree while open would otherwise leave its
    //! list floating with nothing behind it.
    close();
}

void Dropdown::activate()
{
    if (isOpen())
        close();
    else
        open();
}

void Dropdown::_highlight(int index)
{
    if (!_list)
        return;

    _highlighted = index;

    for (usize i = 0; i < _list->childCount(); ++i)
    {
        auto& row = static_cast<Selectable&>(_list->childAt(i));
        row.selected = static_cast<int>(i) == index;
    }
}

void Dropdown::_commit(int index)
{
    selected.set(std::clamp(index, -1, static_cast<int>(_items.size()) - 1));
    close();
}

void Dropdown::_step(int delta)
{
    if (_items.empty())
        return;

    const int count = static_cast<int>(_items.size());
    const int from = isOpen() ? _highlighted : selected.get();
    const int next = std::clamp(from + delta, 0, count - 1);

    if (isOpen())
        _highlight(next);
    else
        _commit(next);
}

bool Dropdown::onKeyDown(const KeyEvent& event)
{
    switch (event.key)
    {
    case wma::KEY_UP:
        _step(-1);
        return true;

    case wma::KEY_DOWN:
        _step(1);
        return true;

    case wma::KEY_ENTER:
    case wma::KEY_SPACE:
        if (isOpen())
            _commit(_highlighted);
        else
            open();
        return true;

    default:
        break;
    }

    //! Escape is left alone: UIRoot closes the topmost overlay with it, which
    //! is this list, and duplicating that here would close it twice.
    return false;
}

void Dropdown::accessibility(AccessibilityInfo& out) const
{
    Widget::accessibility(out);

    out.role = Role::Button;
    out.expanded = isOpen();

    if (out.name.empty())
        out.name = text();
}

} // namespace aura3d::ui
