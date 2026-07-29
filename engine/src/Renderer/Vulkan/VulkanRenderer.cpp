#include "aura/Renderer/Vulkan/VulkanRenderer.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <thread>

#include <glm/ext/matrix_clip_space.hpp>

#include "aura/aura.h"
#include "aura/Core/AuraException/AuraException.h"
#include "aura/Renderer/Vulkan/VkAura/EmbeddedSpirv.h"

namespace aura3d {
namespace vk {

namespace {

//! Vertex layout consumed by the overlay pipeline; mirrors gfx::Vertex2D.
[[nodiscard]] VkVertexInputBindingDescription overlay2DBindingDescription() noexcept
{
    VkVertexInputBindingDescription binding{};
    binding.binding = 0;
    binding.stride = sizeof(gfx::Vertex2D);
    binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    return binding;
}

//! Locations 0/1/2 match vk_shader2d.vert's inPosition/inTexCoord/inColor.
[[nodiscard]] AttributeDescriptionArray<VkVertexInputAttributeDescription> overlay2DAttributeDescriptions() noexcept
{
    AttributeDescriptionArray<VkVertexInputAttributeDescription> attributes{};

    attributes[0].binding = 0;
    attributes[0].location = 0;
    attributes[0].format = VK_FORMAT_R32G32_SFLOAT;
    attributes[0].offset = offsetof(gfx::Vertex2D, pos);

    attributes[1].binding = 0;
    attributes[1].location = 1;
    attributes[1].format = VK_FORMAT_R32G32_SFLOAT;
    attributes[1].offset = offsetof(gfx::Vertex2D, texCoord);

    attributes[2].binding = 0;
    attributes[2].location = 2;
    attributes[2].format = VK_FORMAT_R32G32B32A32_SFLOAT;
    attributes[2].offset = offsetof(gfx::Vertex2D, color);

    return attributes;
}

//! Largest power-of-two sample count that is both <= `requested` and
//! supported by the device. Falls back to 1x for a nonsensical or
//! unsupported request rather than rejecting it.
[[nodiscard]] VkSampleCountFlagBits resolveSampleCount(int requested, VkSampleCountFlagBits deviceMax) noexcept
{
    VkSampleCountFlagBits resolved = VK_SAMPLE_COUNT_1_BIT;
    for (VkSampleCountFlagBits candidate : { VK_SAMPLE_COUNT_2_BIT, VK_SAMPLE_COUNT_4_BIT,
                                              VK_SAMPLE_COUNT_8_BIT, VK_SAMPLE_COUNT_16_BIT,
                                              VK_SAMPLE_COUNT_32_BIT, VK_SAMPLE_COUNT_64_BIT }) {
        if (static_cast<int>(candidate) <= requested && candidate <= deviceMax)
            resolved = candidate;
    }
    return resolved;
}

} // namespace

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

    const bool enableValidation = settings->getValidationLayers();

    _vmaConfig = VulkanMemoryManager::loadConfig(settings);
    _memoryManager = std::make_unique<VulkanMemoryManager>();

    createWindow(settings->getWindowTitle().c_str(), settings->getWindowBackend());
    setupInput();
    createCoreObjects(enableValidation);

    _msaaSamples = resolveSampleCount(settings->getMsaaSamples(), _vkDeviceManager->getMaxUsableSampleCount());

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

    _vkOverlay2DPipelineManager = std::make_unique<VkGraphicsPipelineManager>(
        vk_vert_2d, vk_vert_2d_len, vk_frag_2d, vk_frag_2d_len, dev);

    /*
     * Replace the 3D interface the constructor installed. The overlay shaders
     * reference exactly one descriptor -- the sampler in set 0 -- and take their
     * projection from a 64-byte push constant, so declaring the scene's UBO and
     * light sets here would build a layout whose bindings nothing ever fills.
     */
    _vkOverlay2DPipelineManager->resetInterface();

    DescriptorBindingInfo overlaySampler;
    overlaySampler.binding = 0;
    overlaySampler.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    overlaySampler.descriptorCount = 1;
    overlaySampler.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    _vkOverlay2DPipelineManager->addDescriptorBinding(0, overlaySampler);

