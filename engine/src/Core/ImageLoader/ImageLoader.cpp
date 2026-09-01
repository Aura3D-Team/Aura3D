#include "aura/Core/ImageLoader/ImageLoader.h"

#include <algorithm>
#include <cstring>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

namespace aura3d {

ImageData ImageLoader::loadRGBA(const std::string& path)
{
    ImageData image;

    u8* decoded = stbi_load(path.c_str(), &image.width, &image.height, &image.channels, STBI_rgb_alpha);

    if (!decoded)
    {
        INK_WARN << "ImageLoader: failed to load '" << path << "': " << stbi_failure_reason();
        return image;
    }

    image.channels = 4; // STBI_rgb_alpha always yields 4, regardless of the source file
    u64 byteSize = image.getBufferSize();
    image.pixels.resize(byteSize);
    std::ranges::copy(decoded, decoded+byteSize, image.pixels.begin());

    stbi_image_free(decoded);

    INK_DEBUG << "ImageLoader: loaded '" << path << "' (" << image.width << "x" << image.height
              << ", " << image.channels << " source channels)";

    return image;
}

ImageData ImageLoader::makeCheckerboard(u32 size, u32 cells,
                                        u8 r0, u8 g0, u8 b0,
                                        u8 r1, u8 g1, u8 b1)
{
    ImageData image;
    image.width  = std::max(size, 2u);
    image.height = image.width;
    image.channels = 4;

    const u32 cellCount = std::max(cells, 1u);
    const u32 cellSize  = std::max(image.width / cellCount, 1u);

    image.pixels.resize(image.getBufferSize());

    for (u32 y = 0; y < image.height; ++y)
    {
        for (u32 x = 0; x < image.width; ++x)
        {
            const bool even = ((x / cellSize) + (y / cellSize)) % 2 == 0;
            const size_t i = (static_cast<size_t>(y) * image.width + x) * image.channels;

            image.pixels[i + 0] = even ? r0 : r1;
            image.pixels[i + 1] = even ? g0 : g1;
            image.pixels[i + 2] = even ? b0 : b1;
            image.pixels[i + 3] = 255;
        }
    }

    return image;
}

} // namespace aura3d
