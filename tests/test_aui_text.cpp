/*
 * The text engine: measurement, wrapping, and the byte-offset <-> pixel
 * mapping every caret and selection is built on.
 *
 * The mapping is what gets silently wrong. A label that measures a pixel short
 * looks fine; a caret that lands inside a multi-byte character corrupts the
 * string on the next keystroke, and nothing about it fails loudly. So the
 * round trip -- byte to position and back -- is pinned here for ASCII and for
 * multi-byte input alike.
 */

#include <cmath>
#include <string>
#include <vector>

#include "aura/UI/Text/TextEngine.h"
#include "aura/UI/Text/Utf8.h"

#include "TestUtils.h"

using namespace aura3d;
using namespace aura3d::ui;

namespace {

[[nodiscard]] bool near(f32 a, f32 b, f32 tolerance = 0.51f)
{
    return std::fabs(a - b) <= tolerance;
}

void testMeasurement()
{
    AtlasTextShaper shaper;
    ShapedText shaped;

    const TextStyle style{.pixelSize = 16.0f};

    shaper.shape("", style, kUnbounded, shaped);
    AURA_CHECK(shaped.empty() && shaped.size.y > 0.0f,
               "an empty string still occupies one line box");
    AURA_CHECK(shaped.lines.size() == 1, "and reports exactly one line");

    shaper.shape("iii", style, kUnbounded, shaped);
    const f32 narrow = shaped.size.x;

    shaper.shape("WWW", style, kUnbounded, shaped);
    AURA_CHECK(shaped.size.x >= narrow, "wider glyphs measure at least as wide");

    shaper.shape("ab", style, kUnbounded, shaped);
    const f32 two = shaped.size.x;

    shaper.shape("abab", style, kUnbounded, shaped);
    AURA_CHECK(near(shaped.size.x, two * 2.0f, 2.0f), "width grows with the string");
}

void testFontSizeScales()
{
    AtlasTextShaper shaper;
    ShapedText small;
    ShapedText large;

    shaper.shape("Aura", TextStyle{.pixelSize = 12.0f}, kUnbounded, small);
    shaper.shape("Aura", TextStyle{.pixelSize = 24.0f}, kUnbounded, large);

    AURA_CHECK(large.size.x > small.size.x && large.size.y > small.size.y,
               "a bigger em size produces bigger text");
    AURA_CHECK(large.page != small.page,
               "and rasterizes on its own page rather than scaling the small one");
}

void testExplicitLineBreaks()
{
    AtlasTextShaper shaper;
    ShapedText shaped;

    shaper.shape("one\ntwo\nthree", TextStyle{}, kUnbounded, shaped);

    AURA_CHECK(shaped.lines.size() == 3, "every '\\n' starts a line");
    AURA_CHECK(near(shaped.size.y, shaped.lineHeight * 3.0f),
               "and the block is that many line heights tall");
    AURA_CHECK(shaped.lines[1].byteBegin == 4, "the second line starts after the first break");
}

void testWordWrap()
{
    AtlasTextShaper shaper;
    ShapedText unwrapped;

    const TextStyle wrapping{.pixelSize = 14.0f, .wrap = TextWrap::Word};

    shaper.shape("alpha beta gamma delta", TextStyle{.pixelSize = 14.0f}, kUnbounded, unwrapped);
    const f32 full = unwrapped.size.x;

    ShapedText shaped;
    shaper.shape("alpha beta gamma delta", wrapping, full * 0.5f, shaped);

    AURA_CHECK(shaped.lines.size() > 1, "text too wide for the limit wraps");
    AURA_CHECK(shaped.size.x <= full * 0.5f + 1.0f, "and no line exceeds the limit");

    //! Every byte of the source must still be reachable: a wrap that dropped
    //! the word it broke on would look plausible and lose data.
    AURA_CHECK(shaped.lines.back().byteEnd == 22, "the last line ends at the end of the string");
}

void testLongWordBreaks()
{
    AtlasTextShaper shaper;
    ShapedText shaped;

    const TextStyle style{.pixelSize = 14.0f, .wrap = TextWrap::Word};
    shaper.shape("supercalifragilistic", style, 30.0f, shaped);

    AURA_CHECK(shaped.lines.size() > 1,
               "a single word wider than the limit is broken rather than overflowing");
}

void testCaretRoundTrip()
{
    AtlasTextShaper shaper;
    ShapedText shaped;

    const std::string text = "hello world";
    shaper.shape(text, TextStyle{.pixelSize = 16.0f}, kUnbounded, shaped);

    bool roundTrips = true;
    for (usize byte = 0; byte <= text.size(); ++byte)
    {
        const glm::vec2 position = shaped.caretPosition(byte);
        if (shaped.byteAt(position) != byte)
            roundTrips = false;
    }

    AURA_CHECK(roundTrips, "every caret position maps back to the byte it came from");

    AURA_CHECK(shaped.byteAt({-100.0f, 0.0f}) == 0, "a click left of the text lands at the start");
    AURA_CHECK(shaped.byteAt({10000.0f, 0.0f}) == text.size(),
               "a click past the end lands at the end");
}

void testCaretNeverSplitsACharacter()
{
    //! Two-byte characters throughout, so a caret off by one byte is a caret
    //! inside a character -- the failure this whole layer exists to prevent.
    const std::string text = "éèêë";

    AtlasTextShaper shaper;
    ShapedText shaped;
    shaper.shape(text, TextStyle{.pixelSize = 16.0f}, kUnbounded, shaped);

    bool onBoundaries = true;
    for (f32 x = -5.0f; x < shaped.size.x + 5.0f; x += 0.5f)
    {
        const usize byte = shaped.byteAt({x, 0.0f});

        if (byte < text.size() && utf8::isContinuation(text[byte]))
            onBoundaries = false;
    }

    AURA_CHECK(onBoundaries, "hit testing only ever lands on a character boundary");
    AURA_CHECK(shaped.glyphs.size() == 4, "four codepoints produce four glyphs");
    AURA_CHECK(shaped.glyphs[1].cluster == 2, "and clusters carry their real byte offsets");
}

void testMultiLineCaret()
{
    AtlasTextShaper shaper;
    ShapedText shaped;

    shaper.shape("first\nsecond", TextStyle{.pixelSize = 14.0f}, kUnbounded, shaped);

    const glm::vec2 second = shaped.caretPosition(6);
    AURA_CHECK(near(second.y, shaped.lineHeight),
               "a caret on the second line sits one line down");
    AURA_CHECK(shaped.lineOf(8) == 1, "and lineOf agrees which line that is");
}

void testSelectionRects()
{
    AtlasTextShaper shaper;
    ShapedText shaped;

    shaper.shape("one\ntwo", TextStyle{.pixelSize = 14.0f}, kUnbounded, shaped);

    std::vector<Rect> rects;
    shaped.selectionRects(0, 7, rects);

    AURA_CHECK(rects.size() == 2, "a selection spanning two lines produces one rect per line");

    rects.clear();
    shaped.selectionRects(1, 2, rects);

    AURA_CHECK(rects.size() == 1 && rects[0].width() > 0.0f,
               "a one-character selection is one non-empty rect");

    rects.clear();
    shaped.selectionRects(3, 3, rects);
    AURA_CHECK(rects.empty(), "an empty selection highlights nothing");
}

void testAlignmentShiftsLines()
{
    AtlasTextShaper shaper;
    ShapedText left;
    ShapedText centred;

    const std::string text = "short\nmuch longer line";

    shaper.shape(text, TextStyle{.pixelSize = 14.0f, .align = Align::Left}, kUnbounded, left);
    shaper.shape(text, TextStyle{.pixelSize = 14.0f, .align = Align::Center}, kUnbounded, centred);

    AURA_CHECK(centred.glyphs.front().penX > left.glyphs.front().penX,
               "centring indents the short line within the block");
    AURA_CHECK(near(centred.size.x, left.size.x),
               "without changing the block's own width");
}

void testUtf8Navigation()
{
    const std::string text = "aéb cd";

    AURA_CHECK(utf8::nextBoundary(text, 1) == 3, "next boundary steps over a two-byte character");
    AURA_CHECK(utf8::previousBoundary(text, 3) == 1, "and back again");
    AURA_CHECK(utf8::nextBoundary(text, text.size()) == text.size(),
               "stepping past the end stays at the end");
    AURA_CHECK(utf8::previousBoundary(text, 0) == 0, "and before the start stays at the start");

    AURA_CHECK(utf8::nextWord(text, 0) == 5, "next word skips the run and the space after it");
    AURA_CHECK(utf8::previousWord(text, text.size()) == 5, "previous word finds its start");

    AURA_CHECK(utf8::clampToBoundary(text, 2) == 1,
               "an offset inside a character is pulled back to its start");

    std::string encoded;
    utf8::append(encoded, U'é');
    AURA_CHECK(encoded.size() == 2, "append encodes to the right number of bytes");

    utf8::append(encoded, static_cast<char32_t>(0xD800));
    AURA_CHECK(encoded.size() == 2, "and drops a surrogate rather than encoding one");
}

void testScaleKeepsLogicalMetrics()
{
    AtlasTextShaper shaper;
    ShapedText normal;
    ShapedText scaled;

    const TextStyle style{.pixelSize = 16.0f};

    shaper.shape("Aura3D", style, kUnbounded, normal);
    const glm::vec2 before = normal.size;

    shaper.setScale(2.0f);
    shaper.shape("Aura3D", style, kUnbounded, scaled);

    //! The whole point of the split: at 200% the glyphs are rasterized twice
    //! as large, and the layout does not move by a pixel.
    AURA_CHECK(near(scaled.size.x, before.x, 1.5f) && near(scaled.size.y, before.y, 1.5f),
               "raising the device-pixel ratio leaves logical metrics unchanged");
}

} // namespace

int main()
{
    testMeasurement();
    testFontSizeScales();
    testExplicitLineBreaks();
    testWordWrap();
    testLongWordBreaks();
    testCaretRoundTrip();
    testCaretNeverSplitsACharacter();
    testMultiLineCaret();
    testSelectionRects();
    testAlignmentShiftsLines();
    testUtf8Navigation();
    testScaleKeepsLogicalMetrics();

    AURA_TEST_MAIN_RETURN();
}
