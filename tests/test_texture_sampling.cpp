/*
 * Texture::sample(), the software rasteriser's texture filter.
 *
 * FontAtlas rasterizes glyphs with antialiased (grayscale) coverage -- see
 * stbtt_MakeGlyphBitmap in FontAtlas.cpp -- but a pen position is essentially
 * never on an integer pixel boundary, since it accumulates fractional glyph
 * advances. Point-sampling that coverage throws the antialiasing away the
 * moment a glyph lands off-grid, which is always, turning a soft edge back
 * into a hard, jagged one. Vulkan, OpenGL and Metal all sample their dynamic
 * textures linearly (see each backend's texture manager); this pins down that
 * the software rasteriser's sampler now agrees with them, rather than relying
 * on eyeballing rendered text for softness.
 */

#include <cstdio>

#include "aura/Renderer/Software/CpuAura/CpuFrameBufferManager.h"

#include "TestUtils.h"

using namespace aura3d;
using aura3d::cpu::Texture;

namespace {

[[nodiscard]] constexpr u32 argb(u8 a, u8 r, u8 g, u8 b) noexcept
{
    return (static_cast<u32>(a) << 24) | (static_cast<u32>(r) << 16)
         | (static_cast<u32>(g) <<  8) |  static_cast<u32>(b);
}

[[nodiscard]] u8 alphaOf(u32 texel) noexcept
{
    return static_cast<u8>((texel >> 24) & 0xFFu);
}

[[nodiscard]] u8 redOf(u32 texel) noexcept
{
    return static_cast<u8>((texel >> 16) & 0xFFu);
}

void test_texel_centres_are_exact()
{
    // A 2x2 checkerboard: sampling exactly at a texel's centre must return
    // that texel unchanged, with no blend contribution from its neighbours.
    Texture tex(2, 2);
    tex.data[0] = argb(255, 0, 0, 0);       // (0,0) black
    tex.data[1] = argb(255, 255, 255, 255); // (1,0) white
    tex.data[2] = argb(255, 255, 255, 255); // (0,1) white
    tex.data[3] = argb(255, 0, 0, 0);       // (1,1) black

    AURA_CHECK(tex.sample(0.25f, 0.25f) == argb(255, 0, 0, 0),
              "texel (0,0)'s centre samples exactly black");
    AURA_CHECK(tex.sample(0.75f, 0.25f) == argb(255, 255, 255, 255),
              "texel (1,0)'s centre samples exactly white");
}

void test_midpoint_blends_evenly()
{
    // Sampling exactly halfway between a black and a white texel must return
    // ~50% grey -- the direct signature of bilinear rather than nearest.
    Texture tex(2, 2);
    tex.data[0] = argb(255, 0, 0, 0);
    tex.data[1] = argb(255, 255, 255, 255);
    tex.data[2] = argb(255, 255, 255, 255);
    tex.data[3] = argb(255, 0, 0, 0);

    const u8 r = redOf(tex.sample(0.5f, 0.25f));
    AURA_CHECK(r >= 126 && r <= 129, "the midpoint between black and white is ~mid-grey");
}

void test_uniform_texture_is_unaffected_by_uv()
{
    // A single-colour texture must sample as that colour everywhere,
    // including UVs past [0,1] -- clamp-to-edge, not wrap or garbage reads.
    Texture tex(4, 4);
    for (u32& texel : tex.data)
        texel = argb(200, 10, 20, 30);

    const u32 expected = argb(200, 10, 20, 30);
    AURA_CHECK(tex.sample(0.5f, 0.5f) == expected, "uniform texture samples true at its centre");
    AURA_CHECK(tex.sample(0.0f, 0.0f) == expected, "uniform texture samples true at UV (0,0)");
    AURA_CHECK(tex.sample(1.0f, 1.0f) == expected, "uniform texture samples true at UV (1,1)");
    AURA_CHECK(tex.sample(-0.5f, 2.0f) == expected,
              "a UV outside [0,1] clamps to the edge rather than wrapping or reading garbage");
}

void test_offgrid_samples_across_an_edge_differ()
{
    // The whole point of this change: two samples straddling an antialiased
    // edge, at positions that do not land on a texel centre, must differ --
    // nearest-neighbour would instead collapse both to the same hard texel.
    Texture tex(4, 1);
    tex.data[0] = argb(0,   255, 255, 255);
    tex.data[1] = argb(128, 255, 255, 255); // the AA edge texel
    tex.data[2] = argb(255, 255, 255, 255);
    tex.data[3] = argb(255, 255, 255, 255);

    const u8 a1 = alphaOf(tex.sample(0.30f, 0.5f));
    const u8 a2 = alphaOf(tex.sample(0.40f, 0.5f));

    AURA_CHECK(a1 != a2, "adjacent off-grid samples straddling an AA edge are not identical");
}

} // namespace

int main()
{
    test_texel_centres_are_exact();
    test_midpoint_blends_evenly();
    test_uniform_texture_is_unaffected_by_uv();
    test_offgrid_samples_across_an_edge_differ();

    AURA_TEST_MAIN_RETURN();
}
