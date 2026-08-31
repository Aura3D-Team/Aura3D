#include "aura/UI/Style.h"

namespace aura3d::ui {

namespace {

//! @p color with its alpha scaled, for the tints a palette derives from the
//! colours it was given rather than carrying separately.
[[nodiscard]] glm::vec4 fade(const glm::vec4& color, float alpha)
{
    return {color.r, color.g, color.b, color.a * alpha};
}

//! Corner radius shared by the controls. One value, so a theme reads as one
//! shape language rather than a dozen unrelated decisions.
constexpr float kControlRounding = 3.0f;

//! Anything past half the shorter side is clamped to a pill, so this is just
//! "as round as it goes" -- what makes a radio button a circle.
constexpr float kPill = 1000.0f;

} // namespace

WidgetStyle Style::over(const WidgetStyle& base) const
{
    WidgetStyle result = base;

    //! Field by field rather than anything clever: eleven optionals, and the
    //! only thing that could go wrong is forgetting one, which reads plainly
    //! here and would not in a loop over pointers-to-member.
    if (surface)     result.surface = *surface;
    if (border)      result.border = *border;
    if (text)        result.text = *text;
    if (accent)      result.accent = *accent;
    if (rounding)    result.rounding = *rounding;
    if (borderWidth) result.borderWidth = *borderWidth;
    if (padding)     result.padding = *padding;
    if (align)       result.align = *align;
    if (height)      result.height = *height;
    if (markScale)   result.markScale = *markScale;
    if (markInset)   result.markInset = *markInset;

    return result;
}

Palette Palette::dark()
{
    return Palette{};
}

Palette Palette::light()
{
    Palette palette;
    palette.window = {0.94f, 0.94f, 0.96f, 0.97f};
    palette.titleBar = {0.85f, 0.86f, 0.89f, 1.00f};
    palette.surface = {0.88f, 0.89f, 0.92f, 1.00f};
    palette.surfaceHovered = {0.80f, 0.82f, 0.86f, 1.00f};
    palette.surfaceActive = {0.36f, 0.58f, 0.88f, 1.00f};
    palette.accent = {0.18f, 0.45f, 0.80f, 1.00f};
    palette.text = {0.10f, 0.11f, 0.13f, 1.00f};
    palette.border = {0.00f, 0.00f, 0.00f, 0.16f};
    palette.overlay = {0.99f, 0.99f, 1.00f, 0.98f};
    return palette;
}

Theme::Theme() : Theme(Palette::dark()) {}

Theme::Theme(const Palette& palette)
{
    applyPalette(palette);
}

/*
 * Every Part starts as the same control -- a filled, slightly rounded
 * rectangle that lights up under the pointer -- and then says only how it
 * differs. That is what keeps a palette swap coherent: a Part nobody has
 * thought about still looks like it belongs, and the lines below are exactly
 * the decisions that make a slider not a button.
 */
Theme& Theme::applyPalette(const Palette& palette)
{
    _palette = palette;

    WidgetStyle control{};
    control.surface = {palette.surface, palette.surfaceHovered, palette.surfaceActive};
    control.border = ColorSet::flat(palette.border);
    control.text = palette.text;
    control.accent = palette.accent;
    control.rounding = kControlRounding;
    control.padding = 6.0f;
    control.align = Align::Left;

    _parts.fill(control);

    const auto part = [this](Part which) -> WidgetStyle& { return (*this)[which]; };

    //! Surfaces with no interaction of their own: they never change colour, so
    //! a flat ColorSet says so once instead of three identical lines.
    //! A panel and its bar are square by default: with no antialiasing, a
    //! rounded corner on a large translucent surface reads as a stair rather
    //! than a curve. Both honour `rounding` if a theme asks for it.
    part(Part::Panel).surface = ColorSet::flat(palette.window);
    part(Part::Panel).rounding = 0.0f;
    part(Part::Panel).padding = 0.0f;

    part(Part::TitleBar).surface = ColorSet::flat(palette.titleBar);
    part(Part::TitleBar).rounding = 0.0f;
    part(Part::TitleBar).align = Align::Center;

    part(Part::Label).surface = {};
    part(Part::Label).padding = 0.0f;

    part(Part::Button).align = Align::Center;

    //! The row itself is bare: what a checkbox draws is the tick box, sized
    //! from markScale, and the label beside it.
    part(Part::Checkbox).surface = {palette.surface, palette.surfaceHovered,
                                    palette.surfaceHovered};
    part(Part::Checkbox).padding = 8.0f;

    part(Part::Radio) = part(Part::Checkbox);
    part(Part::Radio).rounding = kPill;
    part(Part::Radio).markScale = 0.64f;
    part(Part::Radio).markInset = 0.30f;

    //! A slider's track never takes the active colour: the accent fill over it
    //! is what shows the value, and two blues fighting reads as neither.
    part(Part::Slider).surface = {palette.surface, palette.surfaceHovered, palette.surface};
    part(Part::Slider).align = Align::Center;

    part(Part::TextField).surface = {palette.surface, palette.surfaceHovered, palette.surface};
    part(Part::TextField).border = {palette.border, palette.border, palette.accent};
    part(Part::TextField).borderWidth = 1.0f;
    part(Part::TextField).padding = 4.0f;

    part(Part::Dropdown).padding = 4.0f;

    part(Part::DropdownItem).surface = {glm::vec4{0.0f}, palette.surfaceHovered,
                                        palette.surfaceActive};
    part(Part::DropdownItem).rounding = 0.0f;
    part(Part::DropdownItem).padding = 4.0f;

    part(Part::Header).surface = {palette.titleBar, palette.surfaceHovered,
                                 palette.surfaceHovered};

    //! Rows in a list: transparent until pointed at, or they turn a scrolling
    //! list into a wall of rectangles.
    part(Part::TreeNode).surface = {glm::vec4{0.0f}, palette.surfaceHovered,
                                    palette.surfaceHovered};
    part(Part::TreeNode).padding = 2.0f;

    part(Part::Selectable).surface = {glm::vec4{0.0f}, palette.surfaceHovered,
                                      palette.surfaceActive};
    part(Part::Selectable).padding = 4.0f;

    part(Part::Tab).align = Align::Center;
    part(Part::Tab).padding = 8.0f;

    part(Part::ScrollView).surface = ColorSet::flat(glm::vec4{0.0f, 0.0f, 0.0f, 0.25f});
    part(Part::ScrollView).rounding = 0.0f;

    part(Part::ScrollTrack).surface = ColorSet::flat(fade(palette.border, 0.5f));
    part(Part::ScrollTrack).rounding = 0.0f;

    part(Part::ScrollThumb).rounding = kPill;

    part(Part::Tooltip).surface = ColorSet::flat(palette.overlay);
    part(Part::Tooltip).border = ColorSet::flat(palette.border);
    part(Part::Tooltip).borderWidth = 1.0f;
    part(Part::Tooltip).padding = 4.0f;

    part(Part::Separator).surface = ColorSet::flat(palette.border);
    part(Part::Separator).rounding = 0.0f;

    return *this;
}

} // namespace aura3d::ui
