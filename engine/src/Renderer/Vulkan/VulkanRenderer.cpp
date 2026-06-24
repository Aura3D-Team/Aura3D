#include "aura/Renderer/Vulkan/VulkanRenderer.h"

#include <chrono>
#include <thread>

#include "aura/aura.h"
#include "aura/Core/AuraException/AuraException.h"
#include "aura/Renderer/Vulkan/VkAura/EmbeddedSpirv.h"

namespace aura3d {
namespace vk {

VulkanRenderer::VulkanRenderer(const wma::WindowDetails& windowDetails)
    : IRenderer(windowDetails),
      _vkInstanceData({}),
      _vkDeviceData({}),
      _vkImageViewData({})
{
    INK_INFO << "Renderer - VULKAN";

    _vkInstanceData.appName = "Aura3D";
    _vkInstanceData.engineName = "Aura3DEngine";
    _vkInstanceData.appVersion = {1, 0, 0};
    _vkInstanceData.vkInstanceExtensions = {};
    _vkInstanceData.vkValidationLayers = { "VK_LAYER_KHRONOS_validation" };

    _vkDeviceData.vkDeviceExtensions = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };
    _vkDeviceData.vkEnabledLayers = {};
    _vkDeviceData.concurrentQueueFlags = {};
    _vkDeviceData.exclusiveQueueFlags =
        VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT | VK_QUEUE_TRANSFER_BIT;

