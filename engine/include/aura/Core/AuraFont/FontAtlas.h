#ifndef AURA_FONTATLAS_H
#define AURA_FONTATLAS_H

#pragma once

#include <array>
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

#include "aura/Core/AuraFont/AuraBitmapFont.h"
#include "aura/aura.h"

namespace aura3d
{

/**
 * @brief Placement and layout metrics of a single rasterized glyph.
 *
 * All distances are in pixels at the atlas' configured pixel height. The UV
 * pair addresses the glyph's cell inside the atlas texture and can be fed to a
 * quad directly.
 */
struct GlyphInfo
{
    glm::vec2 uvMin{0.0f}; //! Atlas UV of the cell's top-left texel.
    glm::vec2 uvMax{0.0f}; //! Atlas UV of the cell's bottom-right texel.
    glm::vec2 size{0.0f};  //! Rasterized bitmap size in pixels.
    /**
     * Offset from the pen position to the glyph quad's top-left corner, with the
     * pen taken to sit on the *top* of the line box (y grows downwards). Already
     * folds in the font's ascent, so a caller never needs the baseline itself.
     */
    glm::vec2 bearing{0.0f};
    float advance = 0.0f; //! Horizontal pen movement after drawing, in pixels.
};

/**
 * @brief Decodes one UTF-8 scalar value starting at @p offset.
 *
 * Advances @p offset past the consumed bytes. Malformed sequences consume a
 * single byte and yield U+FFFD, so a decode loop always terminates.
 */
[[nodiscard]] char32_t decodeUtf8(std::string_view text, size_t &offset) noexcept;

/// Construction parameters for a @ref FontAtlas. Namespace-scope (not nested)
/// so it can be used as a defaulted parameter of FontAtlas' own methods.
struct FontAtlasDesc
{
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
 * rasterization grows a pending dirty rectangle; @ref takeUpload hands that
 * rectangle over as R8 so the renderer can push it to the GPU with a sub-image
 * copy instead of reuploading the whole texture.
 *
 * The atlas stores coverage only (white RGB, alpha = coverage). Text colour is
 * supplied per-vertex at draw time, so one atlas serves every colour.
 *
 * @note Not thread-safe: @ref glyph mutates the cache and the dirty rectangle.
 */
class FontAtlas
{
  public:
    /// Construction parameters; see @ref FontAtlasDesc.
    using Desc = FontAtlasDesc;

    /// A half-open rectangle of the atlas that changed since the last upload.
    struct DirtyRegion
    {
        u32 x = 0;
        u32 y = 0;
        u32 width = 0;
        u32 height = 0;
    };

    /// UV bounds of a reserved cell, top-left and bottom-right.
    struct UvRect
    {
        glm::vec2 min{0.0f};
        glm::vec2 max{0.0f};
    };

    /// A rectangle of coverage, ready for IRenderer::updateCoverageTextureRegion().
    struct PendingUpload
    {
        DirtyRegion region;           //! Destination rectangle inside the atlas.
        std::span<const u8> coverage; //! region.width * region.height bytes, tightly packed.
    };

    /**
     * @brief Loads a .ttf/.otf file from disk.
     * @return The atlas, or a human-readable message on failure.
     */
    [[nodiscard]] static std::expected<std::unique_ptr<FontAtlas>, std::string> fromFile(const std::string &path,
                                                                                         const Desc &desc = Desc{});

    /**
     * @brief Adopts an in-memory .ttf/.otf image.
     *
     * The bytes are moved into the atlas because stb_truetype keeps pointing at
     * them for the object's whole lifetime.
     */
    [[nodiscard]] static std::expected<std::unique_ptr<FontAtlas>, std::string> fromMemory(std::vector<u8> fontData,
                                                                                           const Desc &desc = Desc{});

    /**
     * @brief Builds an atlas from the engine's embedded rounded bitmap font.
     *
     * Requires no asset on disk, so it is the dependable fallback on platforms
     * where no font file is staged (WASM, Android) and when a load fails.
     * Never fails.
     */
    [[nodiscard]] static std::unique_ptr<FontAtlas> builtinBitmap(const Desc &desc = Desc{});

