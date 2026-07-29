#include "aura/Core/ImageLoader/ImageLoader.h"

#include <array>
#include <string>

#include "TestUtils.h"

using namespace aura3d;

namespace {

std::string assetPath(const std::string& name)
{
    return std::string(AURA_TEST_ASSETS_DIR) + "/" + name;
}

// tests/assets/valid_texture.png is a hand-built 2x2 RGBA PNG (see
// scripts used to generate it) with one exact, known color per texel,
// row-major top-to-bottom: (0,0)=red (1,0)=green / (0,1)=blue (1,1)=white.
std::array<u8, 4> pixelAt(const ImageData& image, u32 x, u32 y)
{
    const size_t i = (static_cast<size_t>(y) * image.width + x) * 4;
    return {image.pixels[i], image.pixels[i + 1], image.pixels[i + 2], image.pixels[i + 3]};
}

void test_load_valid_png()
{
    const ImageData image = ImageLoader::loadRGBA(assetPath("valid_texture.png"));

    AURA_CHECK(image.valid(), "loadRGBA: valid_texture.png decodes to a valid ImageData");
    AURA_CHECK(image.width == 2 && image.height == 2, "loadRGBA: valid_texture.png is 2x2");
    AURA_CHECK(image.pixels.size() == 2u * 2u * 4u, "loadRGBA: pixel buffer is width*height*4 bytes");

    AURA_CHECK((pixelAt(image, 0, 0) == std::array<u8, 4>{255, 0, 0, 255}), "loadRGBA: (0,0) decodes to red");
    AURA_CHECK((pixelAt(image, 1, 0) == std::array<u8, 4>{0, 255, 0, 255}), "loadRGBA: (1,0) decodes to green");
    AURA_CHECK((pixelAt(image, 0, 1) == std::array<u8, 4>{0, 0, 255, 255}), "loadRGBA: (0,1) decodes to blue");
    AURA_CHECK((pixelAt(image, 1, 1) == std::array<u8, 4>{255, 255, 255, 255}), "loadRGBA: (1,1) decodes to white");
}

void test_load_corrupt_and_missing()
{
    const ImageData corrupt = ImageLoader::loadRGBA(assetPath("corrupt_texture.png"));
    AURA_CHECK(!corrupt.valid(), "loadRGBA: corrupt_texture.png is reported invalid, not silently accepted");
    AURA_CHECK(corrupt.pixels.empty(), "loadRGBA: corrupt_texture.png yields no pixel data");

    const ImageData missing = ImageLoader::loadRGBA(assetPath("does_not_exist.png"));
    AURA_CHECK(!missing.valid(), "loadRGBA: a missing file is reported invalid");
}

void test_make_checkerboard_defaults()
{
    const ImageData image = ImageLoader::makeCheckerboard();

    AURA_CHECK(image.valid(), "makeCheckerboard: default output is a valid ImageData");
    AURA_CHECK(image.width == 64 && image.height == 64, "makeCheckerboard: defaults to 64x64");

    // Default fallback colors: opaque magenta (r0,g0,b0), then black (r1,g1,b1).
    AURA_CHECK((pixelAt(image, 0, 0) == std::array<u8, 4>{255, 0, 255, 255}),
              "makeCheckerboard: top-left texel is opaque magenta by default");

    // 64px / 8 cells = 8px per cell, so the cell at x=8 must have flipped color.
    AURA_CHECK((pixelAt(image, 8, 0) == std::array<u8, 4>{0, 0, 0, 255}),
              "makeCheckerboard: the adjacent cell alternates to the second color");

    bool allOpaque = true;
    for (size_t i = 3; i < image.pixels.size(); i += 4) {
        if (image.pixels[i] != 255) { allOpaque = false; break; }
    }
    AURA_CHECK(allOpaque, "makeCheckerboard: alpha channel is fully opaque everywhere");
}

void test_make_checkerboard_clamping()
{
    // size=0/cells=0 are degenerate inputs; the implementation is documented
    // to clamp rather than divide by zero or return an empty image.
    const ImageData image = ImageLoader::makeCheckerboard(0, 0);

    AURA_CHECK(image.valid(), "makeCheckerboard: size=0/cells=0 still produces a valid image");
    AURA_CHECK(image.width >= 2 && image.height >= 2, "makeCheckerboard: size is clamped to at least 2");
}

void test_make_checkerboard_custom_colors()
{
    const ImageData image = ImageLoader::makeCheckerboard(16, 4, 10, 20, 30, 200, 210, 220);

    AURA_CHECK(image.width == 16 && image.height == 16, "makeCheckerboard: honors an explicit size");
    AURA_CHECK((pixelAt(image, 0, 0) == std::array<u8, 4>{10, 20, 30, 255}),
              "makeCheckerboard: honors a custom first color");
}

} // namespace

int main()
{
    test_load_valid_png();
    test_load_corrupt_and_missing();
    test_make_checkerboard_defaults();
    test_make_checkerboard_clamping();
    test_make_checkerboard_custom_colors();
    AURA_TEST_MAIN_RETURN();
}
