#ifndef AURA_UI_STYLE_H
#define AURA_UI_STYLE_H

#pragma once

#include <array>
#include <optional>

#include <glm/glm.hpp>
#include <ink/ink_base.hpp>

/**
 * @file Style.h
 * @brief Per-component theming for aura3d::ui.
 *
 * A @ref Theme holds one @ref WidgetStyle per @ref Part, so buttons, sliders
 * and tabs are styled independently instead of sharing one palette of
 * colours. Nothing a widget draws is hard-coded: colour, corner radius,
 * border, padding, alignment and height all come from its Part's style.
 *
 * Three scopes, widest first:
 *
 * @code
 * gui.theme.applyPalette(ui::Palette::light()); // the whole UI
 * gui.theme[ui::Part::Button].rounding = 8.0f;  // every button, from now on
 * ui::StyleGuard scope(gui, ui::Part::Button, ui::Style{}.rounded(2.0f)); // this scope
 * gui.button("Delete", ui::Style{}.fill(red));  // this one widget
 * @endcode
 */

namespace aura3d::ui {

/// Horizontal placement of a component's text within its rectangle.
enum class Align : u8 { Left, Center, Right };

/**
 * @brief Every component a @ref Theme styles separately.
 *
 * One entry per *visual* component, not per API call: inputFloat() is a
 * TextField, and a scroll region's three surfaces are separable because a
 * theme routinely wants the viewport dark and the thumb bright.
 */
enum class Part : u8 {
    Panel,
    TitleBar,
    Label,
    Button,
    Checkbox,
    Radio,
    Slider,
    TextField,
    Dropdown,
    DropdownItem,
    Header,
    TreeNode,
    Selectable,
    Tab,
    ScrollView,
    ScrollTrack,
    ScrollThumb,
    Tooltip,
    Separator,

    Count
};

/**
 * @brief The three colours one surface takes as the pointer works on it.
 *
 * Which of the three a widget calls "active" is the widget's business: a
 * button's is held, a text field's is focused, a header's is open.
 */
struct ColorSet {
    glm::vec4 normal{0.0f};
    glm::vec4 hovered{0.0f};
    glm::vec4 active{0.0f};

    [[nodiscard]] constexpr const glm::vec4& pick(bool isActive, bool isHovered) const noexcept
    {
        return isActive ? active : isHovered ? hovered : normal;
    }

    /// One colour in all three states, for surfaces that don't react.
    [[nodiscard]] static ColorSet flat(const glm::vec4& color) { return {color, color, color}; }
};

/**
 * @brief How one component looks.
 *
 * Defaults describe an invisible component -- fully transparent, square,
 * unbordered -- so a Part left untouched by a palette draws nothing rather
 * than something arbitrary.
 */
struct WidgetStyle {
    ColorSet surface{}; //! Background fill, by interaction state.
    ColorSet border{};  //! Outline colour, by interaction state.

    glm::vec4 text{1.0f};

    //! Slider fill, tick mark, caret, selection highlight and focus ring --
    //! everything the component draws *over* its surface to say "this one".
    glm::vec4 accent{1.0f};

    float rounding = 0.0f;    //! Corner radius in pixels; anything past half the
                              //! shorter side gives a pill (or a circle).
    float borderWidth = 0.0f; //! Outline thickness; 0 draws no outline.
    float padding = 6.0f;     //! Text inset from the component's own edges.

    Align align = Align::Left;

    //! Row height, for a component that is not Metrics::rowHeight tall.
    std::optional<float> height{};

    //! Checkbox and radio only: the tick box's side as a fraction of the row
    //! height, and its mark's inset as a fraction of the box. The two are the
    //! whole visual difference between a check box and a radio dot.
    float markScale = 0.73f;
    float markInset = 0.25f;
};

/**
 * @brief A sparse override of a @ref WidgetStyle: only the fields it sets.
 *
 * Every field left unset falls through to the Part's entry in the @ref Theme.
 * That fall-through is the whole point -- it is what lets one call restyle one
 * thing without restating the other ten, and what stops a partial style from
 * drawing an *invisible* widget the way a bare WidgetStyle (transparent by
 * default) would.
 *
 * @code
 * gui.button("Delete", ui::Style{}.fill({0.66f, 0.20f, 0.22f, 1.0f}));
 * gui.label("Heading", ui::Style{}.alignText(ui::Align::Center));
 * @endcode
 *
 * The setters chain and return @c *this, so a temporary reads as one
 * expression. Assign the fields directly for anything they don't cover.
 */
struct Style {
    std::optional<ColorSet> surface{};
    std::optional<ColorSet> border{};
    std::optional<glm::vec4> text{};
    std::optional<glm::vec4> accent{};
    std::optional<float> rounding{};
    std::optional<float> borderWidth{};
    std::optional<float> padding{};
    std::optional<Align> align{};
    std::optional<float> height{};
    std::optional<float> markScale{};
    std::optional<float> markInset{};