    _vkImageViewData = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
}

VulkanRenderer::~VulkanRenderer()
{
    if (_vkDeviceManager && _vkDeviceManager->getDevice())
        vkDeviceWaitIdle(*_vkDeviceManager->getDevice());
    cleanup();
}

void VulkanRenderer::initialize(AuraSettings* settings)
{
    if (_isInitialized) return;

#ifdef NDEBUG
    const bool enableValidation = false;
#else
    const bool enableValidation = true;
#endif

    _vmaConfig = VulkanMemoryManager::loadConfig(settings);
    _memoryManager = std::make_unique<VulkanMemoryManager>();

    createWindow(APPLICATION_NAME, wma::WindowBackend::SDL3);
    setupInput();
    createCoreObjects(enableValidation);
    createResourceManagers();
    buildSwapchainResources();
    createUniformBuffers();
    createDescriptorSets();
    setupCommandBuffers();

    _isInitialized = true;
}

void VulkanRenderer::createWindow(const char* title, const wma::WindowBackend& wBackend)
{
    _windowManagerApi = wma::createWindowManager(wBackend, _windowDetails, wma::GraphicsAPI::Vulkan);
    _windowManagerApi->createWindow(title);
}

void VulkanRenderer::setupInput()
{
    _windowManagerApi->getKeyboardListener().addKeyAction(wma::Key::KEY_ESCAPE, wma::KeyAction{
        [this](){ cleanup(); }, nullptr });
}

void VulkanRenderer::createCoreObjects(bool enableValidation)
{
    for (const char* ext : _windowManagerApi->getVulkanExtensions())
        _vkInstanceData.vkInstanceExtensions.push_back(ext);

    _vkInstance = std::make_unique<VkInstanceManager>(_vkInstanceData, enableValidation);
    _vkDeviceManager = std::make_unique<VkDeviceManager>(_vkInstance->getVkInstance(), _vkDeviceData);

    _vkSurfaceManager = std::make_unique<VkSurfaceManager>(
        _vkInstance->getVkInstance(),
        _windowManagerApi->getBackendType(),
        _windowManagerApi->getWindowInstance());

    _memoryManager->initialize(
        *_vkInstance->getVkInstance(),
        *_vkDeviceManager->getPhysicalDevice(),
        *_vkDeviceManager->getDevice(),
        _vmaConfig);

    _queueDataFromExclusiveFlags = _vkDeviceManager->getQueueManager()->getQueues(_vkDeviceData.exclusiveQueueFlags);
    _graphicsIndexFamily = VkQueueManager::findQueueFamilyIndex(
        *_vkDeviceManager->getPhysicalDevice(),
        _vkDeviceData.exclusiveQueueFlags,
        *_vkSurfaceManager->getSurface());
}

void VulkanRenderer::createResourceManagers()
{
    VkDevice* dev = _vkDeviceManager->getDevice();

    _vkSwapChainManager = std::make_unique<VkSwapChainManager>(
        *_vkDeviceManager->getPhysicalDevice(), dev, *_vkSurfaceManager->getSurface());
    _vkImageViewsManager = std::make_unique<VkImageViewsManager>(dev);
    _vkRenderPassManager = std::make_unique<VkRenderPassManager>(dev);
    _vkFrameBuffersManager = std::make_unique<VkFrameBuffersManager>(dev);
    _vkDescriptorManager = std::make_unique<VkDescriptorManager>(dev);

    _vkGraphicsPipelineManager = std::make_unique<VkGraphicsPipelineManager>(
        vk_vert_3d, vk_vert_3d_len, vk_frag_3d, vk_frag_3d_len, dev);

    _vkVertexBufferManager = std::make_unique<VkVertexBufferManager>(_memoryManager.get(), dev);
    _vkIndexBufferManager = std::make_unique<VkIndexBufferManager>(_memoryManager.get(), dev);
    _vkUniformBufferManager = std::make_unique<VkUniformBufferManager>(_memoryManager.get(), dev);
    _vkCommandManager = std::make_unique<VkCommandManager>(dev, _graphicsIndexFamily);

    _vkTextureManager = std::make_unique<VkTextureManager>(
        _memoryManager.get(),
        dev,
        _vkCommandManager->getThreadCommandPool(),
        _queueDataFromExclusiveFlags.front()->queues.front());

    _vkRenderSyncManager = std::make_unique<VkRenderSyncManager>(dev);
}

void VulkanRenderer::setupPipeline(const std::string& vertShaderPath, const std::string& fragShaderPath)
{
    _vkGraphicsPipelineManager = std::make_unique<VkGraphicsPipelineManager>(
        vertShaderPath, fragShaderPath, _vkDeviceManager->getDevice());
    if (_isInitialized) {
        createDescriptorSets();
    }
}

void VulkanRenderer::buildSwapchainResources()
{
    _vkSwapChainManager->createSwapChain(&_windowDetails, *_vkSurfaceManager->getSurface(), _vkDeviceManager.get());
    _vkImageViewsManager->createImageViews(
        _vkSwapChainManager->getSwapChainImages(),
        _vkSwapChainManager->getChoosedSurfaceFormat()->format,
        _vkImageViewData);

    createDepthResources();

    _vkRenderPassManager->createRenderPass(
        _vkSwapChainManager->getChoosedSurfaceFormat()->format,
        true,
        _depth.format);

    _vkFrameBuffersManager->createFrameBuffers(
        _vkImageViewsManager->getImageViews(),
        *_vkRenderPassManager->getRenderPass(),
        *_vkSwapChainManager->getExtent2D(),
        _vkImageViewsManager->getDepthImageView());

    _imagesCount = static_cast<u32>(_vkSwapChainManager->getSwapChainImages().size());
}

void VulkanRenderer::createUniformBuffers()
{
    _vkUniformBufferManager->createUniformBuffers(
        _vkSwapChainManager->getSwapchainCreateInfoKHR()->imageSharingMode,
        _imagesCount);

    for (u32 i = 0; i < _imagesCount; ++i) {
        _vkUniformBufferManager->updateUniformBuffer(i, const_cast<gfx::TransformUBO&>(_currentTransform));
    }
}

void VulkanRenderer::createDescriptorSets()
{
    if (!_vkGraphicsPipelineManager) return;

    _vkGraphicsPipelineManager->createDescriptorSetLayouts();
    _descSets.resize(_imagesCount);

    for (u32 i = 0; i < _imagesCount; ++i) {
        _descSets[i] = _vkDescriptorManager->allocateDescriptorSet(
            _vkGraphicsPipelineManager->getDescriptorSetLayout(0));

        VkDescriptorBufferInfo bufInfo = _vkUniformBufferManager->getDescriptorBufferInfo(i);
        _vkDescriptorManager->updateDescriptorSet(
            _descSets[i], 0, bufInfo.buffer, bufInfo.range, bufInfo.offset);
    }

    std::vector<VkVertexInputBindingDescription> bindings = { VkVertexBufferManager::getBindingDescription() };
    auto attributes = VkVertexBufferManager::getAttributeDescriptions();

    _vkGraphicsPipelineManager->createPipeline(
        *_vkRenderPassManager->getRenderPass(),
        *_vkSwapChainManager->getExtent2D(),
        bindings,
        attributes,
        VkVertexBufferManager::getAttributeDescriptionCount(),
        true);

    _pipelineReady = true;
}

void VulkanRenderer::updateTextureDescriptorSets(TextureHandle textureHandle)
{
    if (!_pipelineReady || !_vkGraphicsPipelineManager) return;

    auto nameIt = _texNames.find(textureHandle);
    if (nameIt == _texNames.end()) return;

    const auto* texture = _vkTextureManager->getTexture(nameIt->second);
    if (!texture) return;

    _texDescSets.resize(_imagesCount);
    for (u32 i = 0; i < _imagesCount; ++i) {
        _texDescSets[i] = _vkDescriptorManager->allocateDescriptorSet(
            _vkGraphicsPipelineManager->getDescriptorSetLayout(1));
        _vkDescriptorManager->updateCombinedImageSamplerDescriptorSet(
            _texDescSets[i], 0, texture->view, texture->sampler);
    }
}

void VulkanRenderer::destroySwapchainResources()
{
    _pipelineReady = false;
    _descSets.clear();
    _texDescSets.clear();

    _vkFrameBuffersManager->cleanup();
    _vkImageViewsManager->cleanup();
    _vkSwapChainManager->cleanup();
    _vkRenderPassManager->cleanup();
    _vkRenderSyncManager->cleanup();
    destroyDepthResources();

    _vkDescriptorManager = std::make_unique<VkDescriptorManager>(_vkDeviceManager->getDevice());
}

void VulkanRenderer::createDepthResources()
{
    VkExtent2D extent = *_vkSwapChainManager->getExtent2D();

    VkImageCreateInfo imgInfo = {};
    imgInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imgInfo.imageType = VK_IMAGE_TYPE_2D;
    imgInfo.format = _depth.format;
    imgInfo.extent = { extent.width, extent.height, 1 };
    imgInfo.mipLevels = 1;
    imgInfo.arrayLayers = 1;
    imgInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imgInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imgInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    imgInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imgInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    AllocatedImage depthImage = _memoryManager->createImage(imgInfo, VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE);
    _depth.image = depthImage.image;
    _depth.allocation = depthImage.allocation;

    _vkImageViewsManager->createDepthImageView(_depth.image, _depth.format);
}

void VulkanRenderer::destroyDepthResources()
{
    if (!_depth.isValid()) return;

    _vkImageViewsManager->cleanupDepthImageView();

    AllocatedImage depthImage{_depth.image, _depth.allocation};
    _memoryManager->destroyImage(depthImage);
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
    createUniformBuffers();
    createDescriptorSets();

    for (const auto& entry : _texNames) {
        updateTextureDescriptorSets(entry.first);
        break;
    }
    _vkRenderSyncManager->create();
}

void VulkanRenderer::cleanup()
{
    if (_vkDeviceManager && _vkDeviceManager->getDevice())
        vkDeviceWaitIdle(*_vkDeviceManager->getDevice());

    _descSets.clear();
    _texDescSets.clear();
    _vbNames.clear();
    _ibNames.clear();
    _texNames.clear();
    _pipelineReady = false;
    _renderPassActive = false;
    _frameBegun = false;

    if (_vkVertexBufferManager) _vkVertexBufferManager->cleanup();
    if (_vkIndexBufferManager) _vkIndexBufferManager->cleanup();
    if (_vkUniformBufferManager) _vkUniformBufferManager->cleanup();
    if (_vkTextureManager) _vkTextureManager->cleanup();
    if (_depth.isValid()) destroyDepthResources();

    _vkDescriptorManager.reset();
    _vkFrameBuffersManager.reset();
    _vkGraphicsPipelineManager.reset();
    _vkRenderPassManager.reset();
    _vkImageViewsManager.reset();
    _vkSwapChainManager.reset();
    _vkCommandManager.reset();
    _vkRenderSyncManager.reset();
    _vkVertexBufferManager.reset();
    _vkIndexBufferManager.reset();
    _vkUniformBufferManager.reset();
    _vkTextureManager.reset();

    if (_memoryManager) _memoryManager->shutdown();

    _vkSurfaceManager.reset();
    _vkDeviceManager.reset();
    _vkInstance.reset();
    _memoryManager.reset();
    _windowManagerApi.reset();

    _isInitialized = false;
}

VertexBufferHandle VulkanRenderer::createVertexBuffer(std::vector<gfx::Vertex3D>&& vertices)
{
    auto handle = _nextVbHandle++;
    std::string name = "vb_" + std::to_string(handle);

    _vkVertexBufferManager->createVertexBuffer(
        name,
        _vkCommandManager->getThreadCommandPool(),
        _vkSwapChainManager->getSwapchainCreateInfoKHR()->imageSharingMode,
        _queueDataFromExclusiveFlags.front()->queues.front(),
        std::move(vertices),
        false);

    _vbNames[handle] = name;
    return handle;
}

IndexBufferHandle VulkanRenderer::createIndexBuffer(std::vector<u16>&& indices)
{
    auto handle = _nextIbHandle++;
    std::string name = "ib_" + std::to_string(handle);

    _vkIndexBufferManager->createIndexBuffer(
        name,
        _vkCommandManager->getThreadCommandPool(),
        _vkSwapChainManager->getSwapchainCreateInfoKHR()->imageSharingMode,
        _queueDataFromExclusiveFlags.front()->queues.front(),
        std::move(indices),
        false);

    _ibNames[handle] = name;
    return handle;
}

IndexBufferHandle VulkanRenderer::createIndexBuffer(std::vector<u32>&& indices)
{
    std::vector<u16> indices16;
    indices16.reserve(indices.size());
    for (u32 idx : indices) {
        indices16.push_back(static_cast<u16>(idx));
    }
    return createIndexBuffer(std::move(indices16));
}

TextureHandle VulkanRenderer::createSolidColorTexture(u8 r, u8 g, u8 b, u8 a)
{
    auto handle = _nextTexHandle++;
    std::string name = "tex_" + std::to_string(handle);
    _vkTextureManager->createSolidColorTexture(name, r, g, b, a);
    _texNames[handle] = name;
    updateTextureDescriptorSets(handle);
    return handle;
}

void VulkanRenderer::beginFrame()
{
    _frameBegun = false;
    _renderPassActive = false;

    auto* windowFlags = _windowManagerApi->getWindowFlags();
    if (!windowFlags || !_pipelineReady) return;

    _vkRenderSyncManager->waitForFences(_currentFrame);

    if (windowFlags->resized) {
        while (windowFlags->resized) {
            windowFlags->resized = false;
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        handleWindowChanges();
        return;
    }

    u32 imageIndex = _vkSwapChainManager->acquireNextImage(
        _vkRenderSyncManager->getImageAvailableSemaphores()[_currentFrame],
        windowFlags);

    if (imageIndex >= _imagesCount) return;

    _currentImageIndex = imageIndex;
    _frameBegun = true;

    _vkRenderSyncManager->resetFences(_currentFrame);
    _vkCommandManager->resetCommandPool();

    VkCommandBuffer cmd = _cmdBuffers[_currentFrame];
    VkCommandManager::beginCommandBuffer(cmd);
}

void VulkanRenderer::beginRenderPass()
{
    _renderPassActive = false;
    if (!_frameBegun || !_pipelineReady) return;

    VkCommandBuffer cmd = _cmdBuffers[_currentFrame];
    VkClearValue clearColor = { { {_clearR, _clearG, _clearB, _clearA} } };

    _vkRenderPassManager->beginRenderPass(
        cmd,
        _vkFrameBuffersManager->getFrameBuffers()[_currentImageIndex],
        *_vkSwapChainManager->getExtent2D(),
        &clearColor);

    _renderPassActive = true;

    _vkGraphicsPipelineManager->cmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS);
    _vkUniformBufferManager->updateUniformBuffer(
        _currentImageIndex, const_cast<gfx::TransformUBO&>(_currentTransform));
}

void VulkanRenderer::endRenderPass()
{
    if (!_frameBegun || !_renderPassActive) return;
    VkCommandBuffer cmd = _cmdBuffers[_currentFrame];
    VkRenderPassManager::endRenderPass(cmd);
    _renderPassActive = false;
}

void VulkanRenderer::endFrame()
{
    if (!_frameBegun) return;

    VkCommandBuffer cmd = _cmdBuffers[_currentFrame];
    VkCommandManager::endCommandBuffer(cmd);

    VkQueue graphicsQueue = _queueDataFromExclusiveFlags.front()->queues.front();
    VkQueueManager::submitCmdIntoQueue(
        graphicsQueue, &cmd,
        &_vkRenderSyncManager->getImageAvailableSemaphores()[_currentFrame],
        &_vkRenderSyncManager->getRenderFinishedSemaphores()[_currentFrame],
        _vkRenderSyncManager->getInFlightFences()[_currentFrame]);

    _vkSwapChainManager->presentBackToSwapChain(
        graphicsQueue,
        &_vkRenderSyncManager->getRenderFinishedSemaphores()[_currentFrame],
        _currentImageIndex);

    _frameBegun = false;
    _renderPassActive = false;
    advanceFrame();
}

void VulkanRenderer::setTransform(const gfx::TransformUBO& ubo) { _currentTransform = ubo; }
void VulkanRenderer::bindVertexBuffer(VertexBufferHandle handle) { _currentVertexBuffer = handle; }
void VulkanRenderer::bindIndexBuffer(IndexBufferHandle handle) { _currentIndexBuffer = handle; }
void VulkanRenderer::bindTexture(TextureHandle handle) { _currentTexture = handle; }

void VulkanRenderer::drawIndexed(u32 indexCount, u32 instanceCount)
{
    if (!_frameBegun || !_renderPassActive || !_pipelineReady) return;

    VkCommandBuffer cmd = _cmdBuffers[_currentFrame];
    _vkGraphicsPipelineManager->cmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS);

