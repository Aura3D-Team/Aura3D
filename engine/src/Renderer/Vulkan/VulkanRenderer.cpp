#include "aura/Renderer/Vulkan/VulkanRenderer.h"

#include "aura/Core/AuraException/AuraException.h"

namespace aura3d {
namespace vk {

VulkanRenderer::VulkanRenderer(const wma::WindowDetails& windowDetails, RendererMode mode)
    : IRenderer(windowDetails, mode),
      _vkInstanceData({}),
      _vkDeviceData({}),
      _vkImageViewData({})
{
    INK_INFO << "Renderer - VULKAN (" << RendererModeToString(mode) << ")";

    _vkInstanceData.appName    = "Aura3D";
    _vkInstanceData.engineName = "Aura3DEngine";
    _vkInstanceData.appVersion = {1, 0, 0};
    _vkInstanceData.vkInstanceExtensions = {};
    _vkInstanceData.vkValidationLayers   = { "VK_LAYER_KHRONOS_validation" };

    _vkDeviceData.vkDeviceExtensions = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };
    _vkDeviceData.vkEnabledLayers    = {};
    _vkDeviceData.concurrentQueueFlags = {};
    _vkDeviceData.exclusiveQueueFlags  =
        VK_QUEUE_GRAPHICS_BIT |
        VK_QUEUE_COMPUTE_BIT  |
        VK_QUEUE_TRANSFER_BIT;

