#ifndef AURA_UI_ACCESSIBILITY_H
#define AURA_UI_ACCESSIBILITY_H

#pragma once

#include <string>
#include <vector>

#include <ink/ink_base.hpp>

#include "aura/UI/Core/Geometry.h"

/**
 * @file Accessibility.h
 * @brief What a widget tells an assistive technology about itself.
 *
 * Retrofitting this is what makes a UI toolkit permanently unusable for anyone
 * relying on a screen reader, so every widget fills it in from the start --
 * Widget::accessibility() is a virtual with a sensible default, not an opt-in.
 *
 * Nothing here talks to AT-SPI, UI Automation or NSAccessibility. @ref
 * AccessibilityNode is a platform-independent snapshot of the tree; a bridge
 * consumes it, and so does a test, which is the other reason to model it as
 * data.
 */

namespace aura3d::ui
{

/// What kind of thing a widget is, semantically. Not what it looks like: a
/// clickable card is a Button.
enum class Role : u8
{
    None, //! A visual detail with no meaning of its own -- skipped by a bridge.
    Group,
    Window,
    Label,
    Button,
    CheckBox,
    RadioButton,
    Slider,
    ProgressBar,
    TextField,
    Image,
    ScrollView,
    List,
    ListItem,
    Separator,
};

/// A widget's states as a screen reader sees them. Checked, Selected, Expanded and ReadOnly mean something only
/// for the roles that have the state.
enum class AccessibilityState : u16
{
    None = 0,
    Enabled = 1u << 0u,
    Focusable = 1u << 1u,
    Focused = 1u << 2u,
    /// Everything outside this node is out of reach while it is open: a modal dialog. A bridge hides the rest.
    Modal = 1u << 3u,
    Checked = 1u << 4u,
    Selected = 1u << 5u,
    Expanded = 1u << 6u,
    ReadOnly = 1u << 7u,
};
INK_ENUM_FLAGS(AccessibilityState) // NOLINT(clang-analyzer-optin.core.EnumCastOutOfRange)

/// Everything one widget exposes. Filled by Widget::accessibility().
struct AccessibilityInfo
{
    Role role = Role::None;

    /// What a screen reader reads out. Defaults to the widget's own text; set
    /// it explicitly for anything whose label is an icon or a shape.
    std::string name;

    /// Longer explanation, read after @c name -- a tooltip's content, usually.
    std::string description;

    AccessibilityState state = AccessibilityState::Enabled;

    /// Slider and ProgressBar.
    f32 value = 0.0f;
    f32 minValue = 0.0f;
    f32 maxValue = 0.0f;

    [[nodiscard]] constexpr bool has(AccessibilityState flag) const noexcept
    {
        return INK_FLAG_CHECK(state, flag);
    }
    constexpr void set(AccessibilityState flag, bool on) noexcept
    {
        state = on ? state | flag : state & ~flag;
    }
};

/// One widget's info plus where it is, as a tree. What UIRoot hands a bridge
/// or a test.
struct AccessibilityNode
{
    AccessibilityInfo info{};
    Rect bounds{};
    std::vector<AccessibilityNode> children;
};

} // namespace aura3d::ui

#endif // AURA_UI_ACCESSIBILITY_H