    _vkOverlay2DPipelineManager->setPushConstantRange(
        VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(glm::mat4));

    _vkVertexBufferManager = std::make_unique<VkVertexBufferManager>(_memoryManager.get(), dev);
    _vkIndexBufferManager = std::make_unique<VkIndexBufferManager>(_memoryManager.get(), dev);
    _vkUniformBufferManager = std::make_unique<VkUniformBufferManager>(_memoryManager.get(), dev);
    _vkLightUniformBufferManager = std::make_unique<VkUniformBufferManager>(_memoryManager.get(), dev);
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
    createMsaaColorResources();

    _vkRenderPassManager->createRenderPass(
        _vkSwapChainManager->getChoosedSurfaceFormat()->format,
        true,
        _depth.format,
        _msaaSamples);

    _vkFrameBuffersManager->createFrameBuffers(
        _vkImageViewsManager->getImageViews(),
        *_vkRenderPassManager->getRenderPass(),
        *_vkSwapChainManager->getExtent2D(),
        _vkImageViewsManager->getDepthImageView(),
        _vkImageViewsManager->getColorMsaaImageView());

    _imagesCount = static_cast<u32>(_vkSwapChainManager->getSwapChainImages().size());
}

void VulkanRenderer::createUniformBuffers()
{
    const VkSharingMode sharingMode = _vkSwapChainManager->getSwapchainCreateInfoKHR()->imageSharingMode;

    _vkUniformBufferManager->createUniformBuffers(sharingMode, _imagesCount);

    for (u32 i = 0; i < _imagesCount; ++i) 
    {
        _vkUniformBufferManager->updateUniformBuffer(i, const_cast<gfx::TransformUBO&>(_currentTransform));
    }

    _vkLightUniformBufferManager->createUniformBuffers(sharingMode, _imagesCount, sizeof(gfx::LightUBO));

    updateLightUniformBuffers();
}

void VulkanRenderer::updateLightUniformBuffers()
{
    if (!_vkLightUniformBufferManager) 
        return;

    for (u32 i = 0; i < _imagesCount; ++i) 
    {
        _vkLightUniformBufferManager->updateUniformBufferRaw(i, &_light, sizeof(gfx::LightUBO));
    }
}

void VulkanRenderer::setLight(const gfx::LightUBO& light)
{
    IRenderer::setLight(light);
    updateLightUniformBuffers();
}

void VulkanRenderer::createDescriptorSets()
{
    if (!_vkGraphicsPipelineManager) return;

    _vkGraphicsPipelineManager->createDescriptorSetLayouts();
    _descSets.resize(_imagesCount);
    _lightDescSets.resize(_imagesCount);

    for (u32 i = 0; i < _imagesCount; ++i) {
        _descSets[i] = _vkDescriptorManager->allocateDescriptorSet(
            _vkGraphicsPipelineManager->getDescriptorSetLayout(0));

        VkDescriptorBufferInfo bufInfo = _vkUniformBufferManager->getDescriptorBufferInfo(i);
        _vkDescriptorManager->updateDescriptorSet(
            _descSets[i], 0, bufInfo.buffer, bufInfo.range, bufInfo.offset);

        _lightDescSets[i] = _vkDescriptorManager->allocateDescriptorSet(_vkGraphicsPipelineManager->getDescriptorSetLayout(2));

        VkDescriptorBufferInfo lightInfo = _vkLightUniformBufferManager->getDescriptorBufferInfo(i);
        _vkDescriptorManager->updateDescriptorSet(_lightDescSets[i], 0, lightInfo.buffer, lightInfo.range, lightInfo.offset);
    }

    std::vector<VkVertexInputBindingDescription> bindings = { VkVertexBufferManager::getBindingDescription() };
    auto attributes = VkVertexBufferManager::getAttributeDescriptions();

    PipelineOptions sceneOptions{};
    sceneOptions.depthTest = true;
    sceneOptions.cullBackFaces = true;
    sceneOptions.sampleCount = _msaaSamples;

    _vkGraphicsPipelineManager->createPipeline(
        *_vkRenderPassManager->getRenderPass(),
        *_vkSwapChainManager->getExtent2D(),
        bindings,
        attributes,
        VkVertexBufferManager::getAttributeDescriptionCount(),
        sceneOptions);

    createOverlay2DPipeline();

    _pipelineReady = true;
}

void VulkanRenderer::createOverlay2DPipeline()
{
    if (!_vkOverlay2DPipelineManager) return;

    _vkOverlay2DPipelineManager->createDescriptorSetLayouts();

    const std::vector<VkVertexInputBindingDescription> bindings = { overlay2DBindingDescription() };
    const auto attributes = overlay2DAttributeDescriptions();

    /*
     * The defaults already describe an overlay -- no depth interaction, alpha
     * blending on, culling off -- so this spells them out only to make the
     * contrast with the scene pipeline above explicit at the call site.
     */
    PipelineOptions overlayOptions{};
    overlayOptions.depthTest = false;
    overlayOptions.alphaBlend = true;
    overlayOptions.cullBackFaces = false;
    //! Must match the scene pipeline's sample count -- both are built against
    //! the same render pass/subpass, which fixes one multisample state for
    //! every pipeline bound within it.
    overlayOptions.sampleCount = _msaaSamples;

    _vkOverlay2DPipelineManager->createPipeline(
        *_vkRenderPassManager->getRenderPass(),
        *_vkSwapChainManager->getExtent2D(),
        bindings,
        attributes,
        MAX_ATTRIBUTE_DESCRIPTION_2D,
        overlayOptions);
}

void VulkanRenderer::updateOverlay2DTextureDescriptorSets(TextureHandle textureHandle)
{
    if (!_vkOverlay2DPipelineManager) 
        return;

    auto nameIt = _texNames.find(textureHandle);
    if (nameIt == _texNames.end()) 
        return;

    const auto* texture = _vkTextureManager->getTexture(nameIt->second);
    if (!texture) 
        return;

    VkDescriptorSetLayout layout = _vkOverlay2DPipelineManager->getDescriptorSetLayout(0);
    if (layout == VK_NULL_HANDLE) 
        return;

    std::vector<VkDescriptorSet> sets(_imagesCount);
    for (u32 i = 0; i < _imagesCount; ++i)
    {
        sets[i] = _vkDescriptorManager->allocateDescriptorSet(layout);
        _vkDescriptorManager->updateCombinedImageSamplerDescriptorSet(sets[i], 0, texture->view, texture->sampler);
    }
    _tex2dDescSets[textureHandle] = std::move(sets);
}

void VulkanRenderer::updateTextureDescriptorSets(TextureHandle textureHandle)
{
    if (!_pipelineReady || !_vkGraphicsPipelineManager) return;

    auto nameIt = _texNames.find(textureHandle);
    if (nameIt == _texNames.end()) return;

    const auto* texture = _vkTextureManager->getTexture(nameIt->second);
    if (!texture) return;

    std::vector<VkDescriptorSet> sets(_imagesCount);
    for (u32 i = 0; i < _imagesCount; ++i) 
    {
        sets[i] = _vkDescriptorManager->allocateDescriptorSet(
            _vkGraphicsPipelineManager->getDescriptorSetLayout(1));
        _vkDescriptorManager->updateCombinedImageSamplerDescriptorSet(
            sets[i], 0, texture->view, texture->sampler);
    }
    _texDescSets[textureHandle] = std::move(sets);
}

void VulkanRenderer::destroySwapchainResources()
{
    _pipelineReady = false;
    _descSets.clear();
    _lightDescSets.clear();
    _texDescSets.clear();
    _tex2dDescSets.clear();

    _vkFrameBuffersManager->cleanup();
    _vkImageViewsManager->cleanup();
    _vkSwapChainManager->cleanup();
    _vkRenderPassManager->cleanup();
    _vkRenderSyncManager->cleanup();
    destroyDepthResources();
    destroyMsaaColorResources();

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
    // Must match the color attachment's sample count (see VkRenderPassManager);
    // resolves to VK_SAMPLE_COUNT_1_BIT when MSAA is disabled.
    imgInfo.samples = _msaaSamples;
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

void VulkanRenderer::createMsaaColorResources()
{
    if (_msaaSamples == VK_SAMPLE_COUNT_1_BIT) 
        return;

    VkExtent2D extent = *_vkSwapChainManager->getExtent2D();
    const VkFormat colorFormat = _vkSwapChainManager->getChoosedSurfaceFormat()->format;

    VkImageCreateInfo imgInfo = {};
    imgInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imgInfo.imageType = VK_IMAGE_TYPE_2D;
    imgInfo.format = colorFormat;
    imgInfo.extent = { extent.width, extent.height, 1 };
    imgInfo.mipLevels = 1;
    imgInfo.arrayLayers = 1;
    imgInfo.samples = _msaaSamples;
    imgInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    // TRANSIENT: this attachment only ever exists inside a subpass and is
    // resolved away before the render pass ends, so tiled GPUs never need to
    // back it with real memory bandwidth.
    imgInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT;
    imgInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imgInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    AllocatedImage colorImage = _memoryManager->createImage(imgInfo, VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE);
    _msaaColor.image = colorImage.image;
    _msaaColor.allocation = colorImage.allocation;

    _vkImageViewsManager->createColorMsaaImageView(_msaaColor.image, colorFormat);
}

void VulkanRenderer::destroyMsaaColorResources()
{
    if (!_msaaColor.isValid()) return;

    _vkImageViewsManager->cleanupColorMsaaImageView();

    AllocatedImage colorImage{_msaaColor.image, _msaaColor.allocation};
    _memoryManager->destroyImage(colorImage);
    _msaaColor.reset();
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

    //! Every texture needs fresh per-image sets on both pipelines: the old pool
    //! was destroyed with the swapchain resources.
    for (const auto& entry : _texNames) {
        updateTextureDescriptorSets(entry.first);
        updateOverlay2DTextureDescriptorSets(entry.first);
    }

    _vkRenderSyncManager->create();
}

void VulkanRenderer::cleanup()
{
    if (_vkDeviceManager && _vkDeviceManager->getDevice())
        vkDeviceWaitIdle(*_vkDeviceManager->getDevice());

    clearSharedResources();

    _descSets.clear();
    _lightDescSets.clear();
    _texDescSets.clear();
    _tex2dDescSets.clear();
    _vbNames.clear();
    _ibNames.clear();
    _texNames.clear();
    _pipelineReady = false;
    _renderPassActive = false;
    _frameBegun = false;
    _white2DTexture = INVALID_HANDLE;

    //! Before the allocator shuts down below, since these hold VMA allocations.
    destroyOverlay2DBuffers();

    if (_vkVertexBufferManager) _vkVertexBufferManager->cleanup();
    if (_vkIndexBufferManager) _vkIndexBufferManager->cleanup();
    if (_vkUniformBufferManager) _vkUniformBufferManager->cleanup();
    if (_vkLightUniformBufferManager) _vkLightUniformBufferManager->cleanup();
    if (_vkTextureManager) _vkTextureManager->cleanup();
    if (_depth.isValid()) destroyDepthResources();
    if (_msaaColor.isValid()) destroyMsaaColorResources();

    _vkDescriptorManager.reset();
    _vkFrameBuffersManager.reset();
    _vkOverlay2DPipelineManager.reset();
    _vkGraphicsPipelineManager.reset();
    _vkRenderPassManager.reset();
    _vkImageViewsManager.reset();
    _vkSwapChainManager.reset();
    _vkCommandManager.reset();
    _vkRenderSyncManager.reset();
    _vkVertexBufferManager.reset();
    _vkIndexBufferManager.reset();
    _vkUniformBufferManager.reset();
    _vkLightUniformBufferManager.reset();
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
    auto handle = _nextIbHandle++;
    std::string name = "ib_" + std::to_string(handle);

    _vkIndexBufferManager->createIndexBuffer(
        name,
        _vkCommandManager->getThreadCommandPool(),
        _vkSwapChainManager->getSwapchainCreateInfoKHR()->imageSharingMode,
        _queueDataFromExclusiveFlags.front()->queues.front(),
        std::move(indices),
        false
    );

    _ibNames[handle] = name;
    return handle;
}

TextureHandle VulkanRenderer::createSolidColorTexture(u8 r, u8 g, u8 b, u8 a)
{
    const u8 pixel[4] = {r, g, b, a};
    return createTextureFromPixels(pixel, 1, 1);
}

TextureHandle VulkanRenderer::createTextureFromPixels(const u8* rgbaPixels, u32 width, u32 height)
{
    if (!rgbaPixels || width == 0 || height == 0)
    {
        INK_ERROR << "VulkanRenderer: refusing to upload an empty texture";
        return INVALID_HANDLE;
    }

    auto handle = _nextTexHandle++;
    std::string name = "tex_" + std::to_string(handle);

    _vkTextureManager->createTextureFromPixels(name, rgbaPixels, width, height);
    _texNames[handle] = name;
    updateTextureDescriptorSets(handle);
    updateOverlay2DTextureDescriptorSets(handle);
    return handle;
}

TextureHandle VulkanRenderer::createDynamicTexture(u32 width, u32 height)
{
    if (width == 0 || height == 0)
    {
        INK_ERROR << "VulkanRenderer: refusing to allocate an empty dynamic texture";
        return INVALID_HANDLE;
    }

    auto handle = _nextTexHandle++;
    std::string name = "tex_" + std::to_string(handle);

    _vkTextureManager->createDynamicTexture(name, width, height);
    _texNames[handle] = name;

    /*
     * Descriptor sets are allocated once, here, and never again: the image,
     * view and sampler behind them stay put for the texture's whole life, so
     * later updateTextureRegion() calls need no descriptor work. That is what
     * keeps a growing glyph atlas from draining the descriptor pool.
     */
    updateTextureDescriptorSets(handle);
    updateOverlay2DTextureDescriptorSets(handle);
    return handle;
}

void VulkanRenderer::updateTextureRegion(TextureHandle handle, u32 x, u32 y,
                                         u32 width, u32 height, const u8* rgbaPixels)
{
    if (!_vkTextureManager) return;

    auto nameIt = _texNames.find(handle);
    if (nameIt == _texNames.end())
    {
        INK_ERROR << "VulkanRenderer: updateTextureRegion on an unknown texture";
        return;
    }

    _vkTextureManager->updateRegion(nameIt->second, x, y, width, height, rgbaPixels);
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

void VulkanRenderer::bindDrawState(VkCommandBuffer cmd)
{
    _vkGraphicsPipelineManager->cmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS);

    //! set 0 - view/projection, refreshed once per frame in beginRenderPass().
    if (_currentImageIndex < _descSets.size())
    {
        _vkGraphicsPipelineManager->cmdBindDescriptorSets(
            cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, 0, 1, &_descSets[_currentImageIndex], 0, nullptr);
    }

    // set 1 - the texture selected by the last bindTexture().
    auto texIt = _texDescSets.find(_currentTexture);
    if (texIt != _texDescSets.end() && _currentImageIndex < texIt->second.size())
    {
        _vkGraphicsPipelineManager->cmdBindDescriptorSets(
            cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, 1, 1, &texIt->second[_currentImageIndex], 0, nullptr);
    }

    // set 2 - directional light.
    if (_currentImageIndex < _lightDescSets.size())
    {
        _vkGraphicsPipelineManager->cmdBindDescriptorSets(
            cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, 2, 1, &_lightDescSets[_currentImageIndex], 0, nullptr);
    }

    /*
     * Per-draw transform. The model matrix travels as a push constant rather
     * than in the UBO, because the UBO is only written once per frame: pushing
     * here is what lets several objects with different transforms share a
     * single render pass.
     */
    PushConstantBlock pushConstants;
    pushConstants.model = _currentTransform.model;
    pushConstants.normalMatrix =
        glm::mat4(glm::transpose(glm::inverse(glm::mat3(_currentTransform.model))));
    _vkGraphicsPipelineManager->cmdPushConstants(cmd, &pushConstants);
}

void VulkanRenderer::drawIndexed(u32 indexCount, u32 instanceCount)
{
    if (!_frameBegun || !_renderPassActive || !_pipelineReady) 
        return;

    VkCommandBuffer cmd = _cmdBuffers[_currentFrame];
    bindDrawState(cmd);

    auto vbIt = _vbNames.find(_currentVertexBuffer);
    if (vbIt != _vbNames.end()) {
        auto vb = _vkVertexBufferManager->getVertexBuffer(vbIt->second);
        // vb.memoryOffset is VMA's suballocation offset within a shared
        // VkDeviceMemory block, not an offset into this VkBuffer — each mesh
        // owns its own dedicated buffer, so the bind offset is always 0.
        VkDeviceSize vertexOffset = 0;
        vkCmdBindVertexBuffers(cmd, 0, 1, &vb.buffer, &vertexOffset);
    }

    auto ibIt = _ibNames.find(_currentIndexBuffer);
    if (ibIt != _ibNames.end()) {
        auto ib = _vkIndexBufferManager->getIndexBuffer(ibIt->second);
        vkCmdBindIndexBuffer(cmd, ib.buffer, 0, ib.indexType);
    }

    _vkGraphicsPipelineManager->cmdIndexedDraw(
        cmd, *_vkSwapChainManager->getExtent2D(), indexCount, instanceCount, 0, 0, 0);
}

void VulkanRenderer::draw(u32 vertexCount, u32 instanceCount)
{
    if (!_frameBegun || !_renderPassActive || !_pipelineReady) 
        return;

    VkCommandBuffer cmd = _cmdBuffers[_currentFrame];
    bindDrawState(cmd);

    auto vbIt = _vbNames.find(_currentVertexBuffer);
    if (vbIt != _vbNames.end()) {
        auto vb = _vkVertexBufferManager->getVertexBuffer(vbIt->second);
        VkDeviceSize vertexOffset = 0;
        vkCmdBindVertexBuffers(cmd, 0, 1, &vb.buffer, &vertexOffset);
    }

    _vkGraphicsPipelineManager->cmdDraw(
        cmd, *_vkSwapChainManager->getExtent2D(), vertexCount, instanceCount, 0, 0);
}

void VulkanRenderer::ensureOverlay2DCapacity(u32 frame, VkDeviceSize vertexBytes, VkDeviceSize indexBytes)
{
    const VkSharingMode sharingMode =
        _vkSwapChainManager->getSwapchainCreateInfoKHR()->imageSharingMode;

    /*
     * Host-visible and persistently mapped: the batch is written once by the
     * CPU and read once by the GPU, so a staging copy would only add latency.
     * SEQUENTIAL_WRITE lets VMA pick write-combined memory for exactly that
     * access pattern.
     */
    constexpr VmaAllocationCreateFlags kDynamicFlags =
        VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;

    if (vertexBytes > _overlay2DVertexCapacity[frame])
    {
        if (_overlay2DVertexBuffers[frame].buffer != VK_NULL_HANDLE)
            _memoryManager->destroyBuffer(_overlay2DVertexBuffers[frame]);

        _overlay2DVertexBuffers[frame] = _memoryManager->createBuffer(
            vertexBytes, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, sharingMode,
            VMA_MEMORY_USAGE_AUTO, kDynamicFlags);
        _overlay2DVertexCapacity[frame] = vertexBytes;
    }

    if (indexBytes > _overlay2DIndexCapacity[frame])
    {
        if (_overlay2DIndexBuffers[frame].buffer != VK_NULL_HANDLE)
            _memoryManager->destroyBuffer(_overlay2DIndexBuffers[frame]);

        _overlay2DIndexBuffers[frame] = _memoryManager->createBuffer(
            indexBytes, VK_BUFFER_USAGE_INDEX_BUFFER_BIT, sharingMode,
            VMA_MEMORY_USAGE_AUTO, kDynamicFlags);
        _overlay2DIndexCapacity[frame] = indexBytes;
    }
}

void VulkanRenderer::destroyOverlay2DBuffers()
{
    if (!_memoryManager || !_memoryManager->isInitialized())
        return;

    for (u32 frame = 0; frame < MAX_FRAMES_IN_FLIGHT; ++frame)
    {
        if (_overlay2DVertexBuffers[frame].buffer != VK_NULL_HANDLE)
            _memoryManager->destroyBuffer(_overlay2DVertexBuffers[frame]);
        if (_overlay2DIndexBuffers[frame].buffer != VK_NULL_HANDLE)
            _memoryManager->destroyBuffer(_overlay2DIndexBuffers[frame]);

        _overlay2DVertexBuffers[frame] = {};
        _overlay2DIndexBuffers[frame] = {};
        _overlay2DVertexCapacity[frame] = 0;
        _overlay2DIndexCapacity[frame] = 0;
    }
}

void VulkanRenderer::drawBatch2D(std::span<const gfx::Vertex2D> vertices,
                                 std::span<const u32> indices,
                                 TextureHandle texture)
{
    if (!_frameBegun || !_renderPassActive || !_pipelineReady || !_vkOverlay2DPipelineManager)
        return;

    if (vertices.empty() || indices.empty())
        return;

    //! An untextured batch still samples, so stand in an opaque white texel.
    TextureHandle sampled = texture;
    if (!isValidHandle(sampled))
    {
        if (!isValidHandle(_white2DTexture))
            _white2DTexture = createSolidColorTexture(255, 255, 255, 255);
        sampled = _white2DTexture;
    }

    auto texIt = _tex2dDescSets.find(sampled);
    if (texIt == _tex2dDescSets.end() || _currentImageIndex >= texIt->second.size())
        return; //! No overlay-layout descriptor set for this texture.

    const VkDeviceSize vertexBytes = static_cast<VkDeviceSize>(vertices.size_bytes());
    const VkDeviceSize indexBytes = static_cast<VkDeviceSize>(indices.size_bytes());

    ensureOverlay2DCapacity(_currentFrame, vertexBytes, indexBytes);

    AllocatedBuffer& vertexBuffer = _overlay2DVertexBuffers[_currentFrame];
    AllocatedBuffer& indexBuffer = _overlay2DIndexBuffers[_currentFrame];
    if (!vertexBuffer.mappedData || !indexBuffer.mappedData)
        return;

    std::memcpy(vertexBuffer.mappedData, vertices.data(), static_cast<size_t>(vertexBytes));
    std::memcpy(indexBuffer.mappedData, indices.data(), static_cast<size_t>(indexBytes));

    VkCommandBuffer cmd = _cmdBuffers[_currentFrame];
    const VkExtent2D extent = *_vkSwapChainManager->getExtent2D();

    _vkOverlay2DPipelineManager->cmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS);
    _vkOverlay2DPipelineManager->cmdBindDescriptorSets(
        cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, 0, 1, &texIt->second[_currentImageIndex], 0, nullptr);

    /*
     * Window pixels -> clip space. cmdIndexedDraw() sets a negative-height
     * viewport (see its comment) so every Vulkan draw shares OpenGL/GLM's
     * Y-up NDC convention; bottom=height/top=0 is exactly the same swap the
     * OpenGL overlay path uses for that reason. _ZO because Vulkan's depth
     * range is [0,1]; the actual depth is irrelevant with the test disabled.
     */
    const glm::mat4 projection = glm::orthoRH_ZO(
        0.0f, static_cast<f32>(extent.width),
        static_cast<f32>(extent.height), 0.0f,
        0.0f, 1.0f);

    _vkOverlay2DPipelineManager->cmdPushConstants(cmd, &projection);

    const VkDeviceSize vertexOffset = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &vertexBuffer.buffer, &vertexOffset);
    vkCmdBindIndexBuffer(cmd, indexBuffer.buffer, 0, VK_INDEX_TYPE_UINT32);

    //! The whole batch in one call -- the point of the exercise.
    _vkOverlay2DPipelineManager->cmdIndexedDraw(
        cmd, extent, static_cast<u32>(indices.size()), 1, 0, 0, 0);

    /*
     * No state is restored here: every 3D draw re-binds the scene pipeline and
     * its descriptor sets in bindDrawState(), so the overlay cannot leak into
     * one.
     */
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
