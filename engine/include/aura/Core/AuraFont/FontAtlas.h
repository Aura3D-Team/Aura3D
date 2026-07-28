#ifndef AURA_FONTATLAS_H
#define AURA_FONTATLAS_H

#pragma once

#include <cstddef>
#include <expected>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <glm/glm.hpp>

#include "aura/aura.h"

namespace aura3d {

/**
 * @brief Placement and layout metrics of a single rasterized glyph.
 *
 * All distances are in pixels at the atlas' configured pixel height. The UV
 * pair addresses the glyph's cell inside the atlas texture and can be fed to a
 * quad directly.
 */
struct GlyphInfo {
    glm::vec2 uvMin{0.0f};   //! Atlas UV of the cell's top-left texel.
    glm::vec2 uvMax{0.0f};   //! Atlas UV of the cell's bottom-right texel.
    glm::vec2 size{0.0f};    //! Rasterized bitmap size in pixels.
    /**
     * Offset from the pen position to the glyph quad's top-left corner, with the
     * pen taken to sit on the *top* of the line box (y grows downwards). Already
     * folds in the font's ascent, so a caller never needs the baseline itself.
     */
    glm::vec2 bearing{0.0f};
    float advance = 0.0f;    //! Horizontal pen movement after drawing, in pixels.
};

/**
 * @brief Decodes one UTF-8 scalar value starting at @p offset.
 *
 * Advances @p offset past the consumed bytes. Malformed sequences consume a
 * single byte and yield U+FFFD, so a decode loop always terminates.
 */
[[nodiscard]] char32_t decodeUtf8(std::string_view text, size_t& offset) noexcept;

/**
 * @brief Construction parameters for a @ref FontAtlas.
 *
 * At namespace scope rather than nested in FontAtlas because a nested class'
 * default member initializers are not usable inside the enclosing class'
 * own declarations, which is exactly where this type is wanted as a defaulted
 * parameter.
 */
struct FontAtlasDesc {
    u32 width = 2048;          //! Atlas width in texels.
    u32 height = 2048;         //! Atlas height in texels.
    float pixelHeight = 32.0f; //! Rasterization size (cap height to descender).
    u32 padding = 1;           //! Guard texels between cells; stops filter bleed.
};

/**
 * @class FontAtlas
 * @brief A GPU-friendly glyph cache backed by stb_truetype.
 *
 * The atlas owns one large single-channel coverage bitmap (2048x2048 by
 * default). Glyphs are rasterized lazily -- the first time a codepoint is
 * actually drawn -- and packed into it with a shelf allocator. Every
 * rasterization grows a pending dirty rectangle; @ref takeDirtyUpload hands
 * that rectangle over as RGBA8 so the renderer can push it to the GPU with a
 * sub-image copy instead of reuploading the whole texture.
 *
 * The atlas stores coverage only (white RGB, alpha = coverage). Text colour is
 * supplied per-vertex at draw time, so one atlas serves every colour.
 *
 * @note Not thread-safe: @ref glyph mutates the cache and the dirty rectangle.
 */
class FontAtlas {
public:
    /// Construction parameters; see @ref FontAtlasDesc.
    using Desc = FontAtlasDesc;

    /// A half-open rectangle of the atlas that changed since the last upload.
    struct DirtyRegion {
        u32 x = 0;
        u32 y = 0;
        u32 width = 0;
        u32 height = 0;
    };

    /// A dirty rectangle expanded to RGBA8, ready for a sub-image upload.
    struct PendingUpload {
        DirtyRegion region;        //! Destination rectangle inside the atlas.
        std::span<const u8> rgba;  //! region.width * region.height * 4 bytes.
    };

    /**
     * @brief Loads a .ttf/.otf file from disk.
     * @return The atlas, or a human-readable message on failure.
     */
    [[nodiscard]] static std::expected<std::unique_ptr<FontAtlas>, std::string>
    fromFile(const std::string& path, const Desc& desc = Desc{});

    /**
     * @brief Adopts an in-memory .ttf/.otf image.
     *
     * The bytes are moved into the atlas because stb_truetype keeps pointing at
     * them for the object's whole lifetime.
     */
    [[nodiscard]] static std::expected<std::unique_ptr<FontAtlas>, std::string>
    fromMemory(std::vector<u8> fontData, const Desc& desc = Desc{});