    /// Shares immutable font data and texture storage; glyph metrics stay size-specific.
    [[nodiscard]] std::unique_ptr<FontAtlas> createSharedSize(float pixelHeight) const;

    /// Equal for views sharing one sheet, so they can share one texture.
    [[nodiscard]] const void *storageIdentity() const noexcept;
    /// Advances whenever the shared sheet changes.
    [[nodiscard]] u64 coverageRevision() const noexcept;

    /// Clears and returns whether a cell failed to fit since the previous call.
    [[nodiscard]] bool takeAllocationFailure() noexcept;

    ~FontAtlas();

    FontAtlas(const FontAtlas &) = delete;
    FontAtlas &operator=(const FontAtlas &) = delete;
    FontAtlas(FontAtlas &&) noexcept;
    FontAtlas &operator=(FontAtlas &&) noexcept;

    /**
     * @brief Returns @p codepoint's cell, rasterizing it on first use.
     *
     * @return The glyph, or nullptr when the font has no such codepoint and no
     *         substitute could be placed (e.g. the atlas is full).
     */
    [[nodiscard]] const GlyphInfo *glyph(char32_t codepoint);

    /// Kerning adjustment to apply between @p left and @p right, in pixels.
    [[nodiscard]] float kerning(char32_t left, char32_t right) const noexcept;

    /**
     * @brief Reserves (once) a fully opaque cell and returns the UV of its centre.
     *
     * The atlas is a coverage map, so a plain filled rectangle has no UV to
     * sample without this: it lets a batcher mixing solid quads and text (a UI)
     * sample both from one atlas and stay in a single draw call.
     *
     * The cell is 4x4 with the UV addressing its centre, so bilinear filtering
     * never reaches a neighbouring glyph. Allocated on first call and cached
     * thereafter; call early if failure is not tolerable.
     *
     * @return The UV to give every vertex of a solid quad, or nullopt when the
     *         atlas is too full to place the cell.
     */
    [[nodiscard]] std::optional<glm::vec2> solidTexelUv() noexcept;

    /**
     * @brief Reserves (once per radius) a quarter-disc coverage mask.
     *
     * Lets a rounded rectangle be drawn as a nine-slice -- four corner quads
     * plus three solid spans -- instead of one quad per scanline of the caps,
     * which is O(radius) geometry for a shape whose description is a single
     * number. Coverage is the exact area of the disc inside each texel, so the
     * corner is antialiased rather than stepped.
     *
     * The cell is @p radius texels square with the curve's outside at uvMin,
     * so a corner drawn @p radius pixels wide samples it one-to-one; the other
     * three corners are the same cell with uvMin/uvMax swapped per axis.
     *
     * @return The cell's UV bounds, or nullopt when @p radius is out of range
     *         or the atlas is too full to place it.
     */
    [[nodiscard]] const UvRect *cornerMask(u32 radius) noexcept;

    /// Quarter-annulus coverage for a rounded border. Width is in mask texels,
    /// quantized to 1/256 pixel for caching. Shares the regular glyph texture.
    [[nodiscard]] const UvRect *cornerRingMask(u32 radius, f32 width) noexcept;

    /// Largest radius @ref cornerMask will place.
    static constexpr u32 kMaxCornerRadius = 64;

    /**
     * @brief Reserves (once per @p id) an exact-coverage mask of a convex
     *        polygon.
     *
     * The general form of @ref cornerMask, and what keeps every icon a UI
     * needs -- a chevron, a disclosure arrow, a tick, a close cross -- from
     * becoming its own drawing primitive. The shape is rasterized once into
     * the atlas and drawn thereafter as a single textured quad, in whatever
     * colour the vertex carries.
     *
     * Coverage is the *exact* area of the polygon inside each texel, obtained
     * by clipping it against the texel's square and taking the signed area of
     * what survives -- not a point sample and not supersampling, so a diagonal
     * edge is as smooth as the arithmetic allows at any size.
     *
     * @param id Caller's identity for this shape. The same id returns the
     *        cached cell without re-rasterizing, so an icon costs its
     *        rasterization once for the life of the atlas.
     * @param size Cell edge in texels; the shape is drawn this many pixels wide.
     * @param polygon Vertices in cell space, @c [0,size] on both axes. Must be
     *        convex; winding does not matter.
     *
     * @return The cell's UV bounds, or nullptr when @p polygon is not a
     *         polygon, @p size is out of range, or the atlas is too full.
     */
    [[nodiscard]] const UvRect *convexMask(u32 id, u32 size, std::span<const glm::vec2> polygon) noexcept;

