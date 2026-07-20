#include "aura/Core/ImageLoader/ImageLoader.h"

#include <algorithm>
#include <cstring>

// Single translation unit that materialises stb_image. Keeping the
// implementation here means the rest of the engine only ever sees ImageData.
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

namespace aura3d {

ImageData ImageLoader::loadRGBA(const std::string& path)
{
    ImageData image;

    int width = 0;
    int height = 0;
    int channelsInFile = 0;

    // Force 4 channels so every decoded image reaches the backends in the same
    // RGBA8 layout, whatever the source format stored.
    stbi_uc* decoded = stbi_load(path.c_str(), &width, &height, &channelsInFile, STBI_rgb_alpha);

    if (!decoded) 
    {
        INK_WARN << "ImageLoader: failed to load '" << path << "': " << stbi_failure_reason();
        return image;
    }

    if (width <= 0 || height <= 0) 
    {
        INK_WARN << "ImageLoader: '" << path << "' decoded to an empty image";
        stbi_image_free(decoded);
        return image;
    }

    image.width  = static_cast<u32>(width);
    image.height = static_cast<u32>(height);

    const size_t byteCount = static_cast<size_t>(width) * static_cast<size_t>(height) * 4;
    image.pixels.resize(byteCount);
    std::memcpy(image.pixels.data(), decoded, byteCount);

    stbi_image_free(decoded);

    INK_DEBUG << "ImageLoader: loaded '" << path << "' (" << image.width << "x" << image.height
              << ", " << channelsInFile << " source channels)";

    return image;
}

ImageData ImageLoader::makeCheckerboard(u32 size, u32 cells,
                                        u8 r0, u8 g0, u8 b0,
                                        u8 r1, u8 g1, u8 b1)
{
    ImageData image;
    image.width  = std::max(size, 2);
    image.height = image.width;

    const u32 cellCount = std::max(cells, 1);
    const u32 cellSize  = std::max(image.width / cellCount, 1);

    image.pixels.resize(static_cast<size_t>(image.width) * image.height * 4);

    for (u32 y = 0; y < image.height; ++y) 
    {
        for (u32 x = 0; x < image.width; ++x) 
        {
            const bool even = ((x / cellSize) + (y / cellSize)) % 2 == 0;
            const size_t i = (static_cast<size_t>(y) * image.width + x) * 4;

            image.pixels[i + 0] = even ? r0 : r1;
            image.pixels[i + 1] = even ? g0 : g1;
            image.pixels[i + 2] = even ? b0 : b1;
            image.pixels[i + 3] = 255;
        }
    }

    return image;
}

} // namespace aura3d
