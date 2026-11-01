#include "aura/Renderer/Vulkan/VulkanRenderer.h"

#include <algorithm>
#include <cstring>

#include "aura/Core/AuraException/AuraException.h"
#include "aura/Core/Profiling/FrameProfiler.h"
#include "aura/aura.h"

namespace aura3d
{
namespace vk
{

wma::WindowDetails VulkanRenderer::drawableDetails() const
{
    const auto framebuffer = _windowManagerApi->getFramebufferSize();
    auto details = *_windowManagerApi->getWindowDetails();
    details.width = framebuffer.width;
    details.height = framebuffer.height;
    return details;
}

bool VulkanRenderer::swapchainMatchesSurface()
{
    auto details = drawableDetails();
    return _vkSwapChainManager->matchesSurface(*_vkDeviceManager->getPhysicalDevice(), *_vkSurfaceManager->getSurface(),
                                               &details);
}

void VulkanRenderer::buildSwapchainResources()
{
    auto details = drawableDetails();
    _vkSwapChainManager->createSwapChain(&details, *_vkSurfaceManager->getSurface(), _vkDeviceManager.get(), 2,
                                         _windowManagerApi->transparentFramebuffer());
    _vkImageViewsManager->createImageViews(_vkSwapChainManager->getSwapChainImages(),
                                           _vkSwapChainManager->getChoosedSurfaceFormat()->format, _vkImageViewData);

    createDepthResources();
    createMsaaColorResources();

    _vkRenderPassManager->createRenderPass(_vkSwapChainManager->getChoosedSurfaceFormat()->format, true, _depth.format,
                                           _msaaSamples);

    _vkFrameBuffersManager->createFrameBuffers(
        _vkImageViewsManager->getImageViews(), *_vkRenderPassManager->getRenderPass(),
        *_vkSwapChainManager->getExtent2D(), _vkImageViewsManager->getDepthImageView(),
        _vkImageViewsManager->getColorMsaaImageView());

    _imagesCount = static_cast<u32>(_vkSwapChainManager->getSwapChainImages().size());
}

void VulkanRenderer::destroySwapchainResources()
{
    _pipelineReady = false;

    /*
     * Only the per-image transform/light sets are actually invalidated by a
     * resize (the swapchain image count can change) -- explicitly freeing
     * just those back to the pool, rather than destroying and recreating the
     * whole VkDescriptorManager as before, is what lets the persistent
     * bindless texture table (_bindlessTextureSet) -- and every
     * texture already written into them -- survive a resize untouched. No
     * per-texture descriptor work is needed here at all anymore; compare the
     * old handleWindowChanges()/recreateSurfaceAndSwapchain(), which used to
     * redo it for every live texture.
     */
    _vkDescriptorManager->freeDescriptorSets(_descSets);
    _vkDescriptorManager->freeDescriptorSets(_lightDescSets);

    _vkFrameBuffersManager->cleanup();
    _vkImageViewsManager->cleanup();
    _vkSwapChainManager->cleanup();
    _vkRenderPassManager->cleanup();
    _vkRenderSyncManager->cleanup();
    destroyDepthResources();
    destroyMsaaColorResources();
}

void VulkanRenderer::createDepthResources()
{
    VkExtent2D extent = *_vkSwapChainManager->getExtent2D();

    // Must match the color attachment's sample count (see VkRenderPassManager);
    // resolves to VK_SAMPLE_COUNT_1_BIT when MSAA is disabled.
    AllocatedImage depthImage =
        _memoryManager->createImage2D(_depth.format, extent, _msaaSamples, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT);
    _depth.image = depthImage.image;
    _depth.allocation = depthImage.allocation;

    _vkImageViewsManager->createDepthImageView(_depth.image, _depth.format);
}

void VulkanRenderer::destroyDepthResources()
{
    if (!_depth.isValid())
        return;

    _vkImageViewsManager->cleanupDepthImageView();

    AllocatedImage depthImage{_depth.image, _depth.allocation};
    _memoryManager->destroyImage(depthImage);
    _depth.reset();
}

void VulkanRenderer::createMsaaColorResources()
{
    if (_msaaSamples == VK_SAMPLE_COUNT_1_BIT)
        return;

    VkExtent2D extent = *_vkSwapChainManager->getExtent2D();
    const VkFormat colorFormat = _vkSwapChainManager->getChoosedSurfaceFormat()->format;

    // TRANSIENT: this attachment only ever exists inside a subpass and is
    // resolved away before the render pass ends, so tiled GPUs never need to
    // back it with real memory bandwidth.
    AllocatedImage colorImage =
        _memoryManager->createImage2D(colorFormat, extent, _msaaSamples,
                                      VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT);
    _msaaColor.image = colorImage.image;
    _msaaColor.allocation = colorImage.allocation;

    _vkImageViewsManager->createColorMsaaImageView(_msaaColor.image, colorFormat);
}

void VulkanRenderer::destroyMsaaColorResources()
{
    if (!_msaaColor.isValid())
        return;

    _vkImageViewsManager->cleanupColorMsaaImageView();

    AllocatedImage colorImage{_msaaColor.image, _msaaColor.allocation};
    _memoryManager->destroyImage(colorImage);
    _msaaColor.reset();
}

void VulkanRenderer::handleWindowChanges()
{
    vkDeviceWaitIdle(*_vkDeviceManager->getDevice());
    destroySwapchainResources();
    _vkSwapChainManager->initSwapChainSupportDetails(*_vkDeviceManager->getPhysicalDevice(),
                                                     *_vkSurfaceManager->getSurface());
    buildSwapchainResources();
    createUniformBuffers();
    //! No per-texture descriptor work needed here: the bindless texture-array
    //! sets are untouched by destroySwapchainResources() (see its comment),
    //! so every texture created before this resize is still correctly bound.
    createDescriptorSets();

    _vkRenderSyncManager->create(_imagesCount);
}

void VulkanRenderer::recreateSurfaceAndSwapchain()
{
    vkDeviceWaitIdle(*_vkDeviceManager->getDevice());

    destroySwapchainResources();

    _vkSurfaceManager.reset();
    _vkSurfaceManager = std::make_unique<VkSurfaceManager>(
        _vkInstance->getVkInstance(), _windowManagerApi->getBackendType(), _windowManagerApi->getWindowInstance(),
        _windowManagerApi->getNativeDisplayHandle());

    _vkSwapChainManager->initSwapChainSupportDetails(*_vkDeviceManager->getPhysicalDevice(),
                                                     *_vkSurfaceManager->getSurface());
    buildSwapchainResources();
    createUniformBuffers();
    //! No per-texture descriptor work needed here either -- see handleWindowChanges().
    createDescriptorSets();

    _vkRenderSyncManager->create(_imagesCount);
}

glm::uvec2 VulkanRenderer::renderTargetSize() const noexcept
{
    if (_frameState == FrameState::InPass)
        return {_passExtent.width, _passExtent.height};
    if (!_vkSwapChainManager || !_vkSwapChainManager->getExtent2D())
        return glm::uvec2{0};
    const VkExtent2D extent = *_vkSwapChainManager->getExtent2D();
    return {extent.width, extent.height};
}

} // namespace vk
} // namespace aura3d
