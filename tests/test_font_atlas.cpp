// Covers the backend-independent half of the GPU font pipeline: UTF-8 decoding,
// the shelf packer, on-demand glyph rasterization, dirty-rectangle tracking and
// the RGBA expansion handed to the renderer. None of it needs a window or a
// graphics device, so it runs anywhere CTest does.

#include <cmath>
#include <string>
#include <vector>

#include "TestUtils.h"

#include "aura/Core/AuraFont/FontAtlas.h"

using aura3d::FontAtlas;
using aura3d::FontAtlasDesc;
using aura3d::GlyphInfo;
using aura3d::decodeUtf8;

namespace {

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
    AURA_CHECK(decodeUtf8(text, offset) == U'A' && offset == 1,
               "decodeUtf8 reads a 1-byte ASCII scalar");
    AURA_CHECK(decodeUtf8(text, offset) == U'é' && offset == 3,
               "decodeUtf8 reads a 2-byte scalar");
    AURA_CHECK(decodeUtf8(text, offset) == U'€' && offset == 6,
               "decodeUtf8 reads a 3-byte scalar");
    AURA_CHECK(decodeUtf8(text, offset) == U'\U0001F600' && offset == 10,
               "decodeUtf8 reads a 4-byte scalar");
    AURA_CHECK(decodeUtf8(text, offset) == 0,
               "decodeUtf8 returns 0 at the end of the string");

    // A stray continuation byte must consume exactly one byte, so that a decode
    // loop over malformed input still terminates.
    const std::string malformed = "\x80\x41";
    std::size_t badOffset = 0;
    AURA_CHECK(decodeUtf8(malformed, badOffset) == 0xFFFDu && badOffset == 1,
               "decodeUtf8 yields U+FFFD and advances one byte on a bad lead");
    AURA_CHECK(decodeUtf8(malformed, badOffset) == U'A',
               "decodeUtf8 resynchronises after a malformed sequence");

    // A truncated multi-byte sequence at the very end must not read past it.
    const std::string truncated = "\xE2\x82";
    std::size_t truncatedOffset = 0;
    AURA_CHECK(decodeUtf8(truncated, truncatedOffset) == 0xFFFDu && truncatedOffset == 1,
               "decodeUtf8 rejects a truncated sequence without overrunning");
}

void testBitmapFallbackMetrics()
{
    auto atlas = FontAtlas::builtinBitmap(smallAtlas());
    AURA_CHECK(atlas != nullptr, "builtinBitmap always produces an atlas");
    if (!atlas) return;

    AURA_CHECK(atlas->width() == 256 && atlas->height() == 256,
               "atlas honours the requested dimensions");
    AURA_CHECK(atlas->lineHeight() > 0.0f, "bitmap fallback reports a usable line height");

    const GlyphInfo* a = atlas->glyph(U'A');
    AURA_CHECK(a != nullptr, "bitmap fallback resolves an ASCII glyph");
    if (a) {
        AURA_CHECK(a->advance > 0.0f, "'A' advances the pen");
        AURA_CHECK(a->size.x > 0.0f && a->size.y > 0.0f, "'A' occupies a non-empty cell");
        AURA_CHECK(a->uvMax.x > a->uvMin.x && a->uvMax.y > a->uvMin.y,
                   "'A' has a non-degenerate UV rectangle");
    }

    // Space is metrics-only: it moves the pen but reserves no cell, so its UVs
    // stay degenerate and the batch builder skips emitting a quad for it.
    const GlyphInfo* space = atlas->glyph(U' ');
    AURA_CHECK(space != nullptr, "space resolves");
    if (space) {
        AURA_CHECK(space->advance > 0.0f, "space advances the pen");
        AURA_CHECK(space->size.x == 0.0f || space->size.y == 0.0f,
                   "space reserves no atlas cell");
    }

    // The embedded font covers 7-bit ASCII only; anything else must fall back to
    // '?' rather than returning nullptr and dropping the character silently.
    const GlyphInfo* substitute = atlas->glyph(U'€');
    const GlyphInfo* question = atlas->glyph(U'?');
    AURA_CHECK(substitute != nullptr && question != nullptr,
               "an out-of-range codepoint falls back to a substitute glyph");
    if (substitute && question) {
        AURA_CHECK(substitute->uvMin == question->uvMin,
                   "the substitute is '?' rather than a fresh cell");
    }
}

