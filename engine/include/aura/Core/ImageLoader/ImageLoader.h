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
    std::vector<u8> pixels; //! width * height * 4 bytes, RGBA order.
    u32 width  = 0;
    u32 height = 0;

    [[nodiscard]]
    bool valid() const noexcept
    {
        return width > 0 && height > 0 &&
               pixels.size() == static_cast<size_t>(width) * height * 4u;
    }
};

/**
 * @class ImageLoader
 * @brief Decodes image files to RGBA, with an always-available fallback.
 *
 * Backed by stb_image, so PNG/JPEG/TGA/BMP/PSD/GIF all work. The decoder is an
 * implementation detail: this header stays free of third-party includes so it
 * can remain part of the installed public API.
 */
class ImageLoader {
public:
    /**
     * @brief Decodes @p path to 8-bit RGBA.
     *
     * @return The decoded image, or an invalid ImageData when the file is
     *         missing or corrupt. Callers that want the "missing texture" look
     *         should fall back to makeCheckerboard().
     */
    static ImageData loadRGBA(const std::string& path);

    /**
     * @brief Builds a @p size x @p size checkerboard.
     *
     * Defaults to the industry-standard magenta/black "missing texture"
     * indicator. Generated in memory, so it is available on every platform
     * regardless of file system access.
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