    /// Largest cell edge @ref convexMask will place.
    static constexpr u32 kMaxMaskSize = 64;

    [[nodiscard]] float lineHeight() const noexcept
    {
        return _lineHeight;
    }
    [[nodiscard]] float ascent() const noexcept
    {
        return _ascent;
    }
    [[nodiscard]] float descent() const noexcept
    {
        return _descent;
    }
    [[nodiscard]] float pixelHeight() const noexcept
    {
        return _desc.pixelHeight;
    }
    [[nodiscard]] u32 width() const noexcept
    {
        return _desc.width;
    }
    [[nodiscard]] u32 height() const noexcept
    {
        return _desc.height;
    }

    /// True when glyphs were rasterized since the last @ref takeUpload.
    [[nodiscard]] bool dirty() const noexcept;

    /**
     * @brief What a texture last synced at @p revision lacks, or nullopt when it is current.
     *
     * Every texture of the sheet keeps its own @p revision, starting at 0, and this
     * advances it. The dirty rectangle serves the texture that saw the last clean
     * state; any other gets the whole sheet. The span lasts until the next call on
     * any view of the sheet.
     */
    [[nodiscard]] std::optional<PendingUpload> takeUpload(u64 &revision);

  private:
    struct FontImpl;
    struct Storage;

    explicit FontAtlas(const Desc &desc, std::shared_ptr<Storage> storage = {});
    void initializeMetrics();

    /// Rasterizes @p codepoint through stb_truetype; false when absent.
    [[nodiscard]] bool rasterizeTrueType(char32_t codepoint, GlyphInfo &out);

    /// Rasterizes @p codepoint from the embedded bitmap font; false when absent.
    [[nodiscard]] bool rasterizeBitmap(char32_t codepoint, GlyphInfo &out);

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
    void blitCoverage(const u8 *src, u32 srcStride, glm::uvec2 origin, u32 w, u32 h) noexcept;

    Desc _desc{};
    std::shared_ptr<FontImpl> _font; //! Null for the bitmap-font fallback.
    std::shared_ptr<Storage> _storage;
    u32 _failedAllocations = 0;

    std::unordered_map<char32_t, GlyphInfo> _glyphs;

    /*
     * Direct-mapped kern cache, keyed by the packed codepoint pair.
     *
     * stbtt_GetCodepointKernAdvance() is not a lookup: it re-runs a cmap
     * search for *both* codepoints and then a binary search of the kern table,
     * and walkGlyphs() asks for every adjacent pair of every string it lays
     * out -- twice over for a measured-then-drawn label, every frame. A fixed
     * table keeps this allocation-free so kerning() can stay noexcept; the
     * stored pair is compared before the value is trusted, so a collision
     * costs a recompute and never a wrong advance.
     */
    static constexpr usize kKernCacheSlots = 512; //! Power of two; masked, not modulo.

    struct KernEntry
    {
        u64 pair = 0; //! 0 is unreachable: walkGlyphs() stops at codepoint 0.
        f32 value = 0.0f;
    };

    mutable std::array<KernEntry, kKernCacheSlots> _kernCache{};

    float _scale = 1.0f; //! stb_truetype units -> pixels.
    float _ascent = 0.0f;
    float _descent = 0.0f;
    float _lineHeight = 0.0f;

    //! The embedded face at this atlas's size; empty for a TrueType atlas.
    AuraBitmapFont::Layout _bitmapLayout;
};

} // namespace aura3d

#endif // AURA_FONTATLAS_H
