#ifndef AURA_UI_TEXTENGINE_H
#define AURA_UI_TEXTENGINE_H

#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <glm/glm.hpp>
#include <ink/ink_base.hpp>

#include "aura/UI/Core/Geometry.h"

/**
 * @file TextEngine.h
 * @brief Text as a first-class system: shaping, wrapping and cursor mapping.
 *
 * Layout never touches a font. It asks an @ref ITextShaper for a @ref
 * ShapedText -- positioned glyph quads plus the line structure -- and every
 * higher-level question (how wide is this label, which byte did the user click,
 * where does the caret go) is answered from that one object.
 *
 * @ref AtlasTextShaper is the shaper the engine ships: FontAtlas-backed,
 * kerned, word-wrapping, and correct for the Latin/Greek/Cyrillic range. What
 * it deliberately does not do is *shape* -- no ligatures, no mark positioning,
 * no bidi, no font fallback. Those need HarfBuzz and a font database, and the
 * point of ITextShaper being an interface is that they arrive as a second
 * implementation rather than as a rewrite of every widget.
 */

namespace aura3d {
class FontAtlas;
} // namespace aura3d

namespace aura3d::ui {

enum class TextWrap : u8 {
    None,      //! One line per '\n'; overflow is the caller's problem.
    Word,      //! Break between words, and inside a word too long to fit.
    Character, //! Break anywhere. For code, hashes, paths.
};

/// Everything about how a string is laid out, and nothing about where.
struct TextStyle {
    f32 pixelSize = 14.0f;    //! Em size in logical pixels.
    f32 lineSpacing = 1.25f;  //! Multiple of the font's natural line height.
    f32 letterSpacing = 0.0f; //! Extra advance per glyph, logical pixels.

    TextWrap wrap = TextWrap::None;
    Align align = Align::Left; //! Where each line sits within the block width.

    [[nodiscard]] constexpr bool operator==(const TextStyle&) const noexcept = default;
};

/// One positioned glyph quad, in coordinates local to the text block's
/// top-left corner.
struct ShapedGlyph {
    Rect bounds{};
    glm::vec2 uvMin{0.0f};
    glm::vec2 uvMax{0.0f};

    /// Pen position and advance, block-local. Carried alongside @c bounds
    /// because a caret must land correctly on a space, whose quad is empty and
    /// whose bounds therefore say nothing about where the pen was.
    f32 penX = 0.0f;
    f32 advance = 0.0f;

    /// Byte offset of this glyph's first code unit in the source string. The
    /// hinge every caret, selection and hit test turns on.
    u32 cluster = 0;

    u32 line = 0;
};

/// One line of a shaped block. Glyph ranges index into ShapedText::glyphs.
struct ShapedLine {
    u32 firstGlyph = 0;
    u32 glyphCount = 0;

    u32 byteBegin = 0;
    u32 byteEnd = 0; //! Past the last byte *drawn*: excludes the break itself.

    f32 width = 0.0f;
    f32 top = 0.0f;      //! Line box top, block-local.
    f32 baseline = 0.0f; //! Block-local y of the baseline.
};

/**
 * @class ShapedText
 * @brief The result of laying out one string: quads, lines, and the mapping
 *        between byte offsets and pixels.
 *
 * Widgets keep one of these and re-shape only when the string or the style
 * changes, which is what keeps a scrolling list of labels from re-measuring
 * every frame.
 */
class ShapedText {
public:
    std::vector<ShapedGlyph> glyphs;
    std::vector<ShapedLine> lines;

    glm::vec2 size{0.0f}; //! Widest line by total line height.

    f32 lineHeight = 0.0f;
    f32 ascent = 0.0f;

    /// Glyph raster page these quads' UVs address; see ITextShaper::page().
    u32 page = 0;

    [[nodiscard]] bool empty() const noexcept { return glyphs.empty(); }

    void clear() noexcept;

    /// Byte offset of the caret position nearest @p point (block-local).
    /// Clamped into the string, so a click anywhere lands somewhere valid.
    [[nodiscard]] usize byteAt(glm::vec2 point) const noexcept;

    /// Top-left of the caret that sits *before* @p byte.
    [[nodiscard]] glm::vec2 caretPosition(usize byte) const noexcept;

    /// The caret as a paintable rectangle, @p width pixels wide.
    [[nodiscard]] Rect caretRect(usize byte, f32 width = 1.0f) const noexcept;

    /// Index of the line containing @p byte.
    [[nodiscard]] u32 lineOf(usize byte) const noexcept;

    /// The highlight rectangles covering @c [begin,end) -- one per line the
    /// range touches. Appends; does not clear @p out.
    void selectionRects(usize begin, usize end, std::vector<Rect>& out) const;

