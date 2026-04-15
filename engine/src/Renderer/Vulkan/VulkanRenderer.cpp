#include "aura/Renderer/Vulkan/VulkanRenderer.h"

namespace aura3d {
namespace vk {

VulkanRenderer::VulkanRenderer(const wma::WindowDetails& windowDetails) :
    IRenderer(windowDetails),
    _vkInstanceData({}),
    _vkDeviceData({}),
    _vkImageViewData({})
{
    INK_INFO << "Renderer - VULKAN";

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

    _vkImageViewData.aspectMask      = VK_IMAGE_ASPECT_COLOR_BIT;
    _vkImageViewData.baseMipLevel    = 0;
    _vkImageViewData.levelCount      = 1;
    _vkImageViewData.baseArrayLayer  = 0;
    _vkImageViewData.layerCount      = 1;
}

VulkanRenderer::~VulkanRenderer()
{
    if (_vkDeviceManager && _vkDeviceManager->getDevice()) {
        vkDeviceWaitIdle(*_vkDeviceManager->getDevice());
    }
    cleanup();
}

// -----------------------------------------------------------------------------
// initialize – set up ALL Vulkan infrastructure
// -----------------------------------------------------------------------------

void VulkanRenderer::initialize()
{
#ifdef NDEBUG
    const bool enableValidationLayers = false;
#else
    const bool enableValidationLayers = true;
#endif

    // Host allocator
    VkHostAllocatorCreateInfo hostAllocCfg = {};
    hostAllocCfg.threadSafetyMode      = HostThreadSafetyMode::NONE;
    hostAllocCfg.enableMemoryPools     = true;
    hostAllocCfg.enableBatchProcessing = true;
    hostAllocCfg.batchDeallocLimit     = 256;
#ifdef NDEBUG
    hostAllocCfg.trackLeaks = false;
#else
    hostAllocCfg.trackLeaks = true;
#endif

    _vkHostAllocator = std::make_unique<aura3d::vk::VkHostAllocator>(hostAllocCfg);

    _windowManagerApi->getKeyboardListener().addKeyAction(wma::Key::KEY_ESCAPE, wma::KeyAction{
        [this](){ cleanup(); }, nullptr });

    // Gather window-provided Vulkan extensions
    for (const char* ext : _windowManagerApi->getVulkanExtensions()) {
        _vkInstanceData.vkInstanceExtensions.push_back(ext);
    }

    // Vulkan instance
    _vkInstance = std::make_unique<aura3d::vk::VkInstanceManager>(
        _vkHostAllocator.get(), _vkInstanceData, enableValidationLayers);

    // Device manager
    _vkDeviceManager = std::make_unique<aura3d::vk::VkDeviceManager>(
        _vkHostAllocator.get(), _vkInstance->getVkInstance(), _vkDeviceData);

    // Device allocator
    aura3d::vk::VkDeviceAllocatorCreateInfo devAllocCfg = {};
    devAllocCfg.physicalDevice              = *_vkDeviceManager->getPhysicalDevice();
    devAllocCfg.device                      = *_vkDeviceManager->getDevice();
    devAllocCfg.blockSize                   = 128 * 1024 * 1024;
    devAllocCfg.smallBlockSize              =   8 * 1024 * 1024;
    devAllocCfg.enableDefragmentation       = false;
    devAllocCfg.threadSafetyMode            = ThreadSafetyMode::NONE;
    devAllocCfg.strategy                    = AllocationStrategy::FIRST_FIT;
    devAllocCfg.dedicatedAllocationThreshold= 64 * 1024 * 1024;
    devAllocCfg.useBuddyAllocatorForBuffers = true;
    devAllocCfg.deferFrees                  = true;
    devAllocCfg.deferredFreeLimit           = 256;
#ifdef NDEBUG
    devAllocCfg.trackLeaks = false;
#else
    devAllocCfg.trackLeaks = true;
#endif

    _vkDeviceAllocator = std::make_unique<aura3d::vk::VkDeviceAllocator>(
        _vkHostAllocator.get(), devAllocCfg);

    // Surface
    _vkSurfaceManager = std::make_unique<aura3d::vk::VkSurfaceManager>(
        _vkHostAllocator.get(),
        _vkInstance->getVkInstance(),
        _windowManagerApi->getBackendType(),
        _windowManagerApi->getWindowInstance());

    // Queues
    _queueDataFromExclusiveFlags = _vkDeviceManager->getQueueManager()->getQueues(
        _vkDeviceData.exclusiveQueueFlags);
    _graphicsIndexFamily = aura3d::vk::VkQueueManager::findQueueFamilyIndex(
        *_vkDeviceManager->getPhysicalDevice(),
        _vkDeviceData.exclusiveQueueFlags,
        *_vkSurfaceManager->getSurface());

    // Swapchain / image-views / render-pass / framebuffers
    _vkSwapChainManager = std::make_unique<aura3d::vk::VkSwapChainManager>(
        _vkHostAllocator.get(),
        *_vkDeviceManager->getPhysicalDevice(),
        _vkDeviceManager->getDevice(),
        *_vkSurfaceManager->getSurface());

    _vkImageViewsManager   = std::make_unique<aura3d::vk::VkImageViewsManager>(
        _vkHostAllocator.get(), _vkDeviceManager->getDevice());

    _vkRenderPassManager   = std::make_unique<aura3d::vk::VkRenderPassManager>(
        _vkHostAllocator.get(), _vkDeviceManager->getDevice());

    _vkFrameBuffersManager = std::make_unique<aura3d::vk::VkFrameBuffersManager>(
        _vkHostAllocator.get(), _vkDeviceManager->getDevice());

    // Resource managers (the application fills these with its own data)
    _vkDescriptorManager = std::make_unique<aura3d::vk::VkDescriptorManager>(
        _vkHostAllocator.get(), _vkDeviceManager->getDevice());

    _vkVertexBufferManager = std::make_unique<aura3d::vk::VkVertexBufferManager>(
        _vkHostAllocator.get(), _vkDeviceAllocator.get(), _vkDeviceManager->getDevice());

    _vkIndexBufferManager = std::make_unique<aura3d::vk::VkIndexBufferManager>(
        _vkHostAllocator.get(), _vkDeviceAllocator.get(), _vkDeviceManager->getDevice());

    _vkUniformBufferManager = std::make_unique<aura3d::vk::VkUniformBufferManager>(
        _vkHostAllocator.get(), _vkDeviceAllocator.get(), _vkDeviceManager->getDevice());

    _vkCommandManager = std::make_unique<aura3d::vk::VkCommandManager>(
        _vkHostAllocator.get(), _vkDeviceManager->getDevice(), _graphicsIndexFamily);

    _vkTextureManager = std::make_unique<VkTextureManager>(
        _vkHostAllocator.get(),
        _vkDeviceAllocator.get(),
        _vkDeviceManager->getDevice(),
        _vkDeviceManager->getPhysicalDevice(),
        _vkCommandManager->getThreadCommandPool(),
        _queueDataFromExclusiveFlags.front()->queues.front());

    _vkRenderSyncManager = std::make_unique<aura3d::vk::VkRenderSyncManager>(
        _vkHostAllocator.get(), _vkDeviceManager->getDevice());

    // Build swapchain + render-pass + framebuffers
    setupGraphicsPipeline();

    // Command buffers + sync primitives
    setupCommandBuffers();
}

void VulkanRenderer::createWindow(const char* title)
{
    // Window manager
    _windowManagerApi = wma::createWindowManager(
        wma::WindowBackend::SDL2, _windowDetails, wma::GraphicsAPI::Vulkan);

    _windowManagerApi->createWindow(title);
}

// -----------------------------------------------------------------------------
// setupPipeline – application calls this with its chosen shaders
// -----------------------------------------------------------------------------

void VulkanRenderer::setupPipeline(const std::string& vertShaderPath,
                                   const std::string& fragShaderPath)
{
    _vkGraphicsPipelineManager = std::make_unique<aura3d::vk::VkGraphicsPipelineManager>(
        _vkHostAllocator.get(),
        vertShaderPath,
        fragShaderPath,
        _vkDeviceManager->getDevice());
}

// -----------------------------------------------------------------------------
// Private helpers
// -----------------------------------------------------------------------------

void VulkanRenderer::setupGraphicsPipeline()
{
    _vkSwapChainManager->createSwapChain(
        &_windowDetails,
        *_vkSurfaceManager->getSurface(),
        _vkDeviceManager.get());

    _vkImageViewsManager->createImageViews(
        _vkSwapChainManager->getSwapChainImages(),
        _vkSwapChainManager->getChoosedSurfaceFormat()->format,
        _vkImageViewData);

    _vkRenderPassManager->createRenderPass(
        _vkSwapChainManager->getChoosedSurfaceFormat()->format);

    _vkFrameBuffersManager->createFrameBuffers(
        _vkImageViewsManager->getImageViews(),
        *_vkRenderPassManager->getRenderPass(),
        *_vkSwapChainManager->getExtent2D());
}

void VulkanRenderer::setupCommandBuffers()
{
    _cmdBuffers = _vkCommandManager->createCommandBuffer();
    _vkRenderSyncManager->create();
}

// -----------------------------------------------------------------------------
// handleWindowChanges
// -----------------------------------------------------------------------------

void VulkanRenderer::handleWindowChanges()
{
    vkDeviceWaitIdle(*_vkDeviceManager->getDevice());

    _vkFrameBuffersManager->cleanup();
    _vkImageViewsManager->cleanup();
    _vkSwapChainManager->cleanup();
    _vkRenderPassManager->cleanup();
    _vkRenderSyncManager->cleanup();

    _vkSwapChainManager->initSwapChainSupportDetails(
        *_vkDeviceManager->getPhysicalDevice(),
        *_vkSurfaceManager->getSurface());

    _vkSwapChainManager->createSwapChain(
        &_windowDetails,
        *_vkSurfaceManager->getSurface(),
        _vkDeviceManager.get());

    _vkImageViewsManager->createImageViews(
        _vkSwapChainManager->getSwapChainImages(),
        _vkSwapChainManager->getChoosedSurfaceFormat()->format,
        _vkImageViewData);

    _vkRenderPassManager->createRenderPass(
        _vkSwapChainManager->getChoosedSurfaceFormat()->format);

    _vkFrameBuffersManager->createFrameBuffers(
        _vkImageViewsManager->getImageViews(),
        *_vkRenderPassManager->getRenderPass(),
        *_vkSwapChainManager->getExtent2D());

    _vkRenderSyncManager->create();
}

// -----------------------------------------------------------------------------
// cleanup
// -----------------------------------------------------------------------------

void VulkanRenderer::cleanup()
{
    if (_vkDeviceManager && _vkDeviceManager->getDevice()) {
        vkDeviceWaitIdle(*_vkDeviceManager->getDevice());
    }

    if (_vkVertexBufferManager)  _vkVertexBufferManager->cleanup();
    if (_vkIndexBufferManager)   _vkIndexBufferManager->cleanup();
    if (_vkUniformBufferManager) _vkUniformBufferManager->cleanup();
    if (_vkTextureManager)       _vkTextureManager->cleanup();

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
