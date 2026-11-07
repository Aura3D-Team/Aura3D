#include "aura/Renderer/Texture.h"

#include <algorithm>
#include <bit>
#include <cmath>

namespace aura3d
{

u32 bytesPerTexel(TextureFormat format) noexcept
{
    return format == TextureFormat::Rgba16Float ? 8u : 4u;
}

f32 halfToFloat(u16 half) noexcept
{
    const u32 bits = half;
    const u32 sign = (bits & 0x8000u) << 16u;
    const u32 exponent = (bits >> 10u) & 0x1Fu;
    const u32 mantissa = bits & 0x3FFu;
    if (exponent == 0)
    {
        const f32 subnormal = std::ldexp(static_cast<f32>(mantissa), -24);
        return sign != 0 ? -subnormal : subnormal;
    }
    if (exponent == 31)
        return std::bit_cast<f32>(sign | 0x7F800000u | (mantissa << 13u));
    return std::bit_cast<f32>(sign | ((exponent + 112u) << 23u) | (mantissa << 13u));
}

u32 mipLevelCount(u32 width, u32 height) noexcept
{
    return width == 0 || height == 0 ? 0u : static_cast<u32>(std::bit_width(std::max(width, height)));
}

bool regionFits(u32 x, u32 y, u32 width, u32 height, u32 textureWidth, u32 textureHeight) noexcept
{
    //! Subtracted, not added: x + width can wrap past the texture.
    return x <= textureWidth && y <= textureHeight && width <= textureWidth - x && height <= textureHeight - y;
}

} // namespace aura3d
