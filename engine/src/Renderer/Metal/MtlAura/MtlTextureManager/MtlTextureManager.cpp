#include "aura/Renderer/Metal/MtlAura/MtlTextureManager/MtlTextureManager.h"

#include <vector>

#include <ink/ink.hpp>

#include "aura/aura.h"
#include "aura/Core/AuraException/AuraException.h"

namespace aura3d {
namespace mtl {

namespace {

//! Bytes one row of an RGBA8 image occupies.
[[nodiscard]] constexpr NS::UInteger rowBytes(u32 width) noexcept
{
    return static_cast<NS::UInteger>(width) * 4u;
}

} // namespace

MtlTextureManager::MtlTextureManager(MTL::Device* device)
    : _device(device)
{
    if (!_device)
        throw AuraException("MtlTextureManager: device is null");

    NS::SharedPtr<MTL::SamplerDescriptor> descriptor =
        adopt(MTL::SamplerDescriptor::alloc()->init());

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

    INK_DEBUG << "MtlTextureManager destroyed";
}

NS::SharedPtr<MTL::Texture> MtlTextureManager::allocate(u32 width, u32 height,
                                                       const char* label) const
{
    if (width == 0 || height == 0) {
        INK_ERROR << "MtlTextureManager: refusing a " << width << "x" << height << " texture";
        return nullptr;
    }

    NS::SharedPtr<MTL::TextureDescriptor> descriptor =
        adopt(MTL::TextureDescriptor::alloc()->init());

    descriptor->setTextureType(MTL::TextureType2D);
    descriptor->setPixelFormat(kTextureFormat);
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
    if (!texture) {
        INK_ERROR << "MtlTextureManager: the device refused a " << width << "x" << height
                  << " texture allocation";
        return nullptr;
    }

    const NS::SharedPtr<NS::String> textureLabel = makeString(label);
    texture->setLabel(textureLabel.get());

    return texture;
}

TextureHandle MtlTextureManager::createFromPixels(const u8* rgbaPixels, u32 width, u32 height)
{
    if (!rgbaPixels) {
        INK_ERROR << "MtlTextureManager: createFromPixels was given a null pixel pointer";
        return INVALID_HANDLE;
    }

    NS::SharedPtr<MTL::Texture> texture = allocate(width, height, "Aura3D texture");
    if (!texture)
        return INVALID_HANDLE;

    texture->replaceRegion(MTL::Region::Make2D(0, 0, width, height), 0,
                           rgbaPixels, rowBytes(width));

    _textures.push_back(std::move(texture));
    return static_cast<TextureHandle>(_textures.size()); // 1-based
}

TextureHandle MtlTextureManager::createDynamic(u32 width, u32 height)
{
    NS::SharedPtr<MTL::Texture> texture = allocate(width, height, "Aura3D dynamic texture");
    if (!texture)
        return INVALID_HANDLE;

    /*
     * Metal leaves a fresh texture's contents undefined, while IRenderer promises
     * transparent black -- a glyph atlas that starts as uninitialised memory
     * shows garbage in the gaps between glyphs. One zeroed upload at creation is
     * what makes the promise true.
     */
    const std::vector<u8> zeros(static_cast<size_t>(width) * height * 4u, 0u);
    texture->replaceRegion(MTL::Region::Make2D(0, 0, width, height), 0,
                           zeros.data(), rowBytes(width));

    _textures.push_back(std::move(texture));
    return static_cast<TextureHandle>(_textures.size()); // 1-based
}

bool MtlTextureManager::updateRegion(TextureHandle handle, u32 x, u32 y,
                                    u32 width, u32 height, const u8* rgbaPixels)
{
    MTL::Texture* texture = resolve(handle);
    if (!texture) {
        INK_WARN << "MtlTextureManager: updateRegion on unknown texture handle " << handle;
        return false;
    }

    if (!rgbaPixels) {
        INK_ERROR << "MtlTextureManager: updateRegion was given a null pixel pointer";
        return false;
    }

    if (width == 0 || height == 0) {
        INK_WARN << "MtlTextureManager: updateRegion asked for an empty "
                 << width << "x" << height << " rectangle";
        return false;
    }

    /*
     * Rejected rather than clamped, as IRenderer::updateTextureRegion()
     * specifies. Metal's own reaction to an out-of-bounds region is an assertion
     * that kills the process, so this check is what keeps a bad glyph rectangle a
     * logged warning.
     */
    const u64 right = static_cast<u64>(x) + width;
    const u64 bottom = static_cast<u64>(y) + height;
    if (right > texture->width() || bottom > texture->height()) {
        INK_WARN << "MtlTextureManager: updateRegion rectangle (" << x << ", " << y << ", "
                 << width << ", " << height << ") reaches outside the "
                 << texture->width() << "x" << texture->height() << " texture";
        return false;
    }

    texture->replaceRegion(MTL::Region::Make2D(x, y, width, height), 0,
                           rgbaPixels, rowBytes(width));
    return true;
}

MTL::Texture* MtlTextureManager::resolve(TextureHandle handle) const noexcept
{
    if (!isValidHandle(handle))
        return nullptr;

    const size_t index = static_cast<size_t>(handle) - 1;
    if (index >= _textures.size())
        return nullptr;

    return _textures[index].get();
}

void MtlTextureManager::clear() noexcept
{
    _textures.clear();
}

} // namespace mtl
} // namespace aura3d
