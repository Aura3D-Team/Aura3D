#include "aura/Core/AuraFont/AuraBitmapFont.h"

#include <algorithm>
#include <array>
#include <bit>
#include <limits>
#include <numeric>

namespace aura3d
{
namespace
{

constexpr i32 kNone = -1;
constexpr u32 kCapRows = static_cast<u32>(AuraBitmapFont::baseline - AuraBitmapFont::bodyTop);

/// Removes lines one at a time, always the removable one whose loss erases the
/// fewest pixels, until @p target remain. @p loss is asked about a line and its
/// current neighbours (kNone past either end), so it sees earlier removals.
template <class Loss, class Removable>
[[nodiscard]] std::vector<u8> shrink(u32 count, u32 target, Loss &&loss, Removable &&removable)
{
    std::vector<u8> kept(count);
    std::iota(kept.begin(), kept.end(), u8{0});
    while (kept.size() > target)
    {
        usize best = 0;
        u32 bestLoss = std::numeric_limits<u32>::max();
        for (usize k = 0; k < kept.size(); ++k)
        {
            if (!removable(kept, kept[k]))
                continue;
            const i32 prev = k > 0 ? kept[k - 1] : kNone;
            const i32 next = k + 1 < kept.size() ? kept[k + 1] : kNone;
            const u32 lost = loss(kept[k], prev, next);
            if (lost < bestLoss)
            {
                bestLoss = lost;
                best = k;
            }
        }
        kept.erase(kept.begin() + static_cast<std::ptrdiff_t>(best));
    }
    return kept;
}

/// Repeats every line target / count times and the target % count most
/// redundant lines in [@p preferFirst, @p preferEnd) once more: doubling a line
/// that already matches a neighbour thickens nothing that was one pixel wide.
template <class Loss>
[[nodiscard]] std::vector<u8> grow(u32 count, u32 target, u32 preferFirst, u32 preferEnd, Loss &&loss)
{
    std::vector<u8> order(count);
    std::iota(order.begin(), order.end(), u8{0});
    const auto cost = [&](u8 line)
    {
        const i32 prev = line > 0 ? line - 1 : kNone;
        const i32 next = line + 1 < count ? line + 1 : kNone;
        return loss(line, prev, next);
    };
    std::ranges::stable_sort(order,
                             [&](u8 a, u8 b)
                             {
                                 const bool preferA = a >= preferFirst && a < preferEnd;
                                 const bool preferB = b >= preferFirst && b < preferEnd;
                                 return preferA != preferB ? preferA : cost(a) < cost(b);
                             });

    std::vector<u32> repeats(count, target / count);
    for (u32 i = 0; i < target % count; ++i)
        ++repeats[order[i]];

    std::vector<u8> lines;
    lines.reserve(target);
    for (u32 line = 0; line < count; ++line)
        lines.insert(lines.end(), repeats[line], static_cast<u8>(line));
    return lines;
}

/// Pixels of @p line in runs of three or more: the bars a reader sees.
[[nodiscard]] constexpr u32 bars(u32 line) noexcept
{
    const u32 centres = line & (line << 1) & (line >> 1);
    return centres | (centres << 1) | (centres >> 1);
}

/// What removing @p line between @p above and @p below erases: the pixels that
/// match neither neighbour. Two kinds cost more, because a reader relies on
/// them: a bar (the 4's crossbar, an E's arms) and a blank pixel between two
/// inked ones (an i's dot off its stem, the halves of a colon).
[[nodiscard]] constexpr u32 lostPixels(u32 line, u32 above, u32 below) noexcept
{
    constexpr u32 kBarWeight = 8;
    constexpr u32 kSeparatorWeight = 16;
    const u32 unique = (line ^ above) & (line ^ below);
    const u32 separators = ~line & above & below;
    return static_cast<u32>(std::popcount(unique)) +
           (kBarWeight - 1) * static_cast<u32>(std::popcount(unique & bars(line))) +
           (kSeparatorWeight - 1) * static_cast<u32>(std::popcount(separators));
}

[[nodiscard]] constexpr u32 roundDiv(u32 value, u32 divisor) noexcept
{
    return (value + divisor / 2) / divisor;
}

/// Source row of each output row for one glyph: every band keeps the row count
/// the shared layout gave it, and the glyph picks its own rows within it.
[[nodiscard]] std::vector<u8> glyphRows(const AuraBitmapFont::Glyph &glyph, const std::vector<u8> &shared)
{
    std::array<u32, std::tuple_size_v<AuraBitmapFont::Glyph>> repeats{};
    for (const u8 row : shared)
        ++repeats[row];

    std::vector<u8> out;
    out.reserve(shared.size());
    constexpr std::array<u32, 5> kBands{0, AuraBitmapFont::bodyTop, AuraBitmapFont::xHeightTop,
                                        AuraBitmapFont::baseline, std::tuple_size_v<AuraBitmapFont::Glyph>};
    for (usize band = 0; band + 1 < kBands.size(); ++band)
    {
        const u32 first = kBands[band];
        const u32 end = kBands[band + 1];
        const u32 size = end - first;
        u32 target = 0;
        for (u32 row = first; row < end; ++row)
            target += repeats[row];

        const auto rowAt = [&](u32 row)
        {
            return row < glyph.size() ? glyph[row] : u8{0};
        };
        //! This glyph's loss decides; the shared layout breaks ties, so glyphs
        //! alike in a band -- a stem, a blank -- all pick the same rows.
        const auto loss = [&](u8 index, i32 prev, i32 next)
        {
            const u32 row = first + index;
            const u8 above =
                prev == kNone ? (first > 0 ? rowAt(first - 1) : u8{0}) : rowAt(first + static_cast<u32>(prev));
            const u8 below = next == kNone ? rowAt(end) : rowAt(first + static_cast<u32>(next));
            const bool sharedChoice = target < size ? repeats[row] == 0 : repeats[row] > target / size;
            return lostPixels(rowAt(row), above, below) * 2 + (sharedChoice ? 0u : 1u);
        };
        const auto any = [](const std::vector<u8> &, u8)
        {
            return true;
        };

        for (const u8 index : target < size ? shrink(size, target, loss, any) : grow(size, target, 0, size, loss))
            out.push_back(static_cast<u8>(first + index));
    }
    return out;
}

/// Source column of each of @p width output columns, chosen like the rows.
[[nodiscard]] std::vector<u8> glyphColumns(const AuraBitmapFont &font, const AuraBitmapFont::Glyph &glyph,
                                           AuraBitmapFont::Ink ink, u32 width)
{
    if (ink.width == 0 || width == 0)
        return {};

    //! Column @p c as a mask over the glyph's rows.
    const auto column = [&](i32 c)
    {
        u32 mask = 0;
        if (c == kNone)
            return mask;
        for (usize r = 0; r < glyph.size(); ++r)
            mask |= font.inked(glyph[r], ink.first + c) ? 1u << r : 0u;
        return mask;
    };
    const auto loss = [&](u8 c, i32 prev, i32 next)
    {
        return lostPixels(column(c), column(prev), column(next));
    };
    const auto any = [](const std::vector<u8> &, u8)
    {
        return true;
    };

    const auto count = static_cast<u32>(ink.width);
    std::vector<u8> out = width < count ? shrink(count, width, loss, any) : grow(count, width, 0, count, loss);
    for (u8 &c : out)
        c = static_cast<u8>(c + ink.first);
    return out;
}

} // namespace

AuraBitmapFont::Layout AuraBitmapFont::layout(u32 height) const
{
    Layout out;
    if (height == 0)
        return out;

    const auto rows = static_cast<u32>(charHeight);
    //! Pixels of row @p r, over every glyph, that match neither neighbour row.
    const auto loss = [this](u8 r, i32 prev, i32 next)
    {
        u32 lost = 0;
        for (const Glyph &glyph : data)
        {
            const u8 line = glyph[r];
            const u8 above = prev == kNone ? u8{0} : glyph[static_cast<usize>(prev)];
            const u8 below = next == kNone ? u8{0} : glyph[static_cast<usize>(next)];
            lost += lostPixels(line, above, below);
        }
        return lost;
    };

    const auto descenderKept = [](const std::vector<u8> &kept, u8 row)
    {
        return row < baseline || static_cast<u32>(std::ranges::count_if(
                                     kept,
                                     [](u8 r)
                                     {
                                         return r >= baseline;
                                     })) > minDescender;
    };

    out.rows = height < rows ? shrink(rows, height, loss, descenderKept)
                             : grow(rows, height, bodyTop, static_cast<u32>(baseline), loss);
    for (const u8 row : out.rows)
    {
        out.ascent += row < baseline ? 1u : 0u;
        out.caps += row >= bodyTop && row < baseline ? 1u : 0u;
    }

    //! Widths scale with the cap height, and every advance is a whole pixel.
    const auto scaled = [&](i32 units)
    {
        return std::max(1u, roundDiv(static_cast<u32>(units) * out.caps, kCapRows));
    };
    for (usize c = 0; c < data.size(); ++c)
    {
        const Ink extent = ink(data[c]);
        const u32 width = extent.width == 0 ? 0 : scaled(extent.width);
        ScaledGlyph &glyph = out.glyphs[c];
        glyph.rows = glyphRows(data[c], out.rows);
        glyph.columns = glyphColumns(*this, data[c], extent, width);
        glyph.advance = extent.width == 0 ? scaled(charWidth / 2) : width + scaled(charSpacing);
    }
    return out;
}

} // namespace aura3d
