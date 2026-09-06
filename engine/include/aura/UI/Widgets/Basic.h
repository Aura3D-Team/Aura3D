#ifndef AURA_UI_WIDGETS_BASIC_H
#define AURA_UI_WIDGETS_BASIC_H

#pragma once

#include <string>

#include "aura/UI/Core/Property.h"
#include "aura/UI/Text/TextEngine.h"
#include "aura/UI/Widget.h"

/**
 * @file Basic.h
 * @brief The widgets that only display: text, images, rules.
 */

namespace aura3d::ui {

/**
 * @class Label
 * @brief A run of text, shaped once and re-shaped only when something it
 *        depends on changes.
 *
 * @code
 * auto& title = column.add<Label>("AuraShell");
 * title.setFontSize(32.0f);
 * title.setAlign(Align::Center);
 * @endcode
 *
 * With TextWrap::Word the label wraps to the width it is given, so its height
 * depends on its width -- which is exactly why layout has a measure pass.
 */
class Label : public Widget {
public:
    explicit Label(std::string text = {});

    Property<std::string> text;

    /// Em size in logical pixels. Zero (the default) takes
    /// Metrics::fontSize from the theme, so a whole UI rescales from one place.
    void setFontSize(f32 pixels);
    [[nodiscard]] f32 fontSize() const noexcept { return _textStyle.pixelSize; }

    void setWrap(TextWrap wrap);
    void setAlign(Align align);
    void setLineSpacing(f32 multiple);

    void setTextStyle(const TextStyle& style);
    [[nodiscard]] const TextStyle& textStyle() const noexcept { return _textStyle; }

    /// The laid-out glyphs. Valid after the frame's measure pass; used by
    /// anything that needs to point at a character, and by tests.
    [[nodiscard]] const ShapedText& shaped() const noexcept { return _shaped; }

    void accessibility(AccessibilityInfo& out) const override;

protected:
    glm::vec2 measureContent(const Constraints& available) override;
    void paint(DrawList& out) override;

    /// Re-shapes if the string, the style or the wrap width changed since the
    /// last call, and does nothing otherwise -- this runs every measure pass.
    void shapeText(f32 maxWidth);

    /// Where the shaped block sits inside @p content, honouring the style's
    /// horizontal alignment and centring vertically.
    [[nodiscard]] glm::vec2 textOrigin(const Rect& content) const noexcept;

    TextStyle _textStyle{};
    ShapedText _shaped;

private:
    /// What the cached shaping was produced from. A miss on any of the three
    /// is the only thing that re-runs the shaper.
    TextStyle _shapedStyle{};
    f32 _shapedWidth = -1.0f;
    bool _textDirty = true;
    void onAttach() override { _textDirty = true; }
};

/**
 * @class Image
 * @brief A texture, scaled into the widget's rectangle.
 *
 * The engine owns the texture; this only says where to draw it. Loading is
 * IRenderer's job, and keeping it that way is what lets an Image be created
 * before a renderer exists.
 */
class Image final : public Widget {
public:
    /// How the texture fills a rectangle that is not its own shape.
    enum class Fit : u8 {
        Stretch, //! Fills the box, distorting.
        Contain, //! Fits inside, letterboxed.
        Cover,   //! Fills the box, cropping the overflow.
        None,    //! Native size, centred.
    };

    explicit Image(TextureHandle texture = {}, glm::vec2 nativeSize = {});

    void setTexture(TextureHandle texture, glm::vec2 nativeSize);
    void setFit(Fit fit);
    void setTint(const glm::vec4& tint);

    /// Rounds the image's corners. Applied to the quad, so it also crops.
    void setRadius(Corners radius);

    void accessibility(AccessibilityInfo& out) const override;

protected:
    glm::vec2 measureContent(const Constraints& available) override;
    void paint(DrawList& out) override;

private:
    /// The destination rectangle @ref Fit puts the texture in.
    [[nodiscard]] Rect _destination(const Rect& content) const noexcept;

    TextureHandle _texture{};
    glm::vec2 _native{0.0f};
    glm::vec4 _tint{1.0f};
    Corners _radius{};
    Fit _fit = Fit::Contain;
};

/// A hairline rule across the container it sits in.
class Separator final : public Widget {
public:
    explicit Separator(Axis axis = Axis::Horizontal);

    void accessibility(AccessibilityInfo& out) const override;

protected:
    glm::vec2 measureContent(const Constraints& available) override;

private:
    Axis _axis = Axis::Horizontal;
};

} // namespace aura3d::ui

#endif // AURA_UI_WIDGETS_BASIC_H
