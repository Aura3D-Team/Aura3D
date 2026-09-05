#ifndef AURA_UI_WIDGET_H
#define AURA_UI_WIDGET_H

#pragma once

#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "aura/UI/Core/Accessibility.h"
#include "aura/UI/Core/DrawList.h"
#include "aura/UI/Core/Event.h"
#include "aura/UI/Core/Geometry.h"
#include "aura/UI/Core/Signal.h"
#include "aura/UI/Core/Theme.h"
#include "aura/UI/Layout/Layout.h"

/**
 * @file Widget.h
 * @brief The retained widget tree: one node, and everything it can do.
 *
 * Composition, not inheritance. A Button is not a specialised Label; it is a
 * widget that owns a Label and reacts to a press. Subclass Widget to add a
 * *behaviour* the toolkit does not have, not to vary something it already
 * parameterises.
 *
 * @code
 * auto& row = panel.add<Row>();
 * row.spacing = 8.0f;
 *
 * auto& launch = row.add<Button>("Launch");
 * launch.layout().width = Length::fill();
 * launch.clicked.connect([] { launchApplication(); });
 * @endcode
 *
 * Widgets are owned by their parent and never move once built, so callbacks
 * capturing @c this stay valid for as long as the widget is in the tree.
 */

namespace aura3d::ui {

class UIRoot;
class ITextShaper;
class OverlayLayer;
class Widget;

/// Observes a widget without extending its ownership in the tree.
class WidgetRef {
public:
    WidgetRef() = default;
    WidgetRef(Widget* widget);
    [[nodiscard]] Widget* get() const noexcept { return _alive.expired() ? nullptr : _widget; }
private:
    Widget* _widget = nullptr;
    std::weak_ptr<void> _alive;
};

/// Whether a widget draws, and whether it still takes up room when it does
/// not. Collapsed is the one that reflows the layout.
enum class Visibility : u8 { Visible, Hidden, Collapsed };

/**
 * @class Widget
 * @brief A node in the UI tree.
 *
 * @note Neither copyable nor movable. Children hold a parent pointer and
 *       handlers capture @c this; an address that changes would invalidate
 *       both. Build widgets with add(), which allocates them in place.
 */
class Widget {
public:
    Widget();
    virtual ~Widget();

    Widget(const Widget&) = delete;
    Widget& operator=(const Widget&) = delete;
    Widget(Widget&&) = delete;
    Widget& operator=(Widget&&) = delete;

    /// @name Tree
    /// @{

    /**
     * @brief Constructs a child in place and returns it, for configuring.
     *
     * @code
     * auto& title = column.add<Label>("Settings");
     * title.style().text = theme.palette().accent;
     * @endcode
     */
    template <class W, class... Args>
    W& add(Args&&... args)
    {
        auto child = std::make_unique<W>(std::forward<Args>(args)...);
        W& reference = *child;
        _insert(_children.size(), std::move(child));
        return reference;
    }

    template <class W, class... Args>
    W& insert(usize index, Args&&... args)
    {
        auto child = std::make_unique<W>(std::forward<Args>(args)...);
        W& reference = *child;
        _insert(index, std::move(child));
        return reference;
    }

    /// Takes ownership of an already-built subtree.
    Widget& adopt(std::unique_ptr<Widget> child);

    /// Removes @p child and hands it back, still intact -- for moving a
    /// subtree between parents without rebuilding it.
    [[nodiscard]] std::unique_ptr<Widget> detach(Widget& child);

    void remove(Widget& child);
    void clearChildren();

    [[nodiscard]] Widget* parent() const noexcept { return _parent; }
    [[nodiscard]] UIRoot* root() const noexcept { return _root; }

    [[nodiscard]] std::span<const std::unique_ptr<Widget>> children() const noexcept
    {
        return _children;
    }

    [[nodiscard]] usize childCount() const noexcept { return _children.size(); }
    [[nodiscard]] Widget& childAt(usize index) const { return *_children[index]; }

    /// @}

    /// @name Identity
    /// @{

    /// A name for debugging, tests and accessibility fallback. Not drawn.
    void setName(std::string name) { _name = std::move(name); }
    [[nodiscard]] const std::string& name() const noexcept { return _name; }

    /// The first descendant named @p name, depth first. For wiring up a tree
    /// built elsewhere, and for tests.
    [[nodiscard]] Widget* find(std::string_view name) noexcept;

    /// @}

    /// @name Layout
    /// @{

    /// Editable placement. Marks the tree for re-layout, so a widget cannot be
    /// repositioned without the change taking effect.
    [[nodiscard]] LayoutSpec& layout() noexcept
    {
        invalidateLayout();
        return _layout;
    }

    [[nodiscard]] const LayoutSpec& layout() const noexcept { return _layout; }

