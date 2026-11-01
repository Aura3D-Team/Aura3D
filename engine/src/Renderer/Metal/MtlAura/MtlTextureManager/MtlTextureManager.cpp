#include "aura/Renderer/Metal/MtlAura/MtlTextureManager/MtlTextureManager.h"

#include <algorithm>
#include <vector>

#include <ink/ink.hpp>

#include "aura/Core/AuraException/AuraException.h"
#include "aura/aura.h"

namespace aura3d
{
namespace mtl
{

namespace
{

//! Bytes one row of an RGBA8 image occupies.
[[nodiscard]] constexpr NS::UInteger rowBytes(u32 width) noexcept
{
    return static_cast<NS::UInteger>(width) * 4u;
}

[[nodiscard]] MTL::PixelFormat metalFormat(TextureFormat format) noexcept
{
    switch (format)
    {
    case TextureFormat::Rgba8Unorm:
        return MTL::PixelFormatRGBA8Unorm;
    case TextureFormat::Rgba8Srgb:
        return MTL::PixelFormatRGBA8Unorm_sRGB;
    case TextureFormat::Rgba16Float:
        return MTL::PixelFormatRGBA16Float;
    case TextureFormat::Depth: //! Rejected before it gets here: Metal has no depth-only targets yet.
        break;
    }
    return MTL::PixelFormatRGBA8Unorm;
}

[[nodiscard]] MTL::SamplerAddressMode metalWrap(TextureWrap wrap) noexcept
{
    switch (wrap)
    {
    case TextureWrap::Repeat:
        return MTL::SamplerAddressModeRepeat;
    case TextureWrap::MirroredRepeat:
        return MTL::SamplerAddressModeMirrorRepeat;
    case TextureWrap::ClampToEdge:
        return MTL::SamplerAddressModeClampToEdge;
    }
    return MTL::SamplerAddressModeRepeat;
}

[[nodiscard]] MTL::SamplerMinMagFilter metalFilter(TextureFilter filter) noexcept
{
    return filter == TextureFilter::Nearest ? MTL::SamplerMinMagFilterNearest : MTL::SamplerMinMagFilterLinear;
}

} // namespace

MtlTextureManager::MtlTextureManager(MTL::Device *device) : _device(device)
{
    if (!_device)
        throw AuraException("MtlTextureManager: device is null");

    NS::SharedPtr<MTL::SamplerDescriptor> descriptor = adopt(MTL::SamplerDescriptor::alloc()->init());

    const NS::SharedPtr<NS::String> label = makeString("Aura3D albedo sampler");
    descriptor->setLabel(label.get());
    descriptor->setMinFilter(MTL::SamplerMinMagFilterLinear);
    descriptor->setMagFilter(MTL::SamplerMinMagFilterLinear);
    /*
     * No mip chain is generated for uploaded textures, so asking for mip
     * filtering would sample a level that does not exist. NotMipmapped says so
     * explicitly rather than relying on the mipmapLevelCount of 1 to make the
     * choice moot.
     */
    descriptor->setMipFilter(MTL::SamplerMipFilterNotMipmapped);
    /*
     * Clamp rather than repeat: a glyph atlas sampled at its edge must not wrap
     * around and pull in the neighbouring glyph's coverage.
     */
    descriptor->setSAddressMode(MTL::SamplerAddressModeClampToEdge);
    descriptor->setTAddressMode(MTL::SamplerAddressModeClampToEdge);

    _sampler = adopt(_device->newSamplerState(descriptor.get()));
    if (!_sampler)
        throw AuraException("MtlTextureManager: failed to create the shared sampler state");
}

MtlTextureManager::~MtlTextureManager()
{
    clear();
    _sampler.reset();
    _device = nullptr;

    INK_TRACE << "MtlTextureManager destroyed";
}

NS::SharedPtr<MTL::Texture> MtlTextureManager::allocate(u32 width, u32 height, const char *label,
                                                        bool coverageOnly) const
{
    if (width == 0 || height == 0)
    {
        INK_ERROR << "MtlTextureManager: refusing a " << width << "x" << height << " texture";
        return nullptr;
    }

    NS::SharedPtr<MTL::TextureDescriptor> descriptor = adopt(MTL::TextureDescriptor::alloc()->init());

    descriptor->setTextureType(MTL::TextureType2D);
    descriptor->setPixelFormat(coverageOnly ? MTL::PixelFormatR8Unorm : kTextureFormat);
    if (coverageOnly)
        descriptor->setSwizzle(MTL::TextureSwizzleChannels::Make(MTL::TextureSwizzleOne, MTL::TextureSwizzleOne,
                                                                 MTL::TextureSwizzleOne, MTL::TextureSwizzleRed));
    descriptor->setWidth(width);
    descriptor->setHeight(height);
    descriptor->setMipmapLevelCount(1);
    descriptor->setSampleCount(1);
    descriptor->setUsage(MTL::TextureUsageShaderRead);
    /*
     * Shared storage so replaceRegion() can write straight into the texture's
     * backing store. The alternative -- private storage filled through a blit
     * encoder from a staging buffer -- would be faster to sample on a discrete
     * GPU, but it also turns every updateTextureRegion() into a command buffer
     * that has to be submitted and (for a glyph atlas the same frame samples)
     * waited on. Shared keeps the atlas path a plain memcpy.
     */
    descriptor->setStorageMode(MTL::StorageModeShared);

    NS::SharedPtr<MTL::Texture> texture = adopt(_device->newTexture(descriptor.get()));
    if (!texture)
    {
        INK_ERROR << "MtlTextureManager: the device refused a " << width << "x" << height << " texture allocation";
        return nullptr;
    }

    const NS::SharedPtr<NS::String> textureLabel = makeString(label);
    texture->setLabel(textureLabel.get());

    return texture;
}

TextureHandle MtlTextureManager::createFromPixels(const u8 *rgbaPixels, u32 width, u32 height)
{
    if (!rgbaPixels)
    {
        INK_ERROR << "MtlTextureManager: createFromPixels was given a null pixel pointer";
        return {};
    }

    NS::SharedPtr<MTL::Texture> texture = allocate(width, height, "Aura3D texture");
    if (!texture)
        return {};

    texture->replaceRegion(MTL::Region::Make2D(0, 0, width, height), 0, rgbaPixels, rowBytes(width));

    _textures.push_back(std::move(texture));
    return static_cast<TextureHandle>(_textures.size()); // 1-based
}

TextureHandle MtlTextureManager::createTexture(const TextureDesc &desc, std::span<const std::byte> pixels,
                                               MTL::CommandQueue *queue)
{
    const u32 width = desc.width, height = desc.height;
    const NS::UInteger stride = static_cast<NS::UInteger>(width) * bytesPerTexel(desc.format);
    if (width == 0 || height == 0 || pixels.size() != static_cast<size_t>(stride) * height)
    {
        INK_ERROR << "MtlTextureManager: createTexture pixels do not match the descriptor's size and format";
        return {};
    }
    const u32 levels = desc.mipmaps && queue ? mipLevelCount(width, height) : 1u;

    NS::SharedPtr<MTL::TextureDescriptor> descriptor = adopt(MTL::TextureDescriptor::alloc()->init());
    descriptor->setTextureType(MTL::TextureType2D);
    descriptor->setPixelFormat(metalFormat(desc.format));
    descriptor->setWidth(width);
    descriptor->setHeight(height);
    descriptor->setMipmapLevelCount(levels);
    descriptor->setSampleCount(1);
    descriptor->setUsage(MTL::TextureUsageShaderRead);
    descriptor->setStorageMode(MTL::StorageModeShared);
    NS::SharedPtr<MTL::Texture> texture = adopt(_device->newTexture(descriptor.get()));
    if (!texture)
    {
        INK_ERROR << "MtlTextureManager: the device refused a " << width << "x" << height << " texture allocation";
        return {};
    }
    const NS::SharedPtr<NS::String> textureLabel = makeString("Aura3D texture");
    texture->setLabel(textureLabel.get());
    texture->replaceRegion(MTL::Region::Make2D(0, 0, width, height), 0, pixels.data(), stride);

    if (levels > 1)
    {
        //! Waited for: the texture is sampled by the next frame, and creation is a load-time cost.
        MTL::CommandBuffer *commands = queue->commandBuffer();
        MTL::BlitCommandEncoder *blit = commands->blitCommandEncoder();
        blit->generateMipmaps(texture.get());
        blit->endEncoding();
        commands->commit();
        commands->waitUntilCompleted();
    }

    const SamplerDesc &sampler = desc.sampler;
    NS::SharedPtr<MTL::SamplerDescriptor> samplerDescriptor = adopt(MTL::SamplerDescriptor::alloc()->init());
    samplerDescriptor->setMinFilter(metalFilter(sampler.minFilter));
    samplerDescriptor->setMagFilter(metalFilter(sampler.magFilter));
    samplerDescriptor->setMipFilter(levels == 1                                  ? MTL::SamplerMipFilterNotMipmapped
                                    : sampler.mipFilter == TextureFilter::Linear ? MTL::SamplerMipFilterLinear
                                                                                 : MTL::SamplerMipFilterNearest);
    samplerDescriptor->setSAddressMode(metalWrap(sampler.wrapU));
    samplerDescriptor->setTAddressMode(metalWrap(sampler.wrapV));
    //! Metal accepts 1 to 16.
    samplerDescriptor->setMaxAnisotropy(static_cast<NS::UInteger>(std::clamp(sampler.maxAnisotropy, 1.0f, 16.0f)));
    NS::SharedPtr<MTL::SamplerState> state = adopt(_device->newSamplerState(samplerDescriptor.get()));
    if (!state)
    {
        INK_ERROR << "MtlTextureManager: failed to create a sampler state";
        return {};
    }

    _textures.push_back(std::move(texture));
    _samplers.resize(_textures.size());
    _samplers.back() = std::move(state);
    return static_cast<TextureHandle>(_textures.size());
}

TextureHandle MtlTextureManager::createAttachment(u32 width, u32 height, TextureFormat format, TextureHandle replace)
{
    if (width == 0 || height == 0)
        return {};
    NS::SharedPtr<MTL::TextureDescriptor> descriptor = adopt(MTL::TextureDescriptor::alloc()->init());
    descriptor->setTextureType(MTL::TextureType2D);
    descriptor->setPixelFormat(metalFormat(format));
    descriptor->setWidth(width);
    descriptor->setHeight(height);
    descriptor->setMipmapLevelCount(1);
    descriptor->setUsage(MTL::TextureUsageShaderRead | MTL::TextureUsageRenderTarget);
    descriptor->setStorageMode(MTL::StorageModePrivate);
    NS::SharedPtr<MTL::Texture> texture = adopt(_device->newTexture(descriptor.get()));
    if (!texture)
    {
        INK_ERROR << "MtlTextureManager: the device refused a " << width << "x" << height << " render target";
        return {};
    }
    const NS::SharedPtr<NS::String> label = makeString("Aura3D render target");
    texture->setLabel(label.get());
    //! Metal retains the old texture for the command buffers that still use it.
    if (isValidHandle(replace) && replace.value() <= _textures.size())
    {
        _textures[replace.value() - 1] = std::move(texture);
        return replace;
    }
    _textures.push_back(std::move(texture));
    _samplers.resize(_textures.size());
    return static_cast<TextureHandle>(_textures.size());
}

MTL::SamplerState *MtlTextureManager::samplerFor(TextureHandle handle) const noexcept
{
    if (isValidHandle(handle) && handle.value() <= _samplers.size())
        if (MTL::SamplerState *own = _samplers[handle.value() - 1].get())
            return own;
    return _sampler.get();
}

TextureHandle MtlTextureManager::createDynamic(u32 width, u32 height)
{
    NS::SharedPtr<MTL::Texture> texture = allocate(width, height, "Aura3D dynamic texture");
    if (!texture)
        return {};

    /*
     * Metal leaves a fresh texture's contents undefined, while IRenderer promises
     * transparent black -- a glyph atlas that starts as uninitialised memory
     * shows garbage in the gaps between glyphs. One zeroed upload at creation is
     * what makes the promise true.
     */
    const std::vector<u8> zeros(static_cast<size_t>(width) * height * 4u, 0u);
    texture->replaceRegion(MTL::Region::Make2D(0, 0, width, height), 0, zeros.data(), rowBytes(width));

    _textures.push_back(std::move(texture));
    return static_cast<TextureHandle>(_textures.size()); // 1-based
}

bool MtlTextureManager::updateRegion(TextureHandle handle, u32 x, u32 y, u32 width, u32 height, const u8 *rgbaPixels)
{
    return updatePixels(handle, x, y, width, height, rgbaPixels, false);
}

TextureHandle MtlTextureManager::createCoverage(u32 width, u32 height)
{
    NS::SharedPtr<MTL::Texture> texture = allocate(width, height, "Aura3D coverage texture", true);
    if (!texture)
        return {};

    const std::vector<u8> zeros(static_cast<size_t>(width) * height, 0u);
    texture->replaceRegion(MTL::Region::Make2D(0, 0, width, height), 0, zeros.data(), width);
    _textures.push_back(std::move(texture));
    return static_cast<TextureHandle>(_textures.size());
}

bool MtlTextureManager::updateCoverageRegion(TextureHandle handle, u32 x, u32 y, u32 width, u32 height,
                                             const u8 *coverage)
{
    return updatePixels(handle, x, y, width, height, coverage, true);
}

bool MtlTextureManager::updatePixels(TextureHandle handle, u32 x, u32 y, u32 width, u32 height, const u8 *pixels,
                                     bool coverageOnly)
{
    MTL::Texture *texture = resolve(handle);
    if (!texture)
    {
        INK_WARN << "MtlTextureManager: updateRegion on unknown texture handle " << handle;
        return false;
    }

    if (!pixels)
    {
        INK_ERROR << "MtlTextureManager: updateRegion was given a null pixel pointer";
        return false;
    }

    //! RGBA8 bytes fit either 8-bit colour format; a mip chain would go stale.
    const MTL::PixelFormat format = texture->pixelFormat();
    const bool colour = format == kTextureFormat || format == MTL::PixelFormatRGBA8Unorm_sRGB;
    if ((coverageOnly ? format != MTL::PixelFormatR8Unorm : !colour) || texture->mipmapLevelCount() > 1)
    {
        INK_ERROR << "MtlTextureManager: update format does not match texture format, or the texture is mipmapped";
        return false;
    }

    if (width == 0 || height == 0)
    {
        INK_WARN << "MtlTextureManager: updateRegion asked for an empty " << width << "x" << height << " rectangle";
        return false;
    }

    /*
     * Rejected rather than clamped, as IRenderer::updateTextureRegion()
     * specifies. Metal's own reaction to an out-of-bounds region is an assertion
     * that kills the process, so this check is what keeps a bad glyph rectangle a
     * logged warning.
     */
    if (!regionFits(x, y, width, height, static_cast<u32>(texture->width()), static_cast<u32>(texture->height())))
    {
        INK_WARN << "MtlTextureManager: updateRegion rectangle (" << x << ", " << y << ", " << width << ", " << height
                 << ") reaches outside the " << texture->width() << "x" << texture->height() << " texture";
        return false;
    }

    texture->replaceRegion(MTL::Region::Make2D(x, y, width, height), 0, pixels,
                           coverageOnly ? static_cast<NS::UInteger>(width) : rowBytes(width));
    return true;
}

MTL::Texture *MtlTextureManager::resolve(TextureHandle handle) const noexcept
{
    if (!isValidHandle(handle))
        return nullptr;

    const size_t index = static_cast<size_t>(handle.value()) - 1;
    if (index >= _textures.size())
        return nullptr;

    return _textures[index].get();
}

void MtlTextureManager::clear() noexcept
{
    _textures.clear();
    _samplers.clear();
}

} // namespace mtl
} // namespace aura3d
