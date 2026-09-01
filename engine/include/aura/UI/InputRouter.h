#ifndef AURA_UI_INPUTROUTER_H
#define AURA_UI_INPUTROUTER_H

#pragma once

#include <functional>
#include <vector>

#include <wma/wma.hpp>

#include "aura/UI/AuraUI.h"

/**
 * @file InputRouter.h
 * @brief Routes input between a game's modes (gameplay, menus, ...), so one
 *        mode's bindings never fire while another is active.
 *
 * Without this, wma dispatches every bound key regardless of what the UI is
 * doing, so typing into a text field also fires whatever the same key does in
 * gameplay. InputRouter puts each mode's bindings in their own wma input
 * context and switches which context is active, so a suspended mode's
 * bindings simply aren't dispatched.
 *
 * A "mode" is whatever the game wants: pause menu, inventory, cutscene, photo
 * mode. @ref kGameplay and @ref kMenu are provided as a starting pair; add
 * more with createMode().
 *
 * @code
 * const auto inventory = input.createMode({.cursorVisible = true, .uiEnabled = true});
 *
 * input.bindToggle(wma::KEY_ESCAPE, InputRouter::kMenu);
 * input.bindToggle(wma::KEY_E, inventory);
 * input.bindHeld(wma::KEY_W, moveForward);   // kGameplay by default
 *
 * // per frame:
 * input.newFrame();
 * if (input.isMode(inventory)) { ... }
 * gui.render();
 * @endcode
 *
 * @note Touch also gets a context per mode: wma's touch callbacks replace
 *       rather than append, so without this the UI's tap-to-click and an
 *       app's own gestures would overwrite each other.
 */

namespace aura3d::ui {

/**
 * @class InputRouter
 * @brief Switches which mode's key/mouse/touch bindings wma dispatches.
 *
 * Each mode gets its own wma input context per device; switching mode moves
 * the active context, so exactly one mode's bindings fire at a time. O(1) to
 * switch, regardless of how many modes exist.
 *
 * @note Not copyable or movable: bindings capture @c this.
 */
class InputRouter {
public:
    /// Index into the mode table. Handed out by createMode().
    using ModeId = u32;

    /// Cursor captured, no UI input. The default fallback mode.
    static constexpr ModeId kGameplay = 0;

    /// Cursor free, UI takes input. An ordinary mode, not privileged.
    static constexpr ModeId kMenu = 1;

    /// Properties of one mode.
    struct ModeDesc {
        /// Cursor free and visible in this mode. Ignored on Android.
        bool cursorVisible = true;

        /// Whether the UI receives input in this mode. False still draws the
        /// UI (e.g. a HUD) but it takes no clicks or keystrokes.
        bool uiEnabled = true;
    };

    /**
     * @brief Takes over input routing and @p gui's input.
     *
     * Calls Context::attachInput() itself; don't call it separately. Starts
     * in @ref kGameplay -- call switchTo() first if a different mode should
     * be active from frame one (e.g. a phone, always in @ref kMenu).
     */
    InputRouter(wma::IWindowManager& windowManager, Context& gui);

    /// Withdraws every binding this router installed.
    ~InputRouter();

    InputRouter(const InputRouter&) = delete;
    InputRouter& operator=(const InputRouter&) = delete;
    InputRouter(InputRouter&&) = delete;
    InputRouter& operator=(InputRouter&&) = delete;

    /// Declares a new mode and returns its id.
    [[nodiscard]] ModeId createMode(const ModeDesc& desc);

    /// Mode that switchTo()/bindClose() fall back to. Defaults to @ref kGameplay.
    void setBaseMode(ModeId mode) noexcept;

    /**
     * @brief Makes @p key open @p mode, and close it again if already open.
     *
     * Bound in every mode, so it works from anywhere. Ignored while a text
     * field has focus, so it doesn't fight typing.
     */
    void bindToggle(wma::Key key, ModeId mode);

    /// Makes @p key return to the base mode, only while in @p from.
    void bindClose(wma::Key key, ModeId from = kMenu);

    /**
     * @brief Holds @p flag true while @p key is down, in @p mode only.
     *
     * @p flag is also cleared when @p mode is left, so a key held at the
     * moment of a mode switch doesn't stay stuck down.
     *
     * @param flag Must outlive this object.
     */
    void bindHeld(wma::Key key, bool& flag, ModeId mode = kGameplay);

    /// Runs @p action once per press of @p key, in @p mode only.
    void bindPress(wma::Key key, std::function<void()> action, ModeId mode = kGameplay);

    /// Runs @p action on mouse motion, in @p mode only. For camera look.
    void bindLook(std::function<void(const wma::WMAMousePosition&)> action,
                  ModeId mode = kGameplay);

    /// Switches to @p mode. Takes effect at the next newFrame().
    void switchTo(ModeId mode);

    /// Returns to the base mode.
    void close();

    /// Switches to @p mode, or back to the base mode if already in @p mode.
    void toggle(ModeId mode);

    /// Applies any pending mode switch, then opens @p gui's frame. Call once
    /// per frame, before any widget.
    void newFrame();

    /// The active mode.
    [[nodiscard]] ModeId mode() const noexcept { return _mode; }

    /// True if @p mode is active.
    [[nodiscard]] bool isMode(ModeId mode) const noexcept { return _mode == mode; }

    /// True if the base mode is active.
    [[nodiscard]] bool isBaseMode() const noexcept { return _mode == _base; }

    /// Number of modes declared, including the two built-ins.
    [[nodiscard]] size_t modeCount() const noexcept { return _modes.size(); }

    /**
     * @brief True if gameplay (not the UI) should read the keyboard.
     *
     * bindHeld()/bindPress() already check this; only needed for input read
     * some other way, e.g. a polled isKeyDown(). Always true in a mode with
     * @c uiEnabled false.
     */
    [[nodiscard]] bool gameHasKeyboard() const noexcept;

    /// True if gameplay should read the mouse. What bindLook() checks.
    [[nodiscard]] bool gameHasPointer() const noexcept;

private:
    /// One mode's input contexts, plus the ModeDesc it was created with.
    struct Mode {
        wma::InputContextId keys{};
        wma::InputContextId pointer{};
        wma::InputContextId touch{};
        ModeDesc desc{};

        /// Flags bindHeld() bound in this mode; cleared when it is left.
        std::vector<bool*> heldFlags;
    };

    /// One toggle key, replayed into modes created after it.
    struct Toggle {
        wma::Key key = wma::KEY_UNKNOWN;
        ModeId target = kGameplay;
    };

    /// Switches contexts and clears the departing mode's held flags.
    void _applyMode(ModeId mode);

    /// Registers the key -> target toggle into mode @p in.
    void _bindToggleIn(wma::Key key, ModeId target, ModeId in);

    /// False while a text field has focus.
    [[nodiscard]] bool _menuKeyAvailable() const noexcept;

    /// True if @p mode is a declared mode id.
    [[nodiscard]] bool _valid(ModeId mode) const noexcept { return mode < _modes.size(); }

    wma::IWindowManager* _window;
    Context* _gui;

    std::vector<Mode> _modes; // indexed by ModeId
    std::vector<Toggle> _toggles;

    ModeId _mode = kGameplay;
    ModeId _base = kGameplay;

    ModeId _pending = kGameplay;
    bool _switchPending = false;
};

} // namespace aura3d::ui

#endif // AURA_UI_INPUTROUTER_H
