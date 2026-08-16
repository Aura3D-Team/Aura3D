#include "aura/Renderer/Metal/MtlAura/MtlDrawableManager/MtlDrawableManager.h"

#include <ink/ink.hpp>

#include "aura/aura.h"
#include "aura/Core/AuraException/AuraException.h"

namespace aura3d {
namespace mtl {

MtlDrawableManager::MtlDrawableManager(MTL::Device* device, MtlLayerManager* layers)
    : _device(device), _layers(layers)
{
    if (!_device)
        throw AuraException("MtlDrawableManager: device is null");

    if (!_layers)
        throw AuraException("MtlDrawableManager: layer manager is null");

    _renderPass = adopt(MTL::RenderPassDescriptor::alloc()->init());
    if (!_renderPass)
        throw AuraException("MtlDrawableManager: failed to allocate the render pass descriptor");

    createDepthTexture(_layers->getWidth(), _layers->getHeight());
}

MtlDrawableManager::~MtlDrawableManager()
{
    _drawable.reset();
    _renderPass.reset();
    _depthTexture.reset();

    _layers = nullptr;
    _device = nullptr;

    INK_DEBUG << "MtlDrawableManager destroyed";
}

bool MtlDrawableManager::acquire()
{
    //! A drawable left over from a frame that never presented would leak its
    //! slot out of the pool, so the previous one is always dropped first.
    _drawable.reset();

    CA::MetalDrawable* next = _layers->getLayer()->nextDrawable();
    if (!next)
        return false;

    _drawable = retain(next);
    return true;
}

void MtlDrawableManager::release() noexcept
{
    _drawable.reset();
}

MTL::RenderPassDescriptor* MtlDrawableManager::buildRenderPass(f32 clearR, f32 clearG,
                                                              f32 clearB, f32 clearA)
{
    if (!_drawable)
        return nullptr;

    MTL::RenderPassColorAttachmentDescriptor* color = _renderPass->colorAttachments()->object(0);
    color->setTexture(_drawable->texture());
    color->setLoadAction(MTL::LoadActionClear);
    color->setStoreAction(MTL::StoreActionStore);
    color->setClearColor(MTL::ClearColor(static_cast<double>(clearR),
                                        static_cast<double>(clearG),
                                        static_cast<double>(clearB),
                                        static_cast<double>(clearA)));

    MTL::RenderPassDepthAttachmentDescriptor* depth = _renderPass->depthAttachment();
    depth->setTexture(_depthTexture.get());
    depth->setLoadAction(MTL::LoadActionClear);
    //! Discarded rather than stored: see buildRenderPass()'s doc comment.
    depth->setStoreAction(MTL::StoreActionDontCare);
    /*
     * 1.0 is the far plane under Metal's [0,1] depth range, which is why the
     * scene's depth-stencil state compares with CompareFunctionLess -- the same
     * pairing the Vulkan path uses, and the reason Camera::setClipSpace() is put
     * in ClipSpace::Vulkan (i.e. _ZO projections) for this backend.
     */
    depth->setClearDepth(1.0);

    return _renderPass.get();
}

void MtlDrawableManager::handleResize()
{
    const u32 width = _layers->getWidth();
    const u32 height = _layers->getHeight();

    if (width == _depthWidth && height == _depthHeight)
        return;

    createDepthTexture(width, height);
}

void MtlDrawableManager::createDepthTexture(u32 width, u32 height)
{
    NS::SharedPtr<MTL::TextureDescriptor> descriptor =
        adopt(MTL::TextureDescriptor::alloc()->init());

    descriptor->setTextureType(MTL::TextureType2D);
    descriptor->setPixelFormat(kDepthFormat);
    descriptor->setWidth(width);
    descriptor->setHeight(height);
    descriptor->setMipmapLevelCount(1);
    descriptor->setSampleCount(1);
    descriptor->setUsage(MTL::TextureUsageRenderTarget);
    /*
     * Private storage: the depth buffer is written and read by the GPU alone,
     * never mapped or uploaded to from the CPU. Combined with the pass's
     * DontCare store action, this is what lets the driver keep it in tile memory
     * on Apple GPUs instead of backing it with device memory.
     */
    descriptor->setStorageMode(MTL::StorageModePrivate);

    NS::SharedPtr<MTL::Texture> texture = adopt(_device->newTexture(descriptor.get()));
    if (!texture) {
        throw AuraException("MtlDrawableManager: failed to allocate a "
                            + std::to_string(width) + "x" + std::to_string(height)
                            + " depth texture");
    }

    _depthTexture = std::move(texture);
    _depthWidth = width;
    _depthHeight = height;

    INK_DEBUG << "Metal depth attachment: " << width << "x" << height;
}

} // namespace mtl
} // namespace aura3d