    //! True when nothing is set. Checked per widget, so resolving an unstyled
    //! one costs this and no copy.
    [[nodiscard]] bool empty() const noexcept
    {
        return !surface && !border && !text && !accent && !rounding && !borderWidth &&
               !padding && !align && !height && !markScale && !markInset;
    }

    /// @p base with every field this patch sets replaced.
    [[nodiscard]] WidgetStyle over(const WidgetStyle& base) const;

    /// @{
    /// Chainable setters. `fill` is the surface, `outline` the border.
    Style& fill(const glm::vec4& color) { surface = ColorSet::flat(color); return *this; }
    Style& fill(const ColorSet& colors) { surface = colors; return *this; }

    Style& outline(const glm::vec4& color, float width = 1.0f)
    {
        border = ColorSet::flat(color);
        borderWidth = width;
        return *this;
    }

    Style& textColor(const glm::vec4& color) { text = color; return *this; }
    Style& accentColor(const glm::vec4& color) { accent = color; return *this; }
    Style& rounded(float radius) { rounding = radius; return *this; }
    Style& pad(float inset) { padding = inset; return *this; }
    Style& alignText(Align to) { align = to; return *this; }
    Style& rowHeight(float pixels) { height = pixels; return *this; }

    //! Checkbox and radio only; see WidgetStyle::markScale.
    Style& mark(float scale, float inset)
    {
        markScale = scale;
        markInset = inset;
        return *this;
    }
    /// @}
};

/// Layout every component shares. Pixels, unless said otherwise.
struct Metrics {
    float rowHeight = 22.0f;   //! Height of one widget row, unless its Part overrides it.
    float itemSpacing = 4.0f;  //! Vertical gap between rows.
    float padding = 8.0f;      //! Panel border to content, all four sides.
    float indent = 12.0f;      //! Left inset added by each open treeNode().
    float textScale = 1.0f;    //! Multiplier on the atlas rasterization size.
    float scrollbarWidth = 10.0f;
    float disabledAlpha = 0.40f; //! Alpha multiplier applied inside beginDisabled().
};

/**
 * @brief The handful of colours a whole theme is generated from.
 *
 * Nine colours reach every one of the ~19 Parts through
 * Theme::applyPalette(), which is the difference between restyling the UI and
 * editing a table.
 */
struct Palette {
    glm::vec4 window{0.09f, 0.10f, 0.12f, 0.94f}; //! Panel body.
    glm::vec4 titleBar{0.16f, 0.18f, 0.22f, 1.00f};
    glm::vec4 surface{0.20f, 0.22f, 0.27f, 1.00f}; //! A control at rest.
    glm::vec4 surfaceHovered{0.27f, 0.30f, 0.37f, 1.00f};
    glm::vec4 surfaceActive{0.20f, 0.45f, 0.75f, 1.00f};
    glm::vec4 accent{0.24f, 0.52f, 0.85f, 1.00f};
    glm::vec4 text{0.92f, 0.93f, 0.95f, 1.00f};
    glm::vec4 border{1.00f, 1.00f, 1.00f, 0.12f};
    glm::vec4 overlay{0.05f, 0.05f, 0.07f, 0.96f}; //! Tooltip and open dropdown.

    [[nodiscard]] static Palette dark();
    [[nodiscard]] static Palette light();
};

/**
 * @class Theme
 * @brief One @ref WidgetStyle per @ref Part, plus the @ref Metrics they share.
 *
 * Generated from a @ref Palette and then editable per Part -- the two are not
 * alternatives. applyPalette() lays down a coherent base; the edits after it
 * are the ones that make a component look like itself.
 *
 * @code
 * gui.theme = ui::Theme::light();
 * gui.theme[ui::Part::Button].rounding = 10.0f;
 * gui.theme[ui::Part::Button].height = 30.0f;
 * @endcode
 */
class Theme {
public:
    /// The engine's dark theme.
    Theme();

    explicit Theme(const Palette& palette);

    [[nodiscard]] static Theme dark() { return Theme{Palette::dark()}; }
    [[nodiscard]] static Theme light() { return Theme{Palette::light()}; }

    /// Regenerates every Part from @p palette. Per-Part edits made before this
    /// are lost, which is the point: it re-establishes a coherent base.
    Theme& applyPalette(const Palette& palette);

    [[nodiscard]] WidgetStyle& operator[](Part part) noexcept { return _parts[_index(part)]; }

    [[nodiscard]] const WidgetStyle& operator[](Part part) const noexcept
    {
        return _parts[_index(part)];
    }

    [[nodiscard]] const Palette& palette() const noexcept { return _palette; }

    Metrics metrics{};

private:
    //! Clamped rather than asserted: operator[] is noexcept and on the path of
    //! every widget, and a bad Part is a caller's bug, not a reason to trap.
    [[nodiscard]] static constexpr size_t _index(Part part) noexcept
    {
        const auto raw = static_cast<size_t>(part);
        return raw < static_cast<size_t>(Part::Count) ? raw : 0;
    }

    Palette _palette{};
    std::array<WidgetStyle, static_cast<size_t>(Part::Count)> _parts{};
};

} // namespace aura3d::ui

#endif // AURA_UI_STYLE_H
