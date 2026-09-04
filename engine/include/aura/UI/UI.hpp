#ifndef AURA_UI_HPP
#define AURA_UI_HPP

#pragma once

/**
 * @file UI.hpp
 * @brief AuraUI in one include.
 *
 * @code
 * #include <aura/UI/UI.hpp>
 * using namespace aura3d::ui;
 * @endcode
 *
 * The layers, bottom up:
 *
 * | Layer     | Header                        | What it owns                          |
 * |-----------|-------------------------------|---------------------------------------|
 * | Core      | Geometry, Signal, Property    | Values, events, observable state      |
 * |           | Event, DrawList, Theme        | Input types, render commands, styling |
 * |           | Animation, Accessibility      | Easing, the semantic tree             |
 * | Text      | Text/TextEngine, Text/Utf8    | Shaping, wrapping, carets             |
 * | Layout    | Layout/Layout                 | Length, Constraints, measure/arrange  |
 * | Widgets   | Widget, Widgets/Basic ...     | The tree, and what is in it           |
 * | Overlay   | Overlay                       | Popups, menus, tooltips, dialogs      |
 * | Root      | UIRoot                        | Dispatch, focus, the frame            |
 * | Backend   | Backend/DrawListRenderer      | DrawList to draw calls                |
 * | Platform  | UIView                        | Renderer and window, wired up         |
 *
 * Nothing below the Backend row knows a renderer exists, and nothing below
 * UIView knows a window does.
 */

#include "aura/UI/Core/Accessibility.h"
#include "aura/UI/Core/Animation.h"
#include "aura/UI/Core/DrawList.h"
#include "aura/UI/Core/Event.h"
#include "aura/UI/Core/Geometry.h"
#include "aura/UI/Core/Icon.h"
#include "aura/UI/Core/Property.h"
#include "aura/UI/Core/Signal.h"
#include "aura/UI/Core/Theme.h"
#include "aura/UI/Layout/Layout.h"
#include "aura/UI/Overlay.h"
#include "aura/UI/Text/TextEngine.h"
#include "aura/UI/Text/Utf8.h"
#include "aura/UI/UIRoot.h"
#include "aura/UI/UIView.h"
#include "aura/UI/Widget.h"
#include "aura/UI/Widgets/Basic.h"
#include "aura/UI/Widgets/Controls.h"
#include "aura/UI/Widgets/Layouts.h"
#include "aura/UI/Widgets/Menus.h"
#include "aura/UI/Widgets/Navigation.h"

#endif // AURA_UI_HPP