    /// Final rectangle in surface pixels. Meaningful after the frame's arrange
    /// pass; empty before the first one.
    [[nodiscard]] const Rect& bounds() const noexcept { return _bounds; }

    /// Where this widget's content lives: @ref bounds minus @ref contentInsets.
    /// Also what clipping and hit testing treat as "inside".
    [[nodiscard]] Rect contentRect() const noexcept { return deflate(_bounds, contentInsets()); }

    /// Space between this widget's edges and its content. Padding, plus
    /// whatever the widget reserves for itself -- a scroll view's gutters.
    [[nodiscard]] virtual Thickness contentInsets() const noexcept { return _layout.padding; }

    [[nodiscard]] glm::vec2 desiredSize() const noexcept { return _desired; }

    /// Pass one: reports how big this widget would like to be within @p space,
    /// margin included.
    glm::vec2 measure(const Constraints& space);

    /// Pass two: takes @p slot, places itself in it and arranges its children.
    void arrange(const Rect& slot);

    void invalidateLayout() noexcept;
    void invalidatePaint() noexcept;

    [[nodiscard]] bool layoutDirty() const noexcept { return _layoutDirty; }

    /// @}

    /// @name Visibility and enablement
    /// @{

    void setVisibility(Visibility visibility);
    [[nodiscard]] Visibility visibility() const noexcept { return _visibility; }

    void setEnabled(bool enabled);
    [[nodiscard]] bool enabled() const noexcept { return _enabled; }

    /// False when this widget or any ancestor is disabled -- what a control
    /// must test before reacting, and what greys it out.
    [[nodiscard]] bool effectivelyEnabled() const noexcept;
    [[nodiscard]] bool effectivelyVisible() const noexcept;

    /// @}

    /// @name Painting
    /// @{

    /// Paints this widget and its subtree into @p out. Applies clipping and
    /// skips what is not visible; override paint()/paintChildren() instead of
    /// this.
    void paintTree(DrawList& out);

    /// @}

    /// @name Input
    /// @{

    /// True when @p point (surface coordinates) belongs to this widget.
    /// Override for a non-rectangular control.
    [[nodiscard]] virtual bool hitTest(glm::vec2 point) const;

    /// True to clip descendants to @ref contentRect. A scroll view says yes.
    /// Also tells hit testing that a point outside cannot belong to a child.
    [[nodiscard]] virtual bool clipsChildren() const noexcept { return false; }

    /// The axis this widget lays its children out along. A child that has to
    /// know -- a Spacer, which is thick one way and nothing the other -- asks
    /// its parent rather than being told twice.
    [[nodiscard]] virtual Axis mainAxis() const noexcept { return Axis::Vertical; }

    /// False lets the pointer through to whatever is underneath -- an overlay,
    /// a decoration, a label on top of a card.
    void setHitTestVisible(bool visible) noexcept { _hitTestVisible = visible; }
    [[nodiscard]] bool hitTestVisible() const noexcept { return _hitTestVisible; }

    /// @{
    /// Return true to consume the event and stop it bubbling to the parent.
    virtual bool onPointerDown(const PointerEvent& event);
    virtual bool onPointerUp(const PointerEvent& event);
    virtual bool onPointerMove(const PointerEvent& event);
    virtual bool onWheel(const WheelEvent& event);
    virtual bool onKeyDown(const KeyEvent& event);
    virtual bool onKeyUp(const KeyEvent& event);
    virtual bool onTextInput(const TextEvent& event);
    /// @}

    /// @{
    /// Notifications, not events: they cannot be refused and do not bubble.
    virtual void onPointerEnter();
    virtual void onPointerLeave();
    /// Capture ended without a matching release; discard press/drag state.
    virtual void onPointerCancel();
    virtual void onFocusIn(FocusReason reason);
    virtual void onFocusOut();
    /// @}

    /// @}

    /// @name Focus
    /// @{

    void setFocusable(bool focusable) noexcept { _focusable = focusable; }

    /// True when this widget can take keyboard focus *right now*: focusable,
    /// visible and not disabled.
    [[nodiscard]] bool focusable() const noexcept;

    [[nodiscard]] bool hasFocus() const noexcept;
    void requestFocus(FocusReason reason = FocusReason::Programmatic);

    [[nodiscard]] bool isHovered() const noexcept;

    /// True when this widget wants the platform's text input active while it
    /// has focus. Drives IWindowManager::setTextInputEnabled(), and tells the
    /// application the UI owns the keyboard.
    [[nodiscard]] virtual bool wantsTextInput() const noexcept { return false; }

    /// @{
    /// While captured, every pointer event goes to this widget wherever the
    /// cursor is -- what makes a drag survive leaving the control.
    void capturePointer();
    void releasePointer();
    [[nodiscard]] bool hasPointerCapture() const noexcept;
    /// @}

