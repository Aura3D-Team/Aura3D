#ifndef AURA_IMAGE_LOADER_H
#define AURA_IMAGE_LOADER_H

#pragma once

#include <string>
#include <vector>

#include "aura/aura.h"

namespace aura3d {

/**
 * @struct ImageData
 * @brief Decoded, tightly packed 8-bit RGBA pixels.
 */
struct ImageData {
    std::vector<u8> pixels = {};
    i32 width = 0;
    i32 height = 0;
    i32 channels = 0;

    [[nodiscard]] 
    const u64 getBufferSize() const noexcept {
        return static_cast<u64>(width) * height * channels;
    }

    [[nodiscard]]
    bool valid() const noexcept
    {
        return width > 0 && height > 0 &&
               pixels.size() == static_cast<u64>(width) * height * channels;
    }
};

/**
 * @class ImageLoader
 * @brief Decodes image files to RGBA (PNG/JPEG/TGA/BMP/PSD/GIF, via stb_image).
 */
class ImageLoader {
public:
    /// Decodes @p path to 8-bit RGBA. Returns an invalid ImageData if the file
    /// is missing or corrupt; see makeCheckerboard() for a visible fallback.
    static ImageData loadRGBA(const std::string& path);

    /**
     * @brief Builds a @p size x @p size checkerboard (default: magenta/black,
     *        the standard "missing texture" look).
     *
     * @param size  Edge length in pixels; clamped to at least 2.
     * @param cells Number of squares along each edge.
     */
    static ImageData makeCheckerboard(u32 size = 64,
                                      u32 cells = 8,
                                      u8 r0 = 255, u8 g0 = 0,   u8 b0 = 255,
                                      u8 r1 = 0,   u8 g1 = 0,   u8 b1 = 0);
};

} // namespace aura3d

#endif // AURA_IMAGE_LOADER_H
