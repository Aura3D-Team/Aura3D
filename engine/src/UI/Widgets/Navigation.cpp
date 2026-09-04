#include "aura/UI/Widgets/Navigation.h"

#include <algorithm>

#include "aura/UI/Core/Icon.h"

namespace aura3d::ui {

namespace {

/// The arrow's share of the header row's height.
constexpr f32 kArrowScale = 0.42f;

/// How long the tab underline takes to travel.
constexpr f32 kIndicatorSeconds = 0.16f;

} // namespace

// =============================================================================
// Disclosure
// =============================================================================

Disclosure::Disclosure(Part part, std::string title, bool startExpanded)
    : expanded(startExpanded)
{
    _part = part;

    _label = &add<Label>(std::move(title));
    _content = &add<Column>();

    _content->setVisibility(startExpanded ? Visibility::Visible : Visibility::Collapsed);

    expanded.changed().connect([this](bool open) {
        _content->setVisibility(open ? Visibility::Visible : Visibility::Collapsed);
        invalidateLayout();
    });
}

void Disclosure::setTitle(std::string title) { _label->text = std::move(title); }

f32 Disclosure::headerHeight() const
{
    const WidgetStyle style = resolvedStyle();
    return std::max(style.height.value_or(theme().metrics.rowHeight), _label->desiredSize().y);
}

Rect Disclosure::headerRect() const
{
    return Rect{bounds().min, {bounds().max.x, bounds().min.y + headerHeight()}};
}

bool Disclosure::hitTest(glm::vec2 point) const
{
    return hitTestVisible() && headerRect().contains(point);
}

glm::vec2 Disclosure::measureContent(const Constraints& available)
{
    const WidgetStyle style = resolvedStyle();
    const f32 row = style.height.value_or(theme().metrics.rowHeight);
    const f32 gutter = row * kArrowScale + style.padding;

    Constraints headerSpace = available;
    if (!isUnbounded(headerSpace.max.x))
        headerSpace.max.x = std::max(0.0f, headerSpace.max.x - gutter);

    const glm::vec2 label = _label->measure(headerSpace);
    const f32 header = std::max(row, label.y);

    if (!expanded.get())
        return {gutter + label.x, header};

    Constraints contentSpace = available;
    if (!isUnbounded(contentSpace.max.x))
        contentSpace.max.x = std::max(0.0f, contentSpace.max.x - _indent);

    const glm::vec2 content = _content->measure(contentSpace);

    return {std::max(gutter + label.x, _indent + content.x), header + content.y};
}

void Disclosure::arrangeContent(const Rect& content)
{
    const WidgetStyle style = resolvedStyle();
    const f32 row = style.height.value_or(theme().metrics.rowHeight);
    const f32 gutter = row * kArrowScale + style.padding;
    const f32 header = headerHeight();

    _label->arrange(Rect{{content.min.x + gutter, content.min.y},
                         {content.max.x, content.min.y + header}});

    if (!expanded.get())
    {
        _content->arrange(Rect{});
        return;
    }

    _content->arrange(Rect{{content.min.x + _indent, content.min.y + header}, content.max});
}

void Disclosure::paint(DrawList& out)
{
    const WidgetStyle style = resolvedStyle();
    const f32 alpha = effectivelyEnabled() ? 1.0f : theme().metrics.disabledAlpha;
    const Rect header = headerRect();

    out.drawRect(header, withAlpha(surfaceColor(style), alpha),
                 withAlpha(style.border.pick(expanded.get(), isHovered()), alpha),
                 style.borderWidth, Corners::all(style.rounding));

    if (_foldable)
    {
        if (ITextShaper* shaper = this->shaper())
        {
            const f32 size = header.height() * kArrowScale;
            const Rect box =
                Rect::fromSize({header.min.x + style.padding * 0.5f, header.center().y - size * 0.5f},
                               {size, size});

            //! The arrow is the state: pointing along the row when folded,
            //! down into the content when open.
            icon::triangle(out, *shaper, box,
                           expanded.get() ? icon::Direction::Down : icon::Direction::Right,
                           withAlpha(style.text, alpha));
        }
    }

    paintFocusRing(out, style, header);
}

void Disclosure::activate()
{
    if (_foldable && effectivelyEnabled())
        expanded = !expanded.get();
}

void Disclosure::accessibility(AccessibilityInfo& out) const
{
    Widget::accessibility(out);

    out.role = Role::Group;
    out.expanded = expanded.get();

    if (out.name.empty())
        out.name = _label->text.get();
}

// =============================================================================
// CollapsingHeader
// =============================================================================

CollapsingHeader::CollapsingHeader(std::string title, bool startExpanded)
    : Disclosure(Part::Header, std::move(title), startExpanded)
{
}

// =============================================================================
// TreeNode
// =============================================================================

TreeNode::TreeNode(std::string title, bool startExpanded)
    : Disclosure(Part::TreeNode, std::move(title), startExpanded)
{
    _indent = theme().metrics.indent;

    //! A node with nothing under it is a leaf: no arrow, and clicking it
    //! selects rather than folds.
    _foldable = false;

    selected.changed().connect([this](bool) { invalidatePaint(); });
}

TreeNode& TreeNode::addChild(std::string title, bool startExpanded)
{
    //! The first child is what turns a leaf into a branch.
    _foldable = true;
    invalidateLayout();

    return content().add<TreeNode>(std::move(title), startExpanded);
}

void TreeNode::paint(DrawList& out)
{
    if (selected.get())
    {
        const WidgetStyle style = resolvedStyle();
        const f32 alpha = effectivelyEnabled() ? 1.0f : theme().metrics.disabledAlpha;

        out.fillRect(headerRect(), withAlpha(style.surface.active, alpha),
                     Corners::all(style.rounding));
    }

    Disclosure::paint(out);
}

void TreeNode::activate()
{
    if (!effectivelyEnabled())
        return;

    //! Both, in that order: clicking a branch selects it *and* folds it, which
    //! is what every file tree does.
    activated.emit();
    Disclosure::activate();
}

void TreeNode::accessibility(AccessibilityInfo& out) const
{
    Disclosure::accessibility(out);

    out.role = Role::ListItem;
    out.selected = selected.get();
}

// =============================================================================
// TabView
// =============================================================================

TabView::TabView() : current(0)
{
    _bar = &add<Row>();
    _bar->setSpacing(2.0f);

    _pages = &add<Widget>();

    current.changed().connect([this](int) { _apply(); });
}

usize TabView::tabCount() const noexcept
{
    return _pages ? _pages->childCount() : 0;
}

Column& TabView::addTab(std::string title)
{
    const auto index = static_cast<int>(tabCount());

    auto& tab = _bar->add<Selectable>(std::move(title));
    tab.setPart(Part::Tab);
    tab.activated.connect([this, index] { current = index; });

    auto& page = _pages->add<Column>();

    _apply();
    return page;
}

void TabView::_apply()
{
    const int count = static_cast<int>(tabCount());
    if (count == 0)
        return;

    const int active = std::clamp(current.get(), 0, count - 1);

    //! Written back so an out-of-range assignment settles at a real tab rather
    //! than leaving the property disagreeing with what is shown.
    if (active != current.get())
    {
        current.set(active);
        return;
    }

    for (int i = 0; i < count; ++i)
    {
        static_cast<Selectable&>(_bar->childAt(static_cast<usize>(i))).selected = i == active;

        _pages->childAt(static_cast<usize>(i))
            .setVisibility(i == active ? Visibility::Visible : Visibility::Collapsed);
    }

    const Rect tab = _bar->childAt(static_cast<usize>(active)).bounds();

    if (!tab.empty())
    {
        _indicatorX.to(tab.min.x, kIndicatorSeconds);
        _indicatorWidth.to(tab.width(), kIndicatorSeconds);
        setAnimating(true);
    }

    invalidateLayout();
}

glm::vec2 TabView::measureContent(const Constraints& available)
{
    const glm::vec2 bar = _bar->measure(available);

    Constraints pageSpace = available;
    pageSpace.max.y = std::max(0.0f, pageSpace.max.y - bar.y);

    const glm::vec2 pages = _pages->measure(pageSpace);

    return {std::max(bar.x, pages.x), bar.y + pages.y};
}

void TabView::arrangeContent(const Rect& content)
{
    const f32 barHeight = _bar->desiredSize().y;

    _bar->arrange(Rect{content.min, {content.max.x, content.min.y + barHeight}});
    _pages->arrange(Rect{{content.min.x, content.min.y + barHeight}, content.max});

    //! Seeded once the bar has real geometry, so the first frame does not
    //! animate the underline in from zero.
    if (_indicatorWidth.target() == 0.0f && tabCount() > 0)
    {
        const Rect tab = _bar->childAt(static_cast<usize>(std::clamp(
                                           current.get(), 0, static_cast<int>(tabCount()) - 1)))
                             .bounds();

        _indicatorX.reset(tab.min.x);
        _indicatorWidth.reset(tab.width());
    }
}

void TabView::paint(DrawList& out)
{
    Widget::paint(out);

    if (tabCount() == 0 || _indicatorWidth.value() <= 0.0f)
        return;

    const WidgetStyle style = theme()[Part::Tab];
    const f32 alpha = effectivelyEnabled() ? 1.0f : theme().metrics.disabledAlpha;

    const f32 thickness = 2.0f;
    const f32 y = _bar->bounds().max.y - thickness;

    out.fillRect(Rect::fromSize({_indicatorX.value(), y}, {_indicatorWidth.value(), thickness}),
                 withAlpha(style.accent, alpha), Corners::all(thickness * 0.5f));
}

bool TabView::onTick(f32 deltaSeconds)
{
    const bool x = _indicatorX.tick(deltaSeconds);
    const bool width = _indicatorWidth.tick(deltaSeconds);

    invalidatePaint();
    return x || width;
}

bool TabView::onKeyDown(const KeyEvent& event)
{
    const int count = static_cast<int>(tabCount());
    if (count == 0)
        return false;

    switch (event.key)
    {
    case wma::KEY_LEFT:
        current = std::max(0, current.get() - 1);
        return true;

    case wma::KEY_RIGHT:
        current = std::min(count - 1, current.get() + 1);
        return true;

    default:
        break;
    }

    return false;
}

void TabView::accessibility(AccessibilityInfo& out) const
{
    Widget::accessibility(out);
    out.role = Role::Group;
}

} // namespace aura3d::ui
