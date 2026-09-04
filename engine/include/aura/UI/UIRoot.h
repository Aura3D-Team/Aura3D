#ifndef AURA_UI_UIROOT_H
#define AURA_UI_UIROOT_H

#pragma once

#include <functional>
#include <memory>
#include <string_view>
#include <vector>

#include "aura/UI/Core/Accessibility.h"
#include "aura/UI/Core/Event.h"
#include "aura/UI/Core/Signal.h"
#include "aura/UI/Core/Theme.h"
#include "aura/UI/Overlay.h"
#include "aura/UI/Widget.h"

/**
 * @file UIRoot.h
 * @brief The tree's owner: layout pass, paint pass, and where input enters.
 *
 * Knows nothing about a window, a renderer or a platform. It takes positions
 * and key codes and produces a @ref DrawList, which is what lets the same UI
 * run on a Wayland surface, an Android view, a WASM canvas or a test with no
 * display at all.
 *
 * @code
 * UIRoot root(shaper);
 * root.resize({1280.0f, 720.0f});
 *
 * auto& column = root.setContent<Column>();
 * column.add<Button>("Launch").clicked.connect([] { launch(); });
 *
 * // per frame
 * root.update(deltaSeconds);
 * root.paint(drawList);
 * @endcode
 */

namespace aura3d::ui {

class ITextShaper;

/**
 * @class UIRoot
 * @brief One widget tree, its input state and its frame loop.
 *
 * @note Not thread-safe: one tree, one thread, like the renderer it feeds.
 */
class UIRoot {
public:
    /**
     * @param shaper Text engine the whole tree lays out with; must outlive
     *        this object.
     */
    explicit UIRoot(ITextShaper& shaper, Theme theme = Theme{});
    ~UIRoot();

    UIRoot(const UIRoot&) = delete;
    UIRoot& operator=(const UIRoot&) = delete;

    /// @name Content
    /// @{

    /// Replaces the tree with a fresh widget of type @p W and returns it.
    template <class W, class... Args>
    W& setContent(Args&&... args)
    {
        auto content = std::make_unique<W>(std::forward<Args>(args)...);
        W& reference = *content;
        _adopt(std::move(content));
        return reference;
    }

    Widget& setContent(std::unique_ptr<Widget> content);

    [[nodiscard]] Widget* content() const noexcept { return _content.get(); }

    /**
     * @brief The floating layer: drop-downs, menus, tooltips, dialogs.
     *
     * Drawn over the content, hit-tested before it, and where anything that
     * has to escape its parent's rectangle belongs. See @ref OverlayLayer.
     */
    [[nodiscard]] OverlayLayer& overlay() noexcept { return *_overlay; }
    [[nodiscard]] const OverlayLayer& overlay() const noexcept { return *_overlay; }

    /// @}

    /// @name Surface
    /// @{

    /// Sets the surface size in *logical* pixels. Layout is DPI-independent;
    /// see setScale().
    void resize(glm::vec2 logicalSize);

    [[nodiscard]] glm::vec2 size() const noexcept { return _size; }
    [[nodiscard]] Rect surface() const noexcept { return Rect::fromSize({0.0f, 0.0f}, _size); }

    /**
     * @brief Sets the device-pixel ratio.
     *
     * Only text rasterization and the backend's vertex emission change: a
     * 48-pixel-tall button is 48 logical units at any scale, so no layout code
     * ever multiplies by it.
     */
    void setScale(f32 scale);
    [[nodiscard]] f32 scale() const noexcept { return _scale; }

    /// @}

    /// @name Frame
    /// @{

    /// Advances animations and re-runs layout if anything invalidated it. An
    /// idle tree does nothing here.
    void update(f32 deltaSeconds);

    /**
     * @brief Records the tree into @p out, if anything changed since the last
     *        call.
     *
     * @return True when @p out was re-recorded. False means the previous
     *         contents still stand, which lets a backend re-submit the vertex
     *         buffer it already built instead of rebuilding it.
     */
    bool paint(DrawList& out);

    [[nodiscard]] bool needsPaint() const noexcept { return _paintDirty || _layoutDirty; }

    /// @}

    /// @name Pointer input
    /// @{

    /// @return True when the UI consumed the event and the application should
    ///         not also act on it.
    bool pointerMoved(glm::vec2 position);
    bool pointerDown(glm::vec2 position, PointerButton button = PointerButton::Left,
                     Modifiers mods = {});
    bool pointerUp(glm::vec2 position, PointerButton button = PointerButton::Left,
                   Modifiers mods = {});
    bool wheel(glm::vec2 delta, glm::vec2 position, Modifiers mods = {});

    /// The cursor left the surface: drops the hover chain. A drag in progress
    /// keeps its capture, so dragging out of the window and back still works.
    void pointerLeft();

    /// @}

    /// @name Keyboard input
    /// @{

    bool keyDown(wma::Key key, Modifiers mods = {}, bool repeat = false);
    bool keyUp(wma::Key key, Modifiers mods = {});
    bool textInput(std::string_view utf8);

    /// @}

    /// @name Focus and pointer state
    /// @{

    void setFocus(Widget* widget, FocusReason reason = FocusReason::Programmatic);
    void clearFocus();

    [[nodiscard]] Widget* focused() const noexcept { return _focused; }

    /// Moves focus to the next (or previous) focusable widget in tree order,
    /// wrapping around. What Tab and Shift+Tab do.
    bool focusNext();
    bool focusPrevious();

