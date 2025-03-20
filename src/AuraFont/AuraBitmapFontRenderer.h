#ifndef AURABITMAPFONTRENDERER_H
#define AURABITMAPFONTRENDERER_H

#pragma once

#include <string>
#include <cstdint>

#include "AuraBitmapFont.h"

namespace aura3d {

class BitmapFontRenderer {
public:
    BitmapFontRenderer() : _font(GetDefaultBitmapFont()) {}

    // Render a string to a pixel buffer
    void RenderText(const std::string& text, uint32_t* buffer, int bufferWidth, int bufferHeight,
                    int x, int y, uint32_t color = 0xFFFFFFFF) {
        int cursorX = x;

        for (char c : text) {
            if (c == '\n') {
                cursorX = x;
                y += _font.charHeight + 1;
                continue;
            }

            if (c < 0 || c > 127) c = '?'; // Replace non-ASCII with ?

            RenderCharacter(c, buffer, bufferWidth, bufferHeight, cursorX, y, color);
            cursorX += _font.charWidth + _font.charSpacing;
        }
    }

    // Calculate the width of a string in pixels
    int CalculateTextWidth(const std::string& text) {
        int width = 0;
        int maxWidth = 0;

        for (char c : text) {
            if (c == '\n') {
                maxWidth = std::max(maxWidth, width);
                width = 0;
                continue;
            }

            width += _font.charWidth + _font.charSpacing;
        }

        return std::max(maxWidth, width);
    }

    // Calculate the height of a string in pixels
    int CalculateTextHeight(const std::string& text) {
        int lines = 1;

        for (char c : text) {
            if (c == '\n') {
                lines++;
            }
        }

        return lines * (_font.charHeight + 1) - 1;
    }

private:
    void RenderCharacter(char character, int x, int y, uint32_t color) {
        if (x < 0 || y < 0 || x >= bufferWidth || y >= bufferHeight) return;

        const auto& charData = _font.data[static_cast<unsigned char>(character)];

        for (int row = 0; row < _font.charHeight; row++) {
            if (y + row >= bufferHeight) break;

            uint8_t rowBits = charData[row];

            for (int col = 0; col < _font.charWidth; col++) {
                if (x + col >= bufferWidth) break;

                // Check if the bit is set (1 = pixel on, 0 = pixel off)
                bool isPixelOn = (rowBits & (1 << (_font.charWidth - 1 - col))) != 0;

                if (isPixelOn) {
                    buffer[(y + row) * bufferWidth + (x + col)] = color;
                }
            }
        }
    }

    const AuraBitmapFont& _font;
};

} // namespace Aura

#endif // AURABITMAPFONTRENDERER_H
