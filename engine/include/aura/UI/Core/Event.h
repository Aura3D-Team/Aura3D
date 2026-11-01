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

namespace aura3d::ui
{

using Modifiers = wma::KeyModifiers;

enum class PointerButton : u8
{
    Left,
    Right,
    Middle
};

/// What produced a pointer event. A finger is the same stream of presses and moves as a mouse, but
/// it has no hover and it scrolls: a widget that cares reads this.
enum class PointerKind : u8
{
    Mouse,
    Touch
};

/// Why a widget gained focus. A field selects all its text when tabbed into
/// but not when clicked, which is the whole reason this is carried.
enum class FocusReason : u8
{
    Pointer,
    Keyboard,
    Programmatic
};

/**
 * @brief A mouse or touch event, in both coordinate systems a handler needs.
 *
 * @c position is in surface pixels (what hit-testing and drag tracking use);
 * @c local is relative to the receiving widget's top-left, refilled at each
 * step of the bubble so a parent sees it in *its* own space.
 */
struct PointerEvent
{
    glm::vec2 position{0.0f};
    glm::vec2 local{0.0f};
    glm::vec2 delta{0.0f}; //! Movement since the previous pointer event.

    PointerButton button = PointerButton::Left;
    Modifiers mods{};

    /// 1 for a single click, 2 for the second of a double click, and so on.
    /// Meaningful on press events only.
    u32 clickCount = 1;

    PointerKind kind = PointerKind::Mouse;

    /// Where the press in progress began, in surface pixels. Meaningful while a button is down.
    glm::vec2 pressPosition{0.0f};
};

struct WheelEvent
{
    glm::vec2 position{0.0f};
    glm::vec2 local{0.0f};

    /// Notches, positive scrolling content towards the end (down / right).
    glm::vec2 delta{0.0f};

    Modifiers mods{};
};

struct KeyEvent
{
    wma::Key key = wma::KEY_UNKNOWN;
    Modifiers mods{};

    /// True for an auto-repeat. An editing action honours it; a command
    /// (Ctrl+S) should not.
    bool repeat = false;
};

/// Committed text, UTF-8. Comes from the platform's text input, so it is
/// already correct through dead keys, IME and keyboard layout.
struct TextEvent
{
    std::string_view text;
};

/**
 * @brief A key combination bound at the surface level rather than to a widget.
 *
 * A combination holding Ctrl, Alt or Super is checked *before* the focused
 * widget, so Ctrl+S saves while a text field has focus. One without is checked
 * only after the widget declines, so a bare Delete still edits text.
 */
struct Shortcut
{
    /// The modifiers a combination holds; Super is Windows or Command.
    enum class Mods : u8
    {
        None = 0,
        Ctrl = 1u << 0u,
        Shift = 1u << 1u,
        Alt = 1u << 2u,
        Super = 1u << 3u,
    };

    wma::Key key = wma::KEY_UNKNOWN;
    Mods mods = Mods::None;

    [[nodiscard]] static constexpr Mods modsOf(const wma::KeyModifiers &held) noexcept
    {
        // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange): Mods is a flag set.
        return static_cast<Mods>((held.ctrl ? 1u << 0u : 0u) | (held.shift ? 1u << 1u : 0u) |
                                 (held.alt ? 1u << 2u : 0u) | (held.super ? 1u << 3u : 0u));
    }

    [[nodiscard]] static constexpr Shortcut withCtrl(wma::Key key, bool shift = false) noexcept
    {
        // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange): Mods is a flag set.
        return {key, static_cast<Mods>(shift ? (1u << 0u) | (1u << 1u) : 1u << 0u)};
    }

    [[nodiscard]] constexpr bool matches(const KeyEvent &event) const noexcept
    {
        return event.key == key && modsOf(event.mods) == mods;
    }
};
INK_ENUM_FLAGS(Shortcut::Mods) // NOLINT(clang-analyzer-optin.core.EnumCastOutOfRange)

} // namespace aura3d::ui

#endif // AURA_UI_EVENT_H