    /**
     * @brief Text shown after the pointer rests on this widget.
     *
     * Opened by the root in the floating layer, so it is never clipped by an
     * ancestor and never affects layout. Inherited by descendants that set
     * none of their own, which is what lets a whole row explain itself.
     */
    void setTooltip(std::string text) { _tooltip = std::move(text); }
    [[nodiscard]] const std::string& tooltip() const noexcept { return _tooltip; }

    /// @}

    /// @name Theming
    /// @{

    /// Per-widget overrides on top of the theme's entry for this widget's
    /// @ref Part. Marks the widget for repaint.
    [[nodiscard]] Style& style() noexcept
    {
        invalidateLayout();
        return _style;
    }

    [[nodiscard]] const Style& style() const noexcept { return _style; }

    [[nodiscard]] Part part() const noexcept { return _part; }

    /**
     * @brief Restyles this widget as a different component.
     *
     * The same row is a list entry, a menu item and a tab depending only on
     * which @ref Part it reads, so this is what lets one @ref Selectable serve
     * all three instead of three near-identical widgets existing.
     */
    void setPart(Part part) noexcept
    {
        _part = part;
        invalidateLayout();
    }

    /// The theme's entry for @ref part with this widget's @ref style over it.
    /// Resolved per paint rather than cached, so a theme edit lands on the
    /// next frame without anyone having to invalidate the tree.
    [[nodiscard]] WidgetStyle resolvedStyle() const;

    [[nodiscard]] const Theme& theme() const noexcept;

    /// @}

    /// @name Accessibility
    /// @{

    /// Fills @p out with what this widget exposes. Overrides should call the
    /// base first and then set what they know.
    virtual void accessibility(AccessibilityInfo& out) const;

    /// Overrides the name a screen reader reads. Required for any control
    /// whose label is an icon.
    void setAccessibleName(std::string name) { _accessibleName = std::move(name); }
    void setAccessibleDescription(std::string text) { _accessibleDescription = std::move(text); }

    /// @}

protected:
    /// @name For subclasses
    /// @{

    /// This widget's intrinsic content size within @p available, ignoring its
    /// own margin, padding and @ref Length -- the base class applies those.
    /// Containers measure their children here.
    virtual glm::vec2 measureContent(const Constraints& available);

    /// Places children inside @p content, which is @ref bounds minus padding.
    virtual void arrangeContent(const Rect& content);

    /// Draws this widget itself. Children are drawn after, by paintChildren().
    virtual void paint(DrawList& out);

    virtual void paintChildren(DrawList& out);

    /// Called once per frame while animating() is true.
    /// @return False to stop being ticked.
    virtual bool onTick(f32 deltaSeconds);

    /// Asks the root to call onTick() every frame. Cheap to toggle; a UI with
    /// nothing animating does no per-frame work.
    void setAnimating(bool animating);
    [[nodiscard]] bool animating() const noexcept { return _animating; }

    /// @{
    /// Structural notifications, for a container keeping its own bookkeeping.
    virtual void onChildAdded(Widget& child, usize index);
    virtual void onChildRemoved(usize index);
    virtual void onAttach();
    virtual void onDetach();
    /// @}

    /// The shaper this tree draws text with; null before the widget is
    /// attached to a root.
    [[nodiscard]] ITextShaper* shaper() const noexcept;

    /// The tree's floating layer, for a widget that opens a menu or a
    /// drop-down. Null before the widget is attached to a root.
    [[nodiscard]] OverlayLayer* overlay() const noexcept;

    /// Which theme entry this widget reads. Set in a subclass' constructor.
    Part _part = Part::Container;

    /// @}

private:
    friend class UIRoot;
    friend class WidgetRef;

    std::shared_ptr<void> _alive;

    void _insert(usize index, std::unique_ptr<Widget> child);
    void _setRoot(UIRoot* root);

    /// The size this widget settled on in measure(), margin excluded -- what
    /// arrange() places, and what tells stretch from an explicit size.
    glm::vec2 _measured{0.0f};
    glm::vec2 _desired{0.0f};
    Rect _bounds{};

    Widget* _parent = nullptr;
    UIRoot* _root = nullptr;
    std::vector<std::unique_ptr<Widget>> _children;

    LayoutSpec _layout{};
    Style _style{};

    std::string _name;
    std::string _tooltip;
    std::string _accessibleName;
    std::string _accessibleDescription;

    Visibility _visibility = Visibility::Visible;
    bool _enabled = true;
    bool _focusable = false;
    bool _hitTestVisible = true;
    bool _animating = false;

    bool _layoutDirty = true;

    //! Set by measure() when a Length pinned the axis, so arrange() knows not
    //! to let Alignment::Stretch overrule it.
    bool _fixedWidth = false;
    bool _fixedHeight = false;
};

} // namespace aura3d::ui

#endif // AURA_UI_WIDGET_H
