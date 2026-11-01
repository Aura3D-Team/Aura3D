#ifndef AURA_RENDERER_TEXTURE_H
#define AURA_RENDERER_TEXTURE_H

#pragma once

#include <ink/ink_base.hpp>

namespace aura3d
{

enum class TextureFormat : u8
{
    Rgba8Unorm,  //!< Sampled as stored: normal, roughness and other data maps.
    Rgba8Srgb,   //!< sRGB-encoded colour, decoded to linear when sampled: photos, albedo.
    Rgba16Float, //!< IEEE half floats, four per texel: HDR images. The software backend clamps them to [0, 1].
    Depth, //!< A render target's depth alone, sampled as depth (nearest, clamped); the backend's format, 24 bits or
           //!< more.
};

enum class TextureFilter : u8
{
    Nearest,
    Linear,
};

enum class TextureWrap : u8
{
    Repeat,
    MirroredRepeat,
    ClampToEdge,
};

struct SamplerDesc
{
    TextureFilter magFilter = TextureFilter::Linear;
    TextureFilter minFilter = TextureFilter::Linear;
    //! Between mip levels; the software backend picks one level per triangle whatever this says.
    TextureFilter mipFilter = TextureFilter::Linear;
    TextureWrap wrapU = TextureWrap::Repeat;
    TextureWrap wrapV = TextureWrap::Repeat;
    f32 maxAnisotropy = 1.0f; //!< 1 is off; clamped to what the device supports.
};

struct TextureDesc
{
    u32 width = 0;
    u32 height = 0;
    TextureFormat format = TextureFormat::Rgba8Srgb;
    bool mipmaps = true; //!< A full chain generated from level 0.
    SamplerDesc sampler{};
};

/// An offscreen colour buffer, with its own depth buffer when @p depth: drawn between
/// IRenderer::beginRenderPass(target) and endRenderPass(), then sampled as a texture. With TextureFormat::Depth there
/// is no colour at all: the target is a depth map, and renderTargetTexture() is that depth.
struct RenderTargetDesc
{
    u32 width = 0;
    u32 height = 0;
    TextureFormat format = TextureFormat::Rgba8Srgb;
    bool depth = true;
};

[[nodiscard]] u32 bytesPerTexel(TextureFormat format) noexcept;
/// IEEE binary16 to float, subnormals and infinities included: what Rgba16Float stores.
[[nodiscard]] f32 halfToFloat(u16 half) noexcept;
/// Levels in a full chain down to 1x1: floor(log2(max(width, height))) + 1; 0 when either side is 0.
[[nodiscard]] u32 mipLevelCount(u32 width, u32 height) noexcept;
/// The rectangle at (@p x, @p y) of @p width x @p height lies inside a @p textureWidth x @p textureHeight texture,
/// without overflowing u32 on the way. An empty rectangle on the far edge fits.
[[nodiscard]] bool regionFits(u32 x, u32 y, u32 width, u32 height, u32 textureWidth, u32 textureHeight) noexcept;

} // namespace aura3d

#endif // AURA_RENDERER_TEXTURE_H
