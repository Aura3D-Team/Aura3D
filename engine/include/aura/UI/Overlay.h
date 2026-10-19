#ifndef AURA_UI_OVERLAY_H
#define AURA_UI_OVERLAY_H

#pragma once

#include <functional>
#include <utility>
#include <vector>

#include "aura/UI/Widget.h"

/**
 * @file Overlay.h
 * @brief The floating layer: everything that escapes its parent's rectangle.
 *
 * A drop-down list, a tooltip, a context menu and a dialog are all the same
 * problem -- content that must be positioned against something, drawn over
 * everything, hit-tested before everything, and dismissed by a click
 * elsewhere. Solving it once here is what stops each of those from inventing
 * its own z-order, its own clipping escape and its own dismissal rule.
 *
 * @code
 * auto& menu = overlay->open<Column>({.anchor = button.bounds(),
 *                                     .placement = Placement::Below});
 * menu.add<Selectable>("Rename");
 * @endcode
 *
 * The layer is an ordinary @ref Widget, so painting, hit testing, focus
 * traversal and invalidation all reach it through the machinery the rest of
 * the tree already uses. Only its placement is special.
 */

namespace aura3d::ui
{

/// Where an overlay sits relative to its anchor. Every option flips to its
/// opposite when the surface has no room, which is what keeps a drop-down near
/// the bottom of the window from opening off-screen.
enum class Placement : u8
{
    Below, //! Under the anchor, left edges aligned. Drop-downs.
    Above,
    Right, //! Beside the anchor, top edges aligned. Submenus.
    Left,
    Over,   //! Directly on the anchor. Popovers that replace their trigger.
    Cursor, //! At the anchor's top-left, nudged clear. Tooltips, context menus.
    Center, //! Centred in the surface, ignoring the anchor. Dialogs.
};

struct OverlayDesc;

/// How an overlay arrives and leaves. AuraUI animates nothing itself; both hooks are the application's.
struct OverlayTransition
{
    /// After the overlay is first placed and before it is first drawn, so bounds() are final and nothing shows in its
    /// resting state first: set its starting state and start the entrance, with UIRoot::animate() for instance.
    std::function<void(Widget &overlay, const OverlayDesc &desc)> enter;

    /// From close(). The overlay keeps drawing, takes no input, and onClosed waits, until @p done is called: call it
    /// once the exit has finished, from any later point. Unset, the overlay is gone at the next frame boundary.
    std::function<void(Widget &overlay, const OverlayDesc &desc, std::function<void()> done)> exit;

    /// An exit still running after this long is cut short as if @c done had been called, so a hook that never calls it
    /// cannot leave a dead overlay on screen. Zero waits for ever.
    f32 exitTimeoutSeconds = 5.0f;
};

/// How one overlay is placed and dismissed.
struct OverlayDesc
{
    /// Rectangle to position against, in surface coordinates. A degenerate
    /// rectangle is a point, which is what @ref Placement::Cursor wants.
    Rect anchor{};

    Placement placement = Placement::Below;

    /// Nudge applied after placement. Positive y pushes further down.
    glm::vec2 offset{0.0f};

    /// How far a @ref Placement::Cursor overlay clears the pointer, so the thing it describes is not under the thing
    /// describing it.
    f32 cursorClearance = 14.0f;

    /// Swallows input to everything underneath, including other overlays below
    /// it. A dialog wants this; a tooltip must not have it.
    bool modal = false;

    /// @{
    /// Light dismissal. Both default on, because an overlay that cannot be
    /// dismissed by the two things everyone tries is a trap.
    bool dismissOnOutsideClick = true;
    bool dismissOnEscape = true;
    bool takeFocus = true;
    Widget *owner = nullptr;
    /// @}

    /// This overlay's own; a hook left unset falls back to OverlayLayer::setDefaultTransition().
    OverlayTransition transition{};

    /// Called once the overlay is gone, so an opener can forget its id. Runs
    /// after the widget is destroyed, never during the event that closed it.
    std::function<void()> onClosed;
};

/**
 * @class OverlayLayer
 * @brief Owns the open overlays, in draw order.
 *
 * Later entries draw on top and are hit first, so "the topmost overlay" is
 * simply the last child.
 *
 * @note Closing is deferred to the next frame boundary. A menu item that
 *       closes the menu it lives in is the normal case, not an edge case, and
 *       destroying the widget inside its own click handler would pull the
 *       ground out from under the dispatch that is still running.
 */
class OverlayLayer final : public Widget
{
  public:
    using Id = u64;

    /// Never returned by open(); a stored id of this value means "nothing".
    static constexpr Id kNone = 0;

    OverlayLayer();

    /**
     * @brief Opens an overlay holding a fresh widget of type @p W.
     * @return The widget, for filling in. Valid until it is closed.
     */
    template <class W, class... Args> W &open(const OverlayDesc &desc, Args &&...args)
    {
        W &widget = add<W>(std::forward<Args>(args)...);
        _register(desc);
        return widget;
    }