    if (_currentImageIndex < _descSets.size()) {
        _vkGraphicsPipelineManager->cmdBindDescriptorSets(
            cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, 0, 1, &_descSets[_currentImageIndex], 0, nullptr);
    }

    auto vbIt = _vbNames.find(_currentVertexBuffer);
    if (vbIt != _vbNames.end()) {
        auto vb = _vkVertexBufferManager->getVertexBuffer(vbIt->second);
        VkDeviceSize vertexOffset = vb.memoryOffset;
        vkCmdBindVertexBuffers(cmd, 0, 1, &vb.buffer, &vertexOffset);
    }

    auto ibIt = _ibNames.find(_currentIndexBuffer);
    if (ibIt != _ibNames.end()) {
        auto ib = _vkIndexBufferManager->getIndexBuffer(ibIt->second);
        vkCmdBindIndexBuffer(cmd, ib.buffer, ib.memoryOffset, VK_INDEX_TYPE_UINT16);
    }

    if (_currentImageIndex < _texDescSets.size()) {
        _vkGraphicsPipelineManager->cmdBindDescriptorSets(
            cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, 1, 1, &_texDescSets[_currentImageIndex], 0, nullptr);
    }

    _vkGraphicsPipelineManager->cmdIndexedDraw(
        cmd, *_vkSwapChainManager->getExtent2D(), indexCount, instanceCount, 0, 0, 0);
}