    /// Deepest widget under the cursor. The hover *chain* reaches from here to
    /// the root, so an ancestor can style itself on a descendant's hover.
    [[nodiscard]] Widget* hovered() const noexcept;
    [[nodiscard]] bool isHovered(const Widget* widget) const noexcept;

    void capturePointer(Widget* widget);
    [[nodiscard]] Widget* pointerCapture() const noexcept { return _capture; }

    /// The widget at @p position, ignoring anything not hit-test visible.
    [[nodiscard]] Widget* widgetAt(glm::vec2 position) const;

    /// @{
    /// What an application gates its own input on: true means the UI is using
    /// that device and the application should not.
    [[nodiscard]] bool capturesKeyboard() const noexcept;
    [[nodiscard]] bool capturesTextInput() const noexcept;
    [[nodiscard]] bool capturesPointer() const noexcept;
    /// @}

    /// @}

    /// @name Shortcuts
    /// @{

    /// @return An id for removeShortcut().
    u64 addShortcut(Shortcut shortcut, std::function<void()> action);
    void removeShortcut(u64 id);

    /// @}

    /// @name Theme and text
    /// @{

    /// Editable theme. Every widget resolves its style per paint, so an edit
    /// here shows up on the next frame with nothing to invalidate by hand.
    [[nodiscard]] Theme& theme() noexcept
    {
        _requestPaint();
        return _theme;
    }

    [[nodiscard]] const Theme& theme() const noexcept { return _theme; }
    [[nodiscard]] ITextShaper& shaper() const noexcept { return *_shaper; }

    /// @}

    /// @{
    /// How long the pointer must rest on a widget before its tooltip opens.
    void setTooltipDelay(f32 seconds) noexcept { _tooltipDelay = std::max(0.0f, seconds); }
    [[nodiscard]] f32 tooltipDelay() const noexcept { return _tooltipDelay; }
    /// @}

    /// Snapshot of the whole tree for an accessibility bridge, or a test.
    [[nodiscard]] AccessibilityNode accessibilityTree() const;

    /// Fires whenever focus moves, with the widget that gained it (or null).
    Signal<Widget*> focusChanged;

private:
    friend class Widget;

    struct ShortcutEntry {
        u64 id = 0;
        Shortcut shortcut{};
        std::function<void()> action;
    };

    void _adopt(std::unique_ptr<Widget> content);

    /// Clears every pointer this root holds into @p widget or its subtree.
    /// Called when a widget is detached, destroyed or disabled.
    void _forget(Widget& widget);

    void _requestLayout() noexcept { _layoutDirty = true; }
    void _requestPaint() noexcept { _paintDirty = true; }
    void _setAnimating(Widget& widget, bool animating);

    void _layout();

    /// Opens or closes the hovered widget's tooltip as the pointer dwells and
    /// moves. Driven from update(), because a tooltip is a function of time.
    void _updateTooltip(f32 deltaSeconds);

    void _closeTooltip();

    /// Nearest widget at or above @p node carrying tooltip text.
    [[nodiscard]] static Widget* _tooltipOwner(Widget* node) noexcept;

    /// Walks from @p target up the parent chain until a handler consumes the
    /// event, refilling the event's local coordinates at each step.
    template <class Event, class Handler>
    bool _bubble(Widget* target, Event& event, Handler&& handler);

    void _updateHover(glm::vec2 position);
    void _dropHover();

    [[nodiscard]] bool _runShortcuts(const KeyEvent& event, bool beforeWidget);

    /// Focusable widgets in tree order. Rebuilt per traversal rather than
    /// cached: Tab is a human-speed event, and a cache would have to be
    /// invalidated by every structural change in the tree.
    void _collectFocusable(Widget& node, std::vector<Widget*>& out) const;

    bool _moveFocus(int direction);

    [[nodiscard]] static bool _isAncestorOf(const Widget& ancestor, const Widget* node) noexcept;

    ITextShaper* _shaper = nullptr;
    Theme _theme{};

    std::unique_ptr<Widget> _content;

    //! Always present, so nothing has to null-check the floating layer.
    std::unique_ptr<OverlayLayer> _overlay;

    glm::vec2 _size{0.0f};
    f32 _scale = 1.0f;

    bool _layoutDirty = true;
    bool _paintDirty = true;

    Widget* _focused = nullptr;
    Widget* _capture = nullptr;

    //! Deepest-last, so back() is the hovered widget and the rest are its
    //! ancestors. Kept as a chain so enter/leave fire once per widget rather
    //! than once per pointer move.
    std::vector<Widget*> _hoverChain;
    std::vector<Widget*> _hoverScratch;

    std::vector<Widget*> _animating;
    std::vector<Widget*> _tickScratch;
    std::vector<Widget*> _focusScratch;

    std::vector<ShortcutEntry> _shortcuts;
    u64 _nextShortcutId = 1;

    glm::vec2 _pointer{0.0f};
    bool _pointerInside = false;

    //! Tooltip state: what the pointer is resting on, for how long, and the
    //! overlay currently showing for it.
    Widget* _tooltipTarget = nullptr;
    f32 _tooltipDwell = 0.0f;
    f32 _tooltipDelay = 0.5f;
    OverlayLayer::Id _tooltipOverlay = OverlayLayer::kNone;

    //! Double-click detection: a second press close enough in time and space
    //! to the previous one.
    f32 _time = 0.0f;
    f32 _lastPressTime = -1.0f;
    glm::vec2 _lastPressPosition{0.0f};
    u32 _clickCount = 0;
};

} // namespace aura3d::ui

#endif // AURA_UI_UIROOT_H
