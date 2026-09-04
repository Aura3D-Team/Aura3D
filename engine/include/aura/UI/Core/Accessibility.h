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

namespace aura3d::ui {

/// What kind of thing a widget is, semantically. Not what it looks like: a
/// clickable card is a Button.
enum class Role : u8 {
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

/// Everything one widget exposes. Filled by Widget::accessibility().
struct AccessibilityInfo {
    Role role = Role::None;

    /// What a screen reader reads out. Defaults to the widget's own text; set
    /// it explicitly for anything whose label is an icon or a shape.
    std::string name;

    /// Longer explanation, read after @c name -- a tooltip's content, usually.
    std::string description;

    bool enabled = true;
    bool focusable = false;
    bool focused = false;

    /// @{
    /// Meaningful for the roles that have the state; ignored otherwise.
    bool checked = false;
    bool selected = false;
    bool expanded = false;
    bool readOnly = false;
    /// @}

    /// Slider and ProgressBar.
    f32 value = 0.0f;
    f32 minValue = 0.0f;
    f32 maxValue = 0.0f;
};

/// One widget's info plus where it is, as a tree. What UIRoot hands a bridge
/// or a test.
struct AccessibilityNode {
    AccessibilityInfo info{};
    Rect bounds{};
    std::vector<AccessibilityNode> children;
};

} // namespace aura3d::ui

#endif // AURA_UI_ACCESSIBILITY_H
