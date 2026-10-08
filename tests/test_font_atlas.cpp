// Covers the backend-independent half of the GPU font pipeline: UTF-8 decoding,
// the shelf packer, on-demand glyph rasterization, dirty-rectangle tracking and
// the RGBA expansion handed to the renderer. None of it needs a window or a
// graphics device, so it runs anywhere CTest does.

#include <algorithm>
#include <bit>
#include <cmath>
#include <string>
#include <vector>

#include "TestUtils.h"

#include "aura/Core/AuraFont/AuraBitmapFont.h"
#include "aura/Core/AuraFont/FontAtlas.h"

using aura3d::AuraBitmapFont;
using aura3d::decodeUtf8;
using aura3d::FontAtlas;
using aura3d::FontAtlasDesc;
using aura3d::GetDefaultBitmapFont;
using aura3d::GlyphInfo;

namespace
{

// A small atlas keeps the test's memory footprint trivial while still
// exercising the same packing code the 2048x2048 default uses.
FontAtlasDesc smallAtlas()
{
    FontAtlasDesc desc;
    desc.width = 256;
    desc.height = 256;
    desc.pixelHeight = 16.0f;
    return desc;
}

void testUtf8Decoding()
{
    // One scalar per sequence length, plus a malformed lead byte.
    const std::string text = "Aé€\U0001F600";

    std::size_t offset = 0;
    AURA_CHECK(decodeUtf8(text, offset) == U'A' && offset == 1, "decodeUtf8 reads a 1-byte ASCII scalar");
    AURA_CHECK(decodeUtf8(text, offset) == U'é' && offset == 3, "decodeUtf8 reads a 2-byte scalar");
    AURA_CHECK(decodeUtf8(text, offset) == U'€' && offset == 6, "decodeUtf8 reads a 3-byte scalar");
    AURA_CHECK(decodeUtf8(text, offset) == U'\U0001F600' && offset == 10, "decodeUtf8 reads a 4-byte scalar");
    AURA_CHECK(decodeUtf8(text, offset) == 0, "decodeUtf8 returns 0 at the end of the string");

    // A stray continuation byte must consume exactly one byte, so that a decode
    // loop over malformed input still terminates.
    const std::string malformed = "\x80\x41";
    std::size_t badOffset = 0;
    AURA_CHECK(decodeUtf8(malformed, badOffset) == 0xFFFDu && badOffset == 1,
               "decodeUtf8 yields U+FFFD and advances one byte on a bad lead");
    AURA_CHECK(decodeUtf8(malformed, badOffset) == U'A', "decodeUtf8 resynchronises after a malformed sequence");

    // A truncated multi-byte sequence at the very end must not read past it.
    const std::string truncated = "\xE2\x82";
    std::size_t truncatedOffset = 0;
    AURA_CHECK(decodeUtf8(truncated, truncatedOffset) == 0xFFFDu && truncatedOffset == 1,
               "decodeUtf8 rejects a truncated sequence without overrunning");
}

void testUtf8RejectsInvalidScalars()
{
    for (std::string_view invalid : {"\xC0\x80", "\xE0\x80\xAF", "\xED\xA0\x80", "\xF4\x90\x80\x80"})
    {
        usize offset = 0;
        AURA_CHECK(decodeUtf8(invalid, offset) == 0xFFFDu && offset == 1,
                   "overlong encodings, surrogates and out-of-range scalars consume one invalid byte");
    }
}

void testSharedCoverageStorage()
{
    auto atlas = FontAtlas::builtinBitmap(smallAtlas());
    const auto *small = atlas->glyph(U'A');
    const GlyphInfo retained = *small;
    const auto solid = atlas->solidTexelUv();
    const auto *corner = atlas->cornerMask(8);
    auto larger = atlas->createSharedSize(24);
    AURA_CHECK(larger->storageIdentity() == atlas->storageIdentity(), "size views share one coverage sheet");
    AURA_CHECK(larger->solidTexelUv() == solid && larger->cornerMask(8) == corner,
               "solid and shape masks are reused across sizes");
    const auto *large = larger->glyph(U'A');
    AURA_CHECK(large && large->size.y == 24 && large->uvMin != retained.uvMin,
               "each size retains its own raster and cells");
    AURA_CHECK(small->uvMin == retained.uvMin && small->uvMax == retained.uvMax,
               "adding a size preserves old glyph UVs");
    const u64 revision = atlas->coverageRevision();
    AURA_CHECK(revision > 0 && revision == larger->coverageRevision(),
               "coverage revisions include every shared view's writes");

    //! Two textures of one sheet: the first takes the dirty rectangle, the other the whole sheet.
    u64 first = 0, second = 0;
    const auto upload = larger->takeUpload(first);
    const FontAtlas::DirtyRegion region = upload ? upload->region : FontAtlas::DirtyRegion{};
    const std::vector<u8> packed =
        upload ? std::vector<u8>(upload->coverage.begin(), upload->coverage.end()) : std::vector<u8>{};
    AURA_CHECK(upload && region.width < atlas->width() && packed.size() == usize(region.width) * region.height &&
                   first == revision && !atlas->dirty() && !atlas->takeUpload(first),
               "the first texture takes the tightly packed dirty rectangle once");
    const auto whole = atlas->takeUpload(second);
    bool equal = whole && whole->region.width == atlas->width() && whole->region.height == atlas->height() &&
                 second == revision && !packed.empty();
    for (u32 y = 0; equal && y < region.height; ++y)
        for (u32 x = 0; equal && x < region.width; ++x)
            equal &= packed[usize(y) * region.width + x] ==
                     whole->coverage[usize(region.y + y) * atlas->width() + region.x + x];
    AURA_CHECK(equal, "a texture that missed the rectangle gets the whole sheet, matching the packed copy");
    (void)atlas->glyph(U'Z');
    const auto next = atlas->takeUpload(second);
    const auto firstAgain = atlas->takeUpload(first);
    AURA_CHECK(next && firstAgain && next->region.width < atlas->width() && firstAgain->region.width == atlas->width(),
               "the dirty rectangle follows whichever texture took the last clean state");
    atlas.reset();
    AURA_CHECK(larger->glyph(U'B') != nullptr && larger->dirty(),
               "a size view owns its storage after its creator is destroyed");
}

void testFailedReservationPreservesShelf()
{
    auto atlas = FontAtlas::builtinBitmap(FontAtlasDesc{.width = 20, .height = 20, .pixelHeight = 8});
    AURA_CHECK(atlas->cornerMask(8) && atlas->cornerMask(6), "fixture fills most of its first shelf");
    AURA_CHECK(!atlas->cornerMask(15) && atlas->takeAllocationFailure(), "oversized remaining placement fails");
    const auto *small = atlas->cornerMask(2);
    AURA_CHECK(small && small->min.y == 0 && small->min.x == 16.0f / 20.0f,
               "a failed wrap leaves the previous shelf available for smaller cells");
}

void testBitmapFallbackMetrics()
{
    auto atlas = FontAtlas::builtinBitmap(smallAtlas());
    AURA_CHECK(atlas != nullptr, "builtinBitmap always produces an atlas");
    if (!atlas)
        return;

    AURA_CHECK(atlas->width() == 256 && atlas->height() == 256, "atlas honours the requested dimensions");
    AURA_CHECK(atlas->lineHeight() > 0.0f, "bitmap fallback reports a usable line height");

    const GlyphInfo *a = atlas->glyph(U'A');
    AURA_CHECK(a != nullptr, "bitmap fallback resolves an ASCII glyph");
    if (a)
    {
        AURA_CHECK(a->advance > 0.0f, "'A' advances the pen");
        AURA_CHECK(a->size.x > 0.0f && a->size.y > 0.0f, "'A' occupies a non-empty cell");
        AURA_CHECK(a->uvMax.x > a->uvMin.x && a->uvMax.y > a->uvMin.y, "'A' has a non-degenerate UV rectangle");
    }

    // Space is metrics-only: it moves the pen but reserves no cell, so its UVs
    // stay degenerate and the batch builder skips emitting a quad for it.
    const GlyphInfo *space = atlas->glyph(U' ');
    AURA_CHECK(space != nullptr, "space resolves");
    if (space)
    {
        AURA_CHECK(space->advance > 0.0f, "space advances the pen");
        AURA_CHECK(space->size.x == 0.0f || space->size.y == 0.0f, "space reserves no atlas cell");
    }

    // The embedded font covers 7-bit ASCII only; anything else must fall back to
    // '?' rather than returning nullptr and dropping the character silently.
    const GlyphInfo *substitute = atlas->glyph(U'€');
    const GlyphInfo *question = atlas->glyph(U'?');
    AURA_CHECK(substitute != nullptr && question != nullptr,
               "an out-of-range codepoint falls back to a substitute glyph");
    if (substitute && question)
    {
        AURA_CHECK(substitute->uvMin == question->uvMin, "the substitute is '?' rather than a fresh cell");
    }
}

void testBitmapRasterSizeAndCoverage()
{
    for (const float height : {12.0f, 14.0f, 16.0f, 18.0f, 20.0f, 32.0f})
    {
        auto desc = smallAtlas();
        desc.pixelHeight = height;
        auto atlas = FontAtlas::builtinBitmap(desc);
        const auto *wide = atlas->glyph(U'W');
        const auto *narrow = atlas->glyph(U'i');
        AURA_CHECK(wide && narrow && narrow->advance < wide->advance, "bitmap glyphs use proportional spacing");
        AURA_CHECK(wide && narrow && wide->advance == std::round(wide->advance) &&
                       narrow->advance == std::round(narrow->advance),
                   "bitmap advances are whole pixels, so gaps between letters stay even");
        AURA_CHECK(wide && wide->size.y == height && atlas->lineHeight() == height,
                   "bitmap raster and line box match the requested device size");
        for (char32_t cp = U'!'; cp <= U'~'; ++cp)
            (void)atlas->glyph(cp);
        u64 revision = 0;
        const auto upload = atlas->takeUpload(revision);
        bool binary = upload.has_value();
        if (upload)
            for (const u8 coverage : upload->coverage)
                binary &= coverage == 0 || coverage == 255;
        AURA_CHECK(binary, "embedded glyphs retain one-bit coverage at every raster size");
    }
}

void testBitmapFaceAtNativeSize()
{
    const AuraBitmapFont &font = GetDefaultBitmapFont();
    const auto native = font.layout(16);
    bool identity = native.rows.size() == 16 && native.ascent == 13;
    for (u32 row = 0; identity && row < 16; ++row)
        identity &= native.rows[row] == row && native.glyphs['B'].rows[row] == row;
    AURA_CHECK(identity, "at 16 px the face is drawn row for row");

    const auto &space = native.glyphs[' '];
    const auto &wide = native.glyphs['W'];
    AURA_CHECK(space.columns.empty() && space.advance == 4 &&
                   wide.columns.size() == static_cast<usize>(font.ink(font.data['W']).width) &&
                   native.glyphs['i'].advance < wide.advance,
               "blank glyphs advance half a cell and narrow glyphs advance less than wide ones");
}

/// Whether every horizontal bar of @p glyphChar -- a row of three or more pixels
/// unlike both neighbours -- keeps at least one row at @p height.
bool keepsEveryStroke(char glyphChar, u32 height)
{
    const AuraBitmapFont &font = GetDefaultBitmapFont();
    const auto &glyph = font.data[static_cast<unsigned char>(glyphChar)];
    const std::vector<u8> rows = font.layout(height).glyphs[static_cast<unsigned char>(glyphChar)].rows;
    for (u32 row = 0; row < 16; ++row)
    {
        const u8 above = row > 0 ? glyph[row - 1] : u8{0};
        const u8 below = row < 15 ? glyph[row + 1] : u8{0};
        if (std::popcount(glyph[row]) < 3 || glyph[row] == above || glyph[row] == below)
            continue;
        if (std::ranges::find(rows, row) == rows.end())
            return false;
    }
    return true;
}

void testBitmapSmallSizesKeepStrokes()
{
    bool kept = true;
    for (const u32 height : {13u, 14u, 15u})
        for (const char c : {'T', 'f', '7', 'E', 'e', 'a', 'g', 'B', '3', '8'})
            kept &= keepsEveryStroke(c, height);
    AURA_CHECK(kept, "13-15 px keep every horizontal bar: padding goes before cap lines and crossbars");

    const std::vector<u8> rows = GetDefaultBitmapFont().layout(12).glyphs['i'].rows;
    AURA_CHECK(keepsEveryStroke('4', 12) && std::ranges::find(rows, u8{4}) != rows.end() && rows.size() == 12,
               "12 px keeps both the 4's crossbar and the gap under the i's dot");

    bool descenders = true;
    for (const u32 height : {9u, 10u, 11u, 12u})
    {
        const auto layout = GetDefaultBitmapFont().layout(height);
        descenders &= std::ranges::count_if(layout.rows,
                                            [](u8 row)
                                            {
                                                return row >= AuraBitmapFont::baseline;
                                            }) >= 2;
    }
    AURA_CHECK(descenders, "small sizes keep two descender rows, so g, p and y stay legible");

    const auto grown = GetDefaultBitmapFont().layout(18);
    bool strokesSingle = true;
    for (const char c : {'E', 'e', 'H'})
    {
        const auto &glyph = GetDefaultBitmapFont().data[static_cast<unsigned char>(c)];
        for (u32 row = 0; row < 16; ++row)
        {
            const u8 above = row > 0 ? glyph[row - 1] : u8{0};
            const u8 below = row < 15 ? glyph[row + 1] : u8{0};
            if (glyph[row] != 0 && glyph[row] != above && glyph[row] != below)
                strokesSingle &= std::ranges::count(grown.rows, row) == 1;
        }
    }
    AURA_CHECK(strokesSingle, "growing to 18 px repeats redundant rows, so one-row strokes stay one row");
}

void testGlyphCachingIsStable()
{
    auto atlas = FontAtlas::builtinBitmap(smallAtlas());
    if (!atlas)
        return;

    const GlyphInfo *first = atlas->glyph(U'M');
    AURA_CHECK(first != nullptr, "first request rasterizes 'M'");
    if (!first)
        return;

    const GlyphInfo copy = *first;

    // Re-requesting must hit the cache: same cell, no second rasterization, and
    // therefore nothing newly dirty once the first upload has been taken.
    u64 revision = 0;
    (void)atlas->takeUpload(revision);
    const GlyphInfo *second = atlas->glyph(U'M');
    AURA_CHECK(second != nullptr && second->uvMin == copy.uvMin && second->uvMax == copy.uvMax,
               "a cached glyph keeps its atlas cell");
    AURA_CHECK(!atlas->dirty(), "a cache hit does not dirty the atlas");
}

void testSolidTexelReservation()
{
    auto atlas = FontAtlas::builtinBitmap(smallAtlas());
    if (!atlas)
        return;

    const auto uv = atlas->solidTexelUv();
    AURA_CHECK(uv.has_value(), "an empty atlas can reserve its solid texel");
    if (!uv)
        return;

    AURA_CHECK(uv->x > 0.0f && uv->x < 1.0f && uv->y > 0.0f && uv->y < 1.0f,
               "the solid texel's UV addresses somewhere inside the atlas");

    AURA_CHECK(atlas->dirty(), "reserving the solid texel queues its upload");

    // What the UV is *for*: a quad sampling it must come out fully opaque, so
    // that a solid rectangle drawn through the glyph atlas looks like a solid
    // rectangle rather than a faint one.
    u64 revision = 0;
    const auto pending = atlas->takeUpload(revision);
    AURA_CHECK(pending.has_value(), "the reserved cell arrives as an upload");
    if (pending)
        AURA_CHECK(!pending->coverage.empty() && std::ranges::all_of(pending->coverage,
                                                                     [](u8 coverage)
                                                                     {
                                                                         return coverage == 255;
                                                                     }),
                   "every texel of the reserved cell is fully opaque");

    // The reservation happens once: a 2D batcher asks for this every frame, and
    // handing out a fresh cell each time would fill the atlas within seconds.
    const auto again = atlas->solidTexelUv();
    AURA_CHECK(again.has_value() && *again == *uv, "the solid texel is reserved once and cached");
    AURA_CHECK(!atlas->dirty(), "a repeat request rasterizes nothing");

    // The cell is reserved, not merely written: glyphs packed afterwards must
    // not be handed the same region and overwrite it.
    for (char32_t c = U'A'; c <= U'Z'; ++c)
        (void)atlas->glyph(c);

    const auto third = atlas->solidTexelUv();
    AURA_CHECK(third.has_value() && *third == *uv, "glyphs rasterized later do not disturb the solid texel");
}

void testDirtyRegionTracking()
{
    auto atlas = FontAtlas::builtinBitmap(smallAtlas());
    if (!atlas)
        return;

    AURA_CHECK(!atlas->dirty(), "a fresh atlas has nothing pending");
    u64 revision = 0;
    AURA_CHECK(!atlas->takeUpload(revision).has_value(), "takeUpload yields nothing when nothing changed");

    const GlyphInfo *g0 = atlas->glyph(U'W');
    const GlyphInfo *g1 = atlas->glyph(U'i');
    AURA_CHECK(g0 && g1, "two distinct glyphs rasterize");
    AURA_CHECK(atlas->dirty(), "rasterizing marks the atlas dirty");

    const auto pending = atlas->takeUpload(revision);
    AURA_CHECK(pending.has_value(), "takeUpload yields the pending rectangle");
    if (pending)
    {
        const auto &region = pending->region;
        AURA_CHECK(region.width > 0 && region.height > 0, "the dirty rectangle is non-empty");
        AURA_CHECK(region.x + region.width <= atlas->width() && region.y + region.height <= atlas->height(),
                   "the dirty rectangle stays inside the atlas");

        // One rectangle must cover both glyphs: that union is what lets a whole
        // string's new characters travel in a single sub-image upload.
        AURA_CHECK(pending->coverage.size() == static_cast<std::size_t>(region.width) * region.height,
                   "the upload is the tightly packed R8 coverage of the rectangle");
        AURA_CHECK(std::ranges::any_of(pending->coverage,
                                       [](u8 coverage)
                                       {
                                           return coverage != 0;
                                       }),
                   "the atlas stores glyph coverage");
    }

    AURA_CHECK(!atlas->dirty(), "taking the upload clears the dirty flag");
}

void testPackerRejectsOversizedAndExhaustedAtlases()
{
    // An atlas far too small for even one glyph must fail cleanly rather than
    // writing outside its buffer.
    FontAtlasDesc tiny;
    tiny.width = 4;
    tiny.height = 4;
    tiny.pixelHeight = 64.0f;

    auto atlas = FontAtlas::builtinBitmap(tiny);
    AURA_CHECK(atlas != nullptr, "an undersized atlas still constructs");
    if (!atlas)
        return;

    AURA_CHECK(atlas->glyph(U'A') == nullptr, "a glyph that cannot fit is refused rather than overflowing");

    // Filling a small atlas with many distinct glyphs must also terminate
    // without corruption once the shelves run out.
    FontAtlasDesc small;
    small.width = 64;
    small.height = 64;
    small.pixelHeight = 24.0f;

    auto crowded = FontAtlas::builtinBitmap(small);
    if (!crowded)
        return;

    bool exhausted = false;
    for (char32_t cp = U'!'; cp <= U'~'; ++cp)
    {
        if (crowded->glyph(cp) == nullptr)
        {
            exhausted = true;
            break;
        }
    }
    AURA_CHECK(exhausted, "the shelf packer reports exhaustion instead of overrunning");

    u64 revision = 0;
    const auto pending = crowded->takeUpload(revision);
    if (pending)
    {
        AURA_CHECK(pending->region.x + pending->region.width <= crowded->width() &&
                       pending->region.y + pending->region.height <= crowded->height(),
                   "the dirty rectangle stays in bounds even when the atlas fills up");
    }
}

void testTrueTypeLoadFailureIsReported()
{
    // Loading must fail with a message rather than throwing or half-succeeding;
    // TextOverlay relies on that to fall back to the embedded bitmap font.
    auto missing = FontAtlas::fromFile("this/path/does/not/exist.ttf", smallAtlas());
    AURA_CHECK(!missing.has_value(), "a missing font file fails");
    if (!missing)
    {
        AURA_CHECK(!missing.error().empty(), "the failure carries a description");
    }

    // Bytes that are not a font at all must be rejected by stb_truetype.
    std::vector<u8> garbage(256, 0xAB);
    auto invalid = FontAtlas::fromMemory(std::move(garbage), smallAtlas());
    AURA_CHECK(!invalid.has_value(), "a malformed font image is rejected");

    auto empty = FontAtlas::fromMemory({}, smallAtlas());
    AURA_CHECK(!empty.has_value(), "an empty font image is rejected");
}

} // namespace

int main()
{
    testUtf8RejectsInvalidScalars();
    testSharedCoverageStorage();
    testFailedReservationPreservesShelf();
    testBitmapRasterSizeAndCoverage();
    testBitmapFaceAtNativeSize();
    testBitmapSmallSizesKeepStrokes();
    testUtf8Decoding();
    testBitmapFallbackMetrics();
    testGlyphCachingIsStable();
    testSolidTexelReservation();
    testDirtyRegionTracking();
    testPackerRejectsOversizedAndExhaustedAtlases();
    testTrueTypeLoadFailureIsReported();

    AURA_TEST_MAIN_RETURN();
}