    _vkImageViewData = {
        VK_IMAGE_ASPECT_COLOR_BIT,
        0, 1,   // baseMipLevel, levelCount
        0, 1    // baseArrayLayer, layerCount
    };
}

VulkanRenderer::~VulkanRenderer()
{
    if (_vkDeviceManager && _vkDeviceManager->getDevice())
        vkDeviceWaitIdle(*_vkDeviceManager->getDevice());
    cleanup();
}

void VulkanRenderer::initialize()
{
#ifdef NDEBUG
    const bool enableValidation = false;
#else
    const bool enableValidation = true;
#endif

    createAllocators(enableValidation);
    createCoreObjects(enableValidation);
    createResourceManagers();
    buildSwapchainResources();
    setupCommandBuffers();
}

void VulkanRenderer::createWindow(const char* title)
{
    _windowManagerApi = wma::createWindowManager(
        wma::WindowBackend::SDL2, _windowDetails, wma::GraphicsAPI::Vulkan);
    _windowManagerApi->createWindow(title);
}

void VulkanRenderer::createAllocators(bool /*enableValidation*/)
{
    VkHostAllocatorCreateInfo hostCfg = {};
    hostCfg.threadSafetyMode      = HostThreadSafetyMode::NONE;
    hostCfg.enableMemoryPools     = true;
    hostCfg.enableBatchProcessing = true;
    hostCfg.batchDeallocLimit     = 256;
#ifdef NDEBUG
    hostCfg.trackLeaks = false;
#else
    hostCfg.trackLeaks = true;
#endif
    _vkHostAllocator = std::make_unique<aura3d::vk::VkHostAllocator>(hostCfg);

    _windowManagerApi->getKeyboardListener().addKeyAction(wma::Key::KEY_ESCAPE, wma::KeyAction{
        [this](){ cleanup(); }, nullptr });
}

void VulkanRenderer::createCoreObjects(bool enableValidation)
{
    for (const char* ext : _windowManagerApi->getVulkanExtensions())
        _vkInstanceData.vkInstanceExtensions.push_back(ext);

    _vkInstance = std::make_unique<aura3d::vk::VkInstanceManager>(
        _vkHostAllocator.get(), _vkInstanceData, enableValidation);

    _vkDeviceManager = std::make_unique<aura3d::vk::VkDeviceManager>(
        _vkHostAllocator.get(), _vkInstance->getVkInstance(), _vkDeviceData);

    VkDeviceAllocatorCreateInfo devCfg = {};
    devCfg.physicalDevice               = *_vkDeviceManager->getPhysicalDevice();
    devCfg.device                       = *_vkDeviceManager->getDevice();
    devCfg.blockSize                    = 128 * 1024 * 1024;
    devCfg.smallBlockSize               =   8 * 1024 * 1024;
    devCfg.enableDefragmentation        = false;
    devCfg.threadSafetyMode             = ThreadSafetyMode::NONE;
    devCfg.strategy                     = AllocationStrategy::FIRST_FIT;
    devCfg.dedicatedAllocationThreshold = 64 * 1024 * 1024;
    devCfg.useBuddyAllocatorForBuffers  = true;
    devCfg.deferFrees                   = true;
    devCfg.deferredFreeLimit            = 256;
#ifdef NDEBUG
    devCfg.trackLeaks = false;
#else
    devCfg.trackLeaks = true;
#endif
    _vkDeviceAllocator = std::make_unique<aura3d::vk::VkDeviceAllocator>(
        _vkHostAllocator.get(), devCfg);

    _vkSurfaceManager = std::make_unique<aura3d::vk::VkSurfaceManager>(
        _vkHostAllocator.get(),
        _vkInstance->getVkInstance(),
        _windowManagerApi->getBackendType(),
        _windowManagerApi->getWindowInstance());

    _queueDataFromExclusiveFlags = _vkDeviceManager->getQueueManager()->getQueues(
        _vkDeviceData.exclusiveQueueFlags);

    _graphicsIndexFamily = aura3d::vk::VkQueueManager::findQueueFamilyIndex(
        *_vkDeviceManager->getPhysicalDevice(),
        _vkDeviceData.exclusiveQueueFlags,
        *_vkSurfaceManager->getSurface());
}

void VulkanRenderer::createResourceManagers()
{
    VkDevice* dev = _vkDeviceManager->getDevice();

    _vkSwapChainManager = std::make_unique<aura3d::vk::VkSwapChainManager>(
        _vkHostAllocator.get(),
        *_vkDeviceManager->getPhysicalDevice(),
        dev,
        *_vkSurfaceManager->getSurface());

    _vkImageViewsManager   = std::make_unique<aura3d::vk::VkImageViewsManager>(_vkHostAllocator.get(), dev);
    _vkRenderPassManager   = std::make_unique<aura3d::vk::VkRenderPassManager>(_vkHostAllocator.get(), dev);
    _vkFrameBuffersManager = std::make_unique<aura3d::vk::VkFrameBuffersManager>(_vkHostAllocator.get(), dev);
    _vkDescriptorManager   = std::make_unique<aura3d::vk::VkDescriptorManager>(_vkHostAllocator.get(), dev);

    _vkVertexBufferManager = std::make_unique<aura3d::vk::VkVertexBufferManager>(
        _vkHostAllocator.get(), _vkDeviceAllocator.get(), dev);

    _vkIndexBufferManager = std::make_unique<aura3d::vk::VkIndexBufferManager>(
        _vkHostAllocator.get(), _vkDeviceAllocator.get(), dev);

    _vkUniformBufferManager = std::make_unique<aura3d::vk::VkUniformBufferManager>(
        _vkHostAllocator.get(), _vkDeviceAllocator.get(), dev);

    _vkCommandManager = std::make_unique<aura3d::vk::VkCommandManager>(
        _vkHostAllocator.get(), dev, _graphicsIndexFamily);

    _vkTextureManager = std::make_unique<VkTextureManager>(
        _vkHostAllocator.get(),
        _vkDeviceAllocator.get(),
        dev,
        _vkDeviceManager->getPhysicalDevice(),
        _vkCommandManager->getThreadCommandPool(),
        _queueDataFromExclusiveFlags.front()->queues.front());

    _vkRenderSyncManager = std::make_unique<aura3d::vk::VkRenderSyncManager>(
        _vkHostAllocator.get(), dev);
}

void VulkanRenderer::setupPipeline(const std::string& vertShaderPath,
                                   const std::string& fragShaderPath)
{
    _vkGraphicsPipelineManager = std::make_unique<aura3d::vk::VkGraphicsPipelineManager>(
        _vkHostAllocator.get(),
        vertShaderPath,
        fragShaderPath,
        _vkDeviceManager->getDevice());
}

void VulkanRenderer::buildSwapchainResources()
{
    _vkSwapChainManager->createSwapChain(
        &_windowDetails,
        *_vkSurfaceManager->getSurface(),
        _vkDeviceManager.get());

    _vkImageViewsManager->createImageViews(
        _vkSwapChainManager->getSwapChainImages(),
        _vkSwapChainManager->getChoosedSurfaceFormat()->format,
        _vkImageViewData);

    if (is3D())
        createDepthResources();

    _vkRenderPassManager->createRenderPass(
        _vkSwapChainManager->getChoosedSurfaceFormat()->format,
        is3D(),
        _depth.format);

    _vkFrameBuffersManager->createFrameBuffers(
        _vkImageViewsManager->getImageViews(),
        *_vkRenderPassManager->getRenderPass(),
        *_vkSwapChainManager->getExtent2D(),
        _vkImageViewsManager->getDepthImageView());
}

void VulkanRenderer::destroySwapchainResources()
{
    _vkFrameBuffersManager->cleanup();
    _vkImageViewsManager->cleanup();     // destroys color + depth views
    _vkSwapChainManager->cleanup();
    _vkRenderPassManager->cleanup();
    _vkRenderSyncManager->cleanup();
    destroyDepthResources();             // destroys depth image + memory
}

void VulkanRenderer::createDepthResources()
{
    VkDevice   device = *_vkDeviceManager->getDevice();
    VkExtent2D extent = *_vkSwapChainManager->getExtent2D();

    VkImageCreateInfo imgInfo = {};
    imgInfo.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imgInfo.imageType     = VK_IMAGE_TYPE_2D;
    imgInfo.format        = _depth.format;
    imgInfo.extent        = { extent.width, extent.height, 1 };
    imgInfo.mipLevels     = 1;
    imgInfo.arrayLayers   = 1;
    imgInfo.samples       = VK_SAMPLE_COUNT_1_BIT;
    imgInfo.tiling        = VK_IMAGE_TILING_OPTIMAL;
    imgInfo.usage         = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    imgInfo.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
    imgInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VK_RESULT_CHECK(vkCreateImage(device, &imgInfo, _vkHostAllocator->getCallbacks(), &_depth.image));

    VK_RESULT_CHECK(_vkDeviceAllocator->allocateMemoryForImage(
        _depth.image, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, _depth.allocation));

    VK_RESULT_CHECK(_vkDeviceAllocator->bindImageMemory(_depth.image, _depth.allocation));

    _vkImageViewsManager->createDepthImageView(_depth.image, _depth.format);

    INK_INFO << "Depth resources created (" << extent.width << "x" << extent.height << ")";
}

void VulkanRenderer::destroyDepthResources()
{
    if (!_depth.isValid()) return;

    // The image view is owned by VkImageViewsManager – already cleaned in destroySwapchainResources
    // (cleanup() calls cleanupDepthImageView). For the explicit path we call it directly.
    _vkImageViewsManager->cleanupDepthImageView();

    vkDestroyImage(*_vkDeviceManager->getDevice(), _depth.image, _vkHostAllocator->getCallbacks());
    _vkDeviceAllocator->freeMemory(_depth.allocation);
    _depth.reset();
}

void VulkanRenderer::setupCommandBuffers()
{
    _cmdBuffers = _vkCommandManager->createCommandBuffer();
    _vkRenderSyncManager->create();
}

void VulkanRenderer::handleWindowChanges()
{
    vkDeviceWaitIdle(*_vkDeviceManager->getDevice());

    destroySwapchainResources();

    _vkSwapChainManager->initSwapChainSupportDetails(
        *_vkDeviceManager->getPhysicalDevice(),
        *_vkSurfaceManager->getSurface());

    buildSwapchainResources();
    _vkRenderSyncManager->create();
}

void VulkanRenderer::cleanup()
{
    if (_vkDeviceManager && _vkDeviceManager->getDevice())
        vkDeviceWaitIdle(*_vkDeviceManager->getDevice());

    if (_vkVertexBufferManager)  _vkVertexBufferManager->cleanup();
    if (_vkIndexBufferManager)   _vkIndexBufferManager->cleanup();
    if (_vkUniformBufferManager) _vkUniformBufferManager->cleanup();
    if (_vkTextureManager)       _vkTextureManager->cleanup();

    if (_depth.isValid())
        destroyDepthResources();

    _vkDescriptorManager.reset();
    _vkFrameBuffersManager.reset();
    _vkGraphicsPipelineManager.reset();
    _vkRenderPassManager.reset();
    _vkImageViewsManager.reset();
    _vkSwapChainManager.reset();
    _vkCommandManager.reset();
    _vkRenderSyncManager.reset();

    _vkDeviceAllocator.reset();
    _vkDeviceManager.reset();
    _vkSurfaceManager.reset();
    _vkInstance.reset();
    _vkHostAllocator.reset();
    _windowManagerApi.reset();
}

}
} // namespace aura3d
