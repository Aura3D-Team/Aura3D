#ifndef AURA_UI_EVENT_H
#define AURA_UI_EVENT_H

#pragma once

#include <string_view>

#include <glm/glm.hpp>
#include <ink/ink_base.hpp>
#include <wma/wma.hpp>

#include "aura/UI/Core/Geometry.h"

/**
 * @file Event.h
 * @brief What the widget tree receives from the platform.
 *
 * Key codes and modifiers are wma's own types rather than a re-declared copy:
 * a translation table between two identical enums is a source of drift and
 * buys nothing, since every Aura3D surface is a wma surface.
 *
 * Every handler returns @c bool -- true means "handled, stop here", which is
 * what ends the bubble up the parent chain. There is no separate accept flag
 * to forget to set.
 */

namespace aura3d::ui {

using Modifiers = wma::KeyModifiers;

enum class PointerButton : u8 { Left, Right, Middle };

/// Why a widget gained focus. A field selects all its text when tabbed into
/// but not when clicked, which is the whole reason this is carried.
enum class FocusReason : u8 { Pointer, Keyboard, Programmatic };

/**
 * @brief A mouse or touch event, in both coordinate systems a handler needs.
 *
 * @c position is in surface pixels (what hit-testing and drag tracking use);
 * @c local is relative to the receiving widget's top-left, refilled at each
 * step of the bubble so a parent sees it in *its* own space.
 */
struct PointerEvent {
    glm::vec2 position{0.0f};
    glm::vec2 local{0.0f};
    glm::vec2 delta{0.0f}; //! Movement since the previous pointer event.

    PointerButton button = PointerButton::Left;
    Modifiers mods{};

    /// 1 for a single click, 2 for the second of a double click, and so on.
    /// Meaningful on press events only.
    u32 clickCount = 1;
};

struct WheelEvent {
    glm::vec2 position{0.0f};
    glm::vec2 local{0.0f};

    /// Notches, positive scrolling content towards the end (down / right).
    glm::vec2 delta{0.0f};

    Modifiers mods{};
};

struct KeyEvent {
    wma::Key key = wma::KEY_UNKNOWN;
    Modifiers mods{};

    /// True for an auto-repeat. An editing action honours it; a command
    /// (Ctrl+S) should not.
    bool repeat = false;
};

/// Committed text, UTF-8. Comes from the platform's text input, so it is
/// already correct through dead keys, IME and keyboard layout.
struct TextEvent {
    std::string_view text;
};

/**
 * @brief A key combination bound at the surface level rather than to a widget.
 *
 * A combination holding Ctrl, Alt or Super is checked *before* the focused
 * widget, so Ctrl+S saves while a text field has focus. One without is checked
 * only after the widget declines, so a bare Delete still edits text.
 */
struct Shortcut {
    wma::Key key = wma::KEY_UNKNOWN;
    bool ctrl = false;
    bool shift = false;
    bool alt = false;
    bool super = false;

    [[nodiscard]] static constexpr Shortcut withCtrl(wma::Key key, bool shift = false) noexcept
    {
        return {key, true, shift, false, false};
    }

    [[nodiscard]] constexpr bool matches(const KeyEvent& event) const noexcept
    {
        return event.key == key && event.mods.ctrl == ctrl && event.mods.shift == shift &&
               event.mods.alt == alt && event.mods.super == super;
    }
};

} // namespace aura3d::ui

#endif // AURA_UI_EVENT_H