    /// The id of the most recently opened overlay. Pair it with open().
    [[nodiscard]] Id lastId() const noexcept;

    /// Marks @p id closed. It stops taking input at once, and drawing too unless an exit transition runs; it goes
    /// structurally at the first collectClosed() after that.
    void close(Id id);

    /// For every overlay whose desc leaves a hook unset, including the drop-downs, submenus and tooltips AuraUI
    /// opens itself: the one place an application animates all of them. The desc says which kind each one is.
    void setDefaultTransition(OverlayTransition transition);

    /**
     * @brief Closes the topmost overlay that dismisses on Escape.
     *
     * Overlays that do not -- a tooltip, which comes and goes with the pointer
     * -- are stepped over rather than treated as a wall, so Escape reaches the
     * menu underneath one.
     *
     * @return False when nothing was closed, so a caller knows whether the key
     *         was actually used.
     */
    bool closeTopmost();

    void closeAll();

    /// Closes every overlay that light-dismisses, topmost first, stopping at
    /// one that does not -- so a click outside a menu inside a modal dialog
    /// closes the menu and leaves the dialog.
    void closeLightDismissible();

    /// Closes the overlay holding @p member and every overlay above it, and
    /// nothing below: a chosen menu command takes its menu stack down and
    /// leaves the panel the menu was opened from standing.
    void closeFrom(const Widget &member);

    /// The owner the overlay holding @p member was opened with, if any.
    [[nodiscard]] Widget *ownerOf(const Widget &member) const noexcept;

    /// An overlay a user can reach, for the accessibility tree.
    struct OpenOverlay
    {
        const Widget *widget = nullptr;
        bool modal = false;
    };

    /// The overlays that are open and take the pointer, back to front. One that is closing, even while its exit
    /// still draws, is gone to a screen reader; so is a click-through one, a tooltip, which is a description of its
    /// owner and already part of it.
    [[nodiscard]] std::vector<OpenOverlay> openOverlays() const;

    [[nodiscard]] bool isOpen(Id id) const noexcept;
    [[nodiscard]] bool empty() const noexcept
    {
        return childCount() == 0;
    }

    /// True while any open overlay is modal.
    [[nodiscard]] bool hasModal() const noexcept;

    /// True while an open, clickable overlay closes on an outside click: a menu, a
    /// drop-down. What closeLightDismissible() would close.
    [[nodiscard]] bool hasLightDismissible() const noexcept;

    [[nodiscard]] Widget *topmost() const noexcept;
    [[nodiscard]] Widget *focusScope() const noexcept;
    [[nodiscard]] Widget *widgetAt(glm::vec2 point) const;
    [[nodiscard]] bool allowsFocus(const Widget *widget) const noexcept;
    bool dismissOutside(glm::vec2 point);
    void syncFocus();
    void forgetOwner(Widget &owner);

    /// True when @p point falls inside any open overlay.
    [[nodiscard]] bool contains(glm::vec2 point) const;

    /// Destroys what close() marked and has finished leaving. UIRoot calls this between frames.
    void collectClosed();

    /// Runs the enter hook of every overlay that has just been placed. UIRoot calls this at the end of layout.
    void runEnterTransitions();

    /// Overlays place themselves; a parent never arranges one.
    [[nodiscard]] bool clipsChildren() const noexcept override
    {
        return false;
    }

  protected:
    glm::vec2 measureContent(const Constraints &available) override;
    void arrangeContent(const Rect &content) override;
    void onChildRemoved(usize index) override;

  private:
    struct Entry
    {
        Id id = kNone;
        OverlayDesc desc{};
        bool closing = false;
        WidgetRef owner;
        WidgetRef restoreFocus;
        bool focusPending = true;
        bool placed = false;     //! Laid out at least once, which is when its enter hook runs and it can be seen.
        bool exiting = false;    //! Closing, and still drawn until its exit transition calls done.
        f32 exitDeadline = 0.0f; //! UIRoot::time() at which an exit that has not called done is cut short.
    };

    /// Top-left @p size should sit at, given @p desc, flipped and then clamped
    /// so the overlay is always wholly on screen when it can be.
    [[nodiscard]] static glm::vec2 place(const OverlayDesc &desc, glm::vec2 size, const Rect &surface);

    //! Parallel to children(): entry i describes child i. Kept in step by
    //! open() appending to both and onChildRemoved() erasing from this one.
    std::vector<Entry> _entries;
    void _register(const OverlayDesc &desc);
    void _finishExit(Id id);

    OverlayTransition _defaultTransition{};
    Id _nextId = 1;
    bool _pendingClose = false;
};

} // namespace aura3d::ui

#endif // AURA_UI_OVERLAY_H