void testGlyphCachingIsStable()
{
    auto atlas = FontAtlas::builtinBitmap(smallAtlas());
    if (!atlas) return;

    const GlyphInfo* first = atlas->glyph(U'M');
    AURA_CHECK(first != nullptr, "first request rasterizes 'M'");
    if (!first) return;

    const GlyphInfo copy = *first;

    // Re-requesting must hit the cache: same cell, no second rasterization, and
    // therefore nothing newly dirty once the first upload has been taken.
    (void)atlas->takeDirtyUpload();
    const GlyphInfo* second = atlas->glyph(U'M');
    AURA_CHECK(second != nullptr && second->uvMin == copy.uvMin && second->uvMax == copy.uvMax,
               "a cached glyph keeps its atlas cell");
    AURA_CHECK(!atlas->dirty(), "a cache hit does not dirty the atlas");
}

void testDirtyRegionTracking()
{
    auto atlas = FontAtlas::builtinBitmap(smallAtlas());
    if (!atlas) return;

    AURA_CHECK(!atlas->dirty(), "a fresh atlas has nothing pending");
    AURA_CHECK(!atlas->takeDirtyUpload().has_value(),
               "takeDirtyUpload yields nothing when nothing changed");

    const GlyphInfo* g0 = atlas->glyph(U'W');
    const GlyphInfo* g1 = atlas->glyph(U'i');
    AURA_CHECK(g0 && g1, "two distinct glyphs rasterize");
    AURA_CHECK(atlas->dirty(), "rasterizing marks the atlas dirty");

    const auto pending = atlas->takeDirtyUpload();
    AURA_CHECK(pending.has_value(), "takeDirtyUpload yields the pending rectangle");
    if (pending) {
        const auto& region = pending->region;
        AURA_CHECK(region.width > 0 && region.height > 0,
                   "the dirty rectangle is non-empty");
        AURA_CHECK(region.x + region.width <= atlas->width() &&
                   region.y + region.height <= atlas->height(),
                   "the dirty rectangle stays inside the atlas");

        // One rectangle must cover both glyphs: that union is what lets a whole
        // string's new characters travel in a single sub-image upload.
        const std::size_t expectedBytes =
            static_cast<std::size_t>(region.width) * region.height * 4u;
        AURA_CHECK(pending->rgba.size() == expectedBytes,
                   "the upload is a tightly packed RGBA8 expansion of the rectangle");

        // Coverage lives in alpha with RGB left white, so vertex colour alone
        // decides the text's colour at draw time.
        bool rgbAllWhite = true;
        bool anyCoverage = false;
        for (std::size_t i = 0; i < pending->rgba.size(); i += 4) {
            if (pending->rgba[i] != 0xFF || pending->rgba[i + 1] != 0xFF ||
                pending->rgba[i + 2] != 0xFF) {
                rgbAllWhite = false;
            }
            if (pending->rgba[i + 3] != 0) {
                anyCoverage = true;
            }
        }
        AURA_CHECK(rgbAllWhite, "the atlas stores white RGB");
        AURA_CHECK(anyCoverage, "the atlas stores glyph coverage in alpha");
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
    if (!atlas) return;

    AURA_CHECK(atlas->glyph(U'A') == nullptr,
               "a glyph that cannot fit is refused rather than overflowing");

    // Filling a small atlas with many distinct glyphs must also terminate
    // without corruption once the shelves run out.
    FontAtlasDesc small;
    small.width = 64;
    small.height = 64;
    small.pixelHeight = 24.0f;

    auto crowded = FontAtlas::builtinBitmap(small);
    if (!crowded) return;

    bool exhausted = false;
    for (char32_t cp = U'!'; cp <= U'~'; ++cp) {
        if (crowded->glyph(cp) == nullptr) {
            exhausted = true;
            break;
        }
    }
    AURA_CHECK(exhausted, "the shelf packer reports exhaustion instead of overrunning");

    const auto pending = crowded->takeDirtyUpload();
    if (pending) {
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
    if (!missing) {
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
    testUtf8Decoding();
    testBitmapFallbackMetrics();
    testGlyphCachingIsStable();
    testDirtyRegionTracking();
    testPackerRejectsOversizedAndExhaustedAtlases();
    testTrueTypeLoadFailureIsReported();

    AURA_TEST_MAIN_RETURN();
}
