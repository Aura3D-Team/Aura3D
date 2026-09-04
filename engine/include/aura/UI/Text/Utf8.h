#ifndef AURA_UI_UTF8_H
#define AURA_UI_UTF8_H

#pragma once

#include <string>
#include <string_view>

#include <ink/ink_base.hpp>

/**
 * @file Utf8.h
 * @brief Byte-offset navigation over UTF-8, for carets and selections.
 *
 * A caret lives at a byte offset, and every editing operation is a question
 * about the offsets around it. Doing that arithmetic per widget is how a text
 * field ends up able to split a multi-byte character in half.
 *
 * Decoding lives in FontAtlas (@c aura3d::decodeUtf8); this is only the
 * boundary walk, which needs no font.
 */

namespace aura3d::ui::utf8 {

[[nodiscard]] constexpr bool isContinuation(char byte) noexcept
{
    return (static_cast<u8>(byte) & 0xC0u) == 0x80u;
}

/// Start of the character before @p offset, or 0.
[[nodiscard]] constexpr usize previousBoundary(std::string_view text, usize offset) noexcept
{
    if (offset == 0)
        return 0;

    --offset;
    while (offset > 0 && isContinuation(text[offset]))
        --offset;

    return offset;
}

/// Start of the character after @p offset, or the end of @p text.
[[nodiscard]] constexpr usize nextBoundary(std::string_view text, usize offset) noexcept
{
    const usize size = text.size();
    if (offset >= size)
        return size;

    ++offset;
    while (offset < size && isContinuation(text[offset]))
        ++offset;

    return offset;
}

[[nodiscard]] constexpr bool isWordSeparator(char c) noexcept
{
    return c == ' ' || c == '\t' || c == '\n';
}

/// Start of the word before @p offset -- Ctrl+Left.
[[nodiscard]] constexpr usize previousWord(std::string_view text, usize offset) noexcept
{
    while (offset > 0 && isWordSeparator(text[previousBoundary(text, offset)]))
        offset = previousBoundary(text, offset);

    while (offset > 0 && !isWordSeparator(text[previousBoundary(text, offset)]))
        offset = previousBoundary(text, offset);

    return offset;
}

/// Start of the word after @p offset -- Ctrl+Right.
[[nodiscard]] constexpr usize nextWord(std::string_view text, usize offset) noexcept
{
    const usize size = text.size();

    while (offset < size && !isWordSeparator(text[offset]))
        offset = nextBoundary(text, offset);

    while (offset < size && isWordSeparator(text[offset]))
        offset = nextBoundary(text, offset);

    return offset;
}

/// Appends @p codepoint as UTF-8. Surrogates and out-of-range values are
/// dropped rather than encoded, so @p out stays valid UTF-8 whatever it is fed.
void append(std::string& out, char32_t codepoint);

/// @p offset moved to the nearest character boundary at or below it. What a
/// caret restored from outside (a saved position, a clamp) must pass through.
[[nodiscard]] constexpr usize clampToBoundary(std::string_view text, usize offset) noexcept
{
    if (offset >= text.size())
        return text.size();

    while (offset > 0 && isContinuation(text[offset]))
        --offset;

    return offset;
}

} // namespace aura3d::ui::utf8

#endif // AURA_UI_UTF8_H