    /// Pen x the line's first glyph starts at. Non-zero on a centred or
    /// right-aligned line, which is what an empty line's caret needs.
    [[nodiscard]] f32 lineStartX(u32 lineIndex) const noexcept;
};

/**
 * @class ITextShaper
 * @brief Turns a string plus a @ref TextStyle into a @ref ShapedText.
 *
 * @note Not thread-safe by contract: shaping rasterizes on demand, so one
 *       shaper serves one UI thread.
 */
class ITextShaper {
public:
    virtual ~ITextShaper() = default;

    /**
     * @brief Lays @p utf8 out into @p out, reusing its storage.
     *
     * @param maxWidth Wrap width in logical pixels; @ref kUnbounded for none.
     *        Ignored when @c style.wrap is TextWrap::None.
     */
    virtual void shape(std::string_view utf8, const TextStyle& style, f32 maxWidth,
                       ShapedText& out) = 0;

    /// Distance between consecutive baselines at @p style, logical pixels.
    [[nodiscard]] virtual f32 lineHeight(const TextStyle& style) = 0;

    /// Baseline offset from the line box top, logical pixels.
    [[nodiscard]] virtual f32 ascent(const TextStyle& style) = 0;

    /// @{
    /// Glyph raster pages, for the backend that uploads them to the GPU. A
    /// shaper that rasterizes elsewhere reports none.
    [[nodiscard]] virtual u32 pageCount() const noexcept { return 0; }
    [[nodiscard]] virtual FontAtlas* page(u32 index) noexcept { return nullptr; }
    /// @}

    /**
     * @brief Sets the device-pixel ratio glyphs are rasterized at.
     *
     * Layout stays in logical pixels whatever this is: a 14px label is 14
     * units tall on a 200% display too, it is simply rasterized at 28 device
     * pixels so it is not blurry. The backend applies the same factor when it
     * emits vertices.
     */
    virtual void setScale(f32 scale) = 0;

    [[nodiscard]] virtual f32 scale() const noexcept = 0;
};

/// Construction parameters for @ref AtlasTextShaper.
struct TextShaperDesc {
    /// Path to a .ttf/.otf file. Empty, or a file that fails to load, falls
    /// back to the engine's embedded bitmap font.
    std::string fontPath;

    u32 pageSize = 1024; //! Edge length of each square glyph page, in texels.

    /// Largest number of distinct rasterization sizes kept alive at once. Each
    /// costs one page and one GPU texture, so a theme using four text sizes
    /// wants four -- not one per pixel value some animation passed through.
    u32 maxPages = 6;
};

/**
 * @class AtlasTextShaper
 * @brief The FontAtlas-backed shaper: kerned, wrapped, and one page per size.
 *
 * A FontAtlas rasterizes at a single em size, so a shaper that must serve a
 * 32px heading and a 12px caption keeps one page each rather than scaling one
 * of them -- scaling is exactly what makes small text on a big atlas look
 * washed out. Pages are created on first use and capped by
 * TextShaperDesc::maxPages.
 */
class AtlasTextShaper final : public ITextShaper {
public:
    explicit AtlasTextShaper(const TextShaperDesc& desc = TextShaperDesc{});
    ~AtlasTextShaper() override;

    AtlasTextShaper(const AtlasTextShaper&) = delete;
    AtlasTextShaper& operator=(const AtlasTextShaper&) = delete;

    void shape(std::string_view utf8, const TextStyle& style, f32 maxWidth,
               ShapedText& out) override;

    [[nodiscard]] f32 lineHeight(const TextStyle& style) override;
    [[nodiscard]] f32 ascent(const TextStyle& style) override;

    [[nodiscard]] u32 pageCount() const noexcept override;
    [[nodiscard]] FontAtlas* page(u32 index) noexcept override;

    void setScale(f32 scale) override;
    [[nodiscard]] f32 scale() const noexcept override { return _scale; }

    /// True when the requested font loaded and the embedded fallback is not in
    /// use. Worth surfacing: the fallback is a 5x8 bitmap, and a UI that
    /// silently degraded to it looks broken rather than unstyled.
    [[nodiscard]] bool usingFallbackFont() const noexcept { return _fontData.empty(); }

private:
    /// One rasterization size: an atlas, and the logical-pixel metrics derived
    /// from it.
    struct Page {
        std::unique_ptr<FontAtlas> atlas;
        u32 devicePixels = 0; //! Rasterization size; the page's identity.
        f32 lineHeight = 0.0f;
        f32 ascent = 0.0f;
    };

    /// Index of the page serving @p style, creating it if there is room.
    /// Falls back to the nearest existing page when the cap is reached.
    [[nodiscard]] Page* _pageFor(const TextStyle& style);

    [[nodiscard]] u32 _devicePixelsFor(const TextStyle& style) const noexcept;

    TextShaperDesc _desc{};
    std::vector<u8> _fontData; //! Empty when running on the embedded font.
    std::vector<Page> _pages;
    f32 _scale = 1.0f;
};

} // namespace aura3d::ui

#endif // AURA_UI_TEXTENGINE_H