    /**
     * @brief Builds an atlas from the engine's embedded 5x8 bitmap font.
     *
     * Requires no asset on disk, so it is the dependable fallback on platforms
     * where no font file is staged (WASM, Android) and when a load fails.
     * Never fails.
     */
    [[nodiscard]] static std::unique_ptr<FontAtlas> builtinBitmap(const Desc& desc = Desc{});

    ~FontAtlas();

    FontAtlas(const FontAtlas&) = delete;
    FontAtlas& operator=(const FontAtlas&) = delete;
    FontAtlas(FontAtlas&&) noexcept;
    FontAtlas& operator=(FontAtlas&&) noexcept;

    /**
     * @brief Returns @p codepoint's cell, rasterizing it on first use.
     *
     * @return The glyph, or nullptr when the font has no such codepoint and no
     *         substitute could be placed (e.g. the atlas is full).
     */
    [[nodiscard]] const GlyphInfo* glyph(char32_t codepoint);

    /// Kerning adjustment to apply between @p left and @p right, in pixels.
    [[nodiscard]] float kerning(char32_t left, char32_t right) const noexcept;

    [[nodiscard]] float lineHeight() const noexcept { return _lineHeight; }
    [[nodiscard]] float ascent() const noexcept { return _ascent; }
    [[nodiscard]] float descent() const noexcept { return _descent; }
    [[nodiscard]] float pixelHeight() const noexcept { return _desc.pixelHeight; }
    [[nodiscard]] u32 width() const noexcept { return _desc.width; }
    [[nodiscard]] u32 height() const noexcept { return _desc.height; }

    /// True when glyphs were rasterized since the last @ref takeDirtyUpload.
    [[nodiscard]] bool dirty() const noexcept { return _dirty; }

    /**
     * @brief Hands over the pending sub-image upload and marks the atlas clean.
     *
     * @return The RGBA8 expansion of the dirty rectangle, or nullopt when
     *         nothing changed. The span stays valid until the next call.
     */
    [[nodiscard]] std::optional<PendingUpload> takeDirtyUpload();

private:
    struct FontImpl;

    explicit FontAtlas(const Desc& desc);

    /// Rasterizes @p codepoint through stb_truetype; false when absent.
    [[nodiscard]] bool rasterizeTrueType(char32_t codepoint, GlyphInfo& out);

    /// Rasterizes @p codepoint from the embedded bitmap font; false when absent.
    [[nodiscard]] bool rasterizeBitmap(char32_t codepoint, GlyphInfo& out);

    /**
     * @brief Reserves a @p w x @p h cell with the shelf allocator.
     *
     * Cells are laid down left to right on a shelf whose height is that of its
     * tallest member; a cell that no longer fits opens a new shelf below.
     * Cheap, and near-optimal for glyphs, which cluster around one height.
     *
     * @return Top-left texel of the reserved cell, or nullopt when full.
     */
    [[nodiscard]] std::optional<glm::uvec2> reserveCell(u32 w, u32 h) noexcept;

    /// Grows the pending dirty rectangle to also cover the given cell.
    void markDirty(u32 x, u32 y, u32 w, u32 h) noexcept;

    /// Blits an 8-bit coverage bitmap into the atlas at @p origin.
    void blitCoverage(const u8* src, u32 srcStride, glm::uvec2 origin, u32 w, u32 h) noexcept;

    Desc _desc{};
    std::unique_ptr<FontImpl> _font;  //! Null for the bitmap-font fallback.
    std::vector<u8> _fontData;        //! Backing .ttf bytes; stb points into these.
    std::vector<u8> _coverage;        //! width * height single-channel master.
    std::vector<u8> _scratch;         //! RGBA staging for the pending upload.

    std::unordered_map<char32_t, GlyphInfo> _glyphs;

    //! Shelf allocator cursor.
    u32 _shelfX = 0;
    u32 _shelfY = 0;
    u32 _shelfHeight = 0;

    //! Pending dirty rectangle, stored as inclusive-exclusive bounds.
    u32 _dirtyX0 = 0, _dirtyY0 = 0, _dirtyX1 = 0, _dirtyY1 = 0;
    bool _dirty = false;

    float _scale = 1.0f;      //! stb_truetype units -> pixels.
    float _ascent = 0.0f;
    float _descent = 0.0f;
    float _lineHeight = 0.0f;

    //! Integer upscale applied to the 5x8 bitmap font in fallback mode.
    u32 _bitmapScale = 1;
};

} // namespace aura3d

#endif // AURA_FONTATLAS_H