void VulkanRenderer::draw(u32 vertexCount, u32 instanceCount)
{
    if (!_frameBegun || !_renderPassActive || !_pipelineReady) return;

    VkCommandBuffer cmd = _cmdBuffers[_currentFrame];
    _vkGraphicsPipelineManager->cmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS);

    if (_currentImageIndex < _descSets.size()) {
        _vkGraphicsPipelineManager->cmdBindDescriptorSets(
            cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, 0, 1, &_descSets[_currentImageIndex], 0, nullptr);
    }

    auto vbIt = _vbNames.find(_currentVertexBuffer);
    if (vbIt != _vbNames.end()) {
        auto vb = _vkVertexBufferManager->getVertexBuffer(vbIt->second);
        VkDeviceSize vertexOffset = vb.memoryOffset;
        vkCmdBindVertexBuffers(cmd, 0, 1, &vb.buffer, &vertexOffset);
    }

    _vkGraphicsPipelineManager->cmdDraw(
        cmd, *_vkSwapChainManager->getExtent2D(), vertexCount, instanceCount, 0, 0);
}

void VulkanRenderer::setClearColor(f32 r, f32 g, f32 b, f32 a)
{
    _clearR = r;
    _clearG = g;
    _clearB = b;
    _clearA = a;
}

wma::IWindowManager* VulkanRenderer::getWindowManager() { return _windowManagerApi.get(); }
RendererChoice VulkanRenderer::getBackendType() const { return RendererChoice::VULKAN; }
VkVertexBufferManager* VulkanRenderer::getVertexBufferManager() { return _vkVertexBufferManager.get(); }
VkIndexBufferManager* VulkanRenderer::getIndexBufferManager() { return _vkIndexBufferManager.get(); }
VkUniformBufferManager* VulkanRenderer::getUniformBufferManager() { return _vkUniformBufferManager.get(); }
VkDescriptorManager* VulkanRenderer::getDescriptorManager() { return _vkDescriptorManager.get(); }
VkGraphicsPipelineManager* VulkanRenderer::getGraphicsPipelineManager() { return _vkGraphicsPipelineManager.get(); }
VkSwapChainManager* VulkanRenderer::getSwapChainManager() { return _vkSwapChainManager.get(); }
VkRenderPassManager* VulkanRenderer::getRenderPassManager() { return _vkRenderPassManager.get(); }
VkFrameBuffersManager* VulkanRenderer::getFrameBuffersManager() { return _vkFrameBuffersManager.get(); }
VkCommandManager* VulkanRenderer::getCommandManager() { return _vkCommandManager.get(); }
VkRenderSyncManager* VulkanRenderer::getRenderSyncManager() { return _vkRenderSyncManager.get(); }
VkTextureManager* VulkanRenderer::getTextureManager() { return _vkTextureManager.get(); }
VkDeviceManager* VulkanRenderer::getDeviceManager() { return _vkDeviceManager.get(); }
VulkanMemoryManager* VulkanRenderer::getMemoryManager() { return _memoryManager.get(); }
const std::vector<aura3d::vk::QueueData*>& VulkanRenderer::getQueues() const { return _queueDataFromExclusiveFlags; }
VkFixedArray<VkCommandBuffer>& VulkanRenderer::getCommandBuffers() { return _cmdBuffers; }
u32 VulkanRenderer::getCurrentFrame() const { return _currentFrame; }
void VulkanRenderer::advanceFrame() { _currentFrame = (_currentFrame + 1) % MAX_FRAMES_IN_FLIGHT; }

} // namespace vk
} // namespace aura3d
