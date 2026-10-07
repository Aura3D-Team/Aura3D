#include "aura/Renderer/Vulkan/VulkanRenderer.h"

#include <algorithm>
#include <bit>
#include <chrono>
#include <cstring>
#include <functional>
#include <system_error>
#include <thread>

#include <ink/ParallelProcessor.h>

#include "aura/Core/AuraException/AuraException.h"
#include "aura/Core/Profiling/FrameProfiler.h"
#include "aura/Renderer/Vulkan/VkAura/EmbeddedSpirv.h"
#include "aura/aura.h"

namespace aura3d
{
namespace vk
{

namespace
{

[[nodiscard]] VkVertexInputBindingDescription batchBindingDescription() noexcept
{
    VkVertexInputBindingDescription binding{};
    binding.binding = 0;
    binding.stride = sizeof(gfx::BatchVertex);
    binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    return binding;
}

[[nodiscard]] AttributeDescriptionArray<VkVertexInputAttributeDescription> batchAttributeDescriptions() noexcept
{
    AttributeDescriptionArray<VkVertexInputAttributeDescription> attributes{};

    attributes[0].binding = 0;
    attributes[0].location = 0;
    attributes[0].format = VK_FORMAT_R32G32B32_SFLOAT;
    attributes[0].offset = offsetof(gfx::BatchVertex, pos);

    attributes[1].binding = 0;
    attributes[1].location = 1;
    attributes[1].format = VK_FORMAT_R32G32_SFLOAT;
    attributes[1].offset = offsetof(gfx::BatchVertex, texCoord);

    attributes[2].binding = 0;
    attributes[2].location = 2;
    attributes[2].format = VK_FORMAT_R32G32B32A32_SFLOAT;
    attributes[2].offset = offsetof(gfx::BatchVertex, color);

    return attributes;
}

//! Camera slots per frame in flight; a frame's cameras past this overwrite the last slot.
constexpr u32 kTransformSlots = 1024;

void declareBatchInterface(VkGraphicsPipelineManager &pipeline, u32 bindlessTextureCapacity)
{
    pipeline.resetInterface();

    DescriptorBindingInfo sampler;
    sampler.binding = 0;
    sampler.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    sampler.descriptorCount = bindlessTextureCapacity;
    sampler.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    sampler.bindingFlags = VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT | VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT;
    pipeline.addDescriptorBinding(0, sampler);

    pipeline.setPushConstantRange(VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(BatchPushConstants));
}

//! Largest power-of-two sample count that is both <= `requested` and
//! supported by the device. Falls back to 1x for a nonsensical or
//! unsupported request rather than rejecting it.
[[nodiscard]] VkSampleCountFlagBits resolveSampleCount(int requested, VkSampleCountFlagBits deviceMax) noexcept
{
    VkSampleCountFlagBits resolved = VK_SAMPLE_COUNT_1_BIT;
    for (VkSampleCountFlagBits candidate : {VK_SAMPLE_COUNT_2_BIT, VK_SAMPLE_COUNT_4_BIT, VK_SAMPLE_COUNT_8_BIT,
                                            VK_SAMPLE_COUNT_16_BIT, VK_SAMPLE_COUNT_32_BIT, VK_SAMPLE_COUNT_64_BIT})
    {
        if (static_cast<int>(candidate) <= requested && candidate <= deviceMax)
            resolved = candidate;
    }
    return resolved;
}

/**
 * @brief Declares the 3D scene pipeline interface: set 0 transform UBO, set 1
 *        bindless texture array, set 2 light UBO, and the model/normalMatrix
 *        push constant.
 *
 * The constructor's own _init() declares set 1 as a single, non-bindless
 * sampler; this replaces it. Used both by createResourceManagers() (the
 * pipeline built by default) and setupPipeline() (a hot-swapped custom
 * pipeline), so that either path's set 1 stays layout-compatible with the
 * persistent _bindlessTextureSet allocated once in createDescriptorSets() --
 * binding a descriptor set to a structurally different layout is invalid.
 */
void declareScene3DInterface(VkGraphicsPipelineManager &pipeline, u32 bindlessTextureCapacity)
{
    pipeline.resetInterface();

    //! Dynamic: each camera of a frame is a slot of one buffer, chosen at bind time.
    DescriptorBindingInfo uboBinding;
    uboBinding.binding = 0;
    uboBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
    uboBinding.descriptorCount = 1;
    uboBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    pipeline.addDescriptorBinding(0, uboBinding);

    DescriptorBindingInfo bindlessSampler3D;
    bindlessSampler3D.binding = 0;
    bindlessSampler3D.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindlessSampler3D.descriptorCount = bindlessTextureCapacity;
    bindlessSampler3D.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    bindlessSampler3D.bindingFlags =
        VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT | VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT;
    pipeline.addDescriptorBinding(1, bindlessSampler3D);

    // set 2: directional light, read by the fragment stage.
    DescriptorBindingInfo lightBinding;
    lightBinding.binding = 0;
    lightBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    lightBinding.descriptorCount = 1;
    lightBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    pipeline.addDescriptorBinding(2, lightBinding);

    pipeline.setPushConstantRange(VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(PushConstantBlock));
}

} // namespace

VulkanRenderer::VulkanRenderer(const wma::WindowDetails &windowDetails)
    : IRenderer(windowDetails), _vkInstanceData({}), _vkDeviceData({}), _vkImageViewData({})
{
    INK_INFO << "Renderer - VULKAN";

    _vkInstanceData.appName = "Aura3D";
    _vkInstanceData.engineName = "Aura3DEngine";
    _vkInstanceData.appVersion = {1, 0, 0};
    _vkInstanceData.vkInstanceExtensions = {};
    _vkInstanceData.vkValidationLayers = {"VK_LAYER_KHRONOS_validation"};

    _vkDeviceData.vkDeviceExtensions = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    _vkDeviceData.vkEnabledLayers = {};
    _vkDeviceData.concurrentQueueFlags = {};
    _vkDeviceData.exclusiveQueueFlags = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT | VK_QUEUE_TRANSFER_BIT;

    _vkImageViewData = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
}

VulkanRenderer::~VulkanRenderer()
{
    if (_vkDeviceManager && _vkDeviceManager->getDevice())
        vkDeviceWaitIdle(*_vkDeviceManager->getDevice());
    cleanup();
}

void VulkanRenderer::initialize(AuraSettings *settings, const JobSystem *jobs)
{
    if (_isInitialized)
        return;

    //! Unused here: draw-call recording has its own dedicated pool
    //! (_recordPool below), sized and shaped for per-thread Vulkan command
    //! pools rather than JobSystem's generic band dispatch. See CPURenderer
    //! for the backend that does share the engine's pool.
    (void)jobs;

    const bool enableValidation = settings->getValidationLayers();

    _vmaConfig = VulkanMemoryManager::loadConfig(settings);
    _memoryManager = std::make_unique<VulkanMemoryManager>();

    createWindow(settings->getWindowTitle().c_str(), settings->getWindowBackend());
    createCoreObjects(enableValidation);

    // Only actually request it from VMA if the device genuinely supports it
    // (see VkDeviceManager::supportsBufferDeviceAddress)
    _vmaConfig.bufferDeviceAddress = _vmaConfig.bufferDeviceAddress && _vkDeviceManager->supportsBufferDeviceAddress();

    _msaaSamples = resolveSampleCount(settings->getMsaaSamples(), _vkDeviceManager->getMaxUsableSampleCount());

    createResourceManagers();
    buildSwapchainResources();
    createUniformBuffers();
    createDescriptorSets();

    /*
     * Reserves texture-array slot 0 (TextureHandle 1, guaranteed since this is
     * the very first texture created) for a fallback opaque-white texture.
     * bindTexture() given an invalid handle and drawBatch()'s untextured-batch
     * case both resolve to this same slot (see textureArrayIndexOf()), so a draw
     * that never bound a texture samples a slot that is always written,
     * instead of one descriptorBindingPartiallyBound only permits leaving
     * unwritten -- not dynamically sampling.
     */
    _fallbackTexture = createSolidColorTexture(255, 255, 255, 255);

    setupCommandBuffers();

    _isInitialized = true;
}

void VulkanRenderer::createWindow(const char *title, const wma::WindowBackend &wBackend)
{
    _windowManagerApi = makeWindow(wBackend, _windowDetails, wma::GraphicsAPI::Vulkan);
    _windowManagerApi->createWindow(title);
}

void VulkanRenderer::createCoreObjects(bool enableValidation)
{
    for (const char *ext : _windowManagerApi->getVulkanExtensions())
        _vkInstanceData.vkInstanceExtensions.push_back(ext);

    _vkInstance = std::make_unique<VkInstanceManager>(_vkInstanceData, enableValidation);
    _vkDeviceManager = std::make_unique<VkDeviceManager>(_vkInstance->getVkInstance(), _vkDeviceData);

    // A window handle isn't necessarily safe to build a surface from the moment
    // it exists, on Android the OS can have released the underlying native
    // window already, and the platform driver crashes on it rather than
    // failing cleanly. wma owns that platform knowledge; see
    // IWindowManager::waitUntilWindowReady.
    if (!_windowManagerApi->waitUntilWindowReady())
        throw std::runtime_error("VulkanRenderer: window never became ready for surface creation");

    _vkSurfaceManager = std::make_unique<VkSurfaceManager>(
        _vkInstance->getVkInstance(), _windowManagerApi->getBackendType(), _windowManagerApi->getWindowInstance(),
        _windowManagerApi->getNativeDisplayHandle());

    _memoryManager->initialize(*_vkInstance->getVkInstance(), *_vkDeviceManager->getPhysicalDevice(),
                               *_vkDeviceManager->getDevice(), _vmaConfig);

    _queueDataFromExclusiveFlags = _vkDeviceManager->getQueueManager()->getQueues(_vkDeviceData.exclusiveQueueFlags);
    _graphicsIndexFamily = VkQueueManager::findQueueFamilyIndex(
        *_vkDeviceManager->getPhysicalDevice(), _vkDeviceData.exclusiveQueueFlags, *_vkSurfaceManager->getSurface());
    if (_graphicsIndexFamily == VK_QUEUE_FAMILY_IGNORED)
        throw std::runtime_error("Vulkan device cannot present to this window surface");

#ifdef AURA_ENABLE_DEBUG_MODE
    //! The raw device-memory counters come from VMA's callbacks regardless;
    //! this is what adds the suballocation and heap-budget detail.
    _debugMetrics.setAllocator(_memoryManager->getAllocator());
#endif
}

void VulkanRenderer::createResourceManagers()
{
    VkDevice *dev = _vkDeviceManager->getDevice();

    //! Resolved against this device's update-after-bind limits, not the
    //! kDesiredBindlessTextures constant -- see maxBindlessTextures(). Cached
    //! here so the pipeline layouts, the pool and the per-draw bounds check in
    //! textureArrayIndexOf() can never disagree about the table's size.
    _bindlessTextureCapacity = _vkDeviceManager->maxBindlessTextures();

    _vkSwapChainManager = std::make_unique<VkSwapChainManager>(*_vkDeviceManager->getPhysicalDevice(), dev,
                                                               *_vkSurfaceManager->getSurface());
    _vkImageViewsManager = std::make_unique<VkImageViewsManager>(dev);
    _vkRenderPassManager = std::make_unique<VkRenderPassManager>(dev);
    _vkFrameBuffersManager = std::make_unique<VkFrameBuffersManager>(dev);
    _vkDescriptorManager = std::make_unique<VkDescriptorManager>(dev, _bindlessTextureCapacity);

    _vkGraphicsPipelineManager = std::make_unique<VkGraphicsPipelineManager>(
        vk_shader3d_vert, vk_shader3d_vert_len, vk_shader3d_frag, vk_shader3d_frag_len, dev);
    declareScene3DInterface(*_vkGraphicsPipelineManager, _bindlessTextureCapacity);

    _vkBatchPipelineManager = std::make_unique<VkGraphicsPipelineManager>(vk_batch_vert, vk_batch_vert_len,
                                                                          vk_batch_frag, vk_batch_frag_len, dev);
    declareBatchInterface(*_vkBatchPipelineManager, _bindlessTextureCapacity);

    _vkVertexBufferManager = std::make_unique<VkVertexBufferManager>(_memoryManager.get(), dev);
    _vkIndexBufferManager = std::make_unique<VkIndexBufferManager>(_memoryManager.get(), dev);
    _vkUniformBufferManager = std::make_unique<VkUniformBufferManager>(_memoryManager.get(), dev);
    _vkLightUniformBufferManager = std::make_unique<VkUniformBufferManager>(_memoryManager.get(), dev);
    _vkCommandManager = std::make_unique<VkCommandManager>(dev, _graphicsIndexFamily);

    _vkTextureManager =
        std::make_unique<VkTextureManager>(_memoryManager.get(), dev, _vkCommandManager->getThreadCommandPool(),
                                           _queueDataFromExclusiveFlags.front()->queues.front());

    _vkRenderSyncManager = std::make_unique<VkRenderSyncManager>(dev);

    //! Zero configured workers selects hardware concurrency.
    const int configuredThreads = AuraSettings::get()->getCpuThreads();
    const unsigned detected = std::thread::hardware_concurrency();
    _recordWorkerCount = configuredThreads > 0 ? static_cast<u32>(configuredThreads) : (detected > 0 ? detected : 1u);

    //! The pool is built by the first drawMeshes() large enough to use it, so a
    //! scene that never reaches that size runs without the worker threads.
    INK_VERBOSE << "Command recording workers: " << _recordWorkerCount;
}

void VulkanRenderer::setupPipeline(const std::string &vertShaderPath, const std::string &fragShaderPath)
{
    _vkGraphicsPipelineManager =
        std::make_unique<VkGraphicsPipelineManager>(vertShaderPath, fragShaderPath, _vkDeviceManager->getDevice());
    //! Must match createResourceManagers()'s pipeline exactly: set 1 has to
    //! stay layout-compatible with the already-allocated, persistent
    //! _bindlessTextureSet, which this custom pipeline does not reallocate.
    declareScene3DInterface(*_vkGraphicsPipelineManager, _bindlessTextureCapacity);
    if (_isInitialized)
    {
        createDescriptorSets();
    }
}

void VulkanRenderer::buildSwapchainResources()
{
    const auto framebuffer = _windowManagerApi->getFramebufferSize();
    auto drawableDetails = *_windowManagerApi->getWindowDetails();
    drawableDetails.width = framebuffer.width;
    drawableDetails.height = framebuffer.height;
    _vkSwapChainManager->createSwapChain(&drawableDetails, *_vkSurfaceManager->getSurface(), _vkDeviceManager.get(), 2,
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

void VulkanRenderer::createUniformBuffers()
{
    const VkSharingMode sharingMode = _vkSwapChainManager->getSwapchainCreateInfoKHR()->imageSharingMode;

    /*
     * One set per frame in flight, not per swapchain image.
     *
     * These are written by the CPU every frame, so what has to be guaranteed
     * is that the GPU is finished reading the copy being overwritten and
     * the only thing that guarantees that here is the per-frame fence waited
     * on at the top of beginFrame(). That fence is indexed by frame slot, so
     * the buffers it protects must be too.
     */
    const u32 framesInFlight = GetMaxFramesInFlight();

    const VkPhysicalDeviceProperties *properties = nullptr;
    vmaGetPhysicalDeviceProperties(_memoryManager->getAllocator(), &properties);
    _vkUniformBufferManager->createUniformBuffers(sharingMode, framesInFlight, sizeof(gfx::TransformUBO),
                                                  kTransformSlots, properties->limits.minUniformBufferOffsetAlignment);

    for (u32 i = 0; i < framesInFlight; ++i)
    {
        _vkUniformBufferManager->updateUniformBuffer(i, _currentTransform);
    }
    _transformSlotsUsed = 0;

    _vkLightUniformBufferManager->createUniformBuffers(sharingMode, framesInFlight, sizeof(gfx::LightUBO));

    updateLightUniformBuffers();
}

void VulkanRenderer::updateLightUniformBuffers()
{
    if (!_vkLightUniformBufferManager)
        return;

    //! Every frame slot's copy, for the reason given in createUniformBuffers():
    //! the light is set rarely and read every frame, so all slots are refreshed
    //! rather than tracking which ones are stale.
    for (u32 i = 0, frames = GetMaxFramesInFlight(); i < frames; ++i)
        _vkLightUniformBufferManager->updateUniformBufferRaw(i, &_light, sizeof(gfx::LightUBO));
}

void VulkanRenderer::setLight(const gfx::LightUBO &light)
{
    IRenderer::setLight(light);
    updateLightUniformBuffers();
}

void VulkanRenderer::createDescriptorSets()
{
    if (!_vkGraphicsPipelineManager)
        return;

    _vkGraphicsPipelineManager->createDescriptorSetLayouts();

    //! Allocated exactly once and shared by both pipelines, so every texture survives
    //! swapchain rebuilds and is written once. From the batch layout, which outlives
    //! the scene manager setupPipeline() can replace; the two are identically defined.
    if (_bindlessTextureSet == VK_NULL_HANDLE)
    {
        _vkBatchPipelineManager->createDescriptorSetLayouts();
        _bindlessTextureSet =
            _vkDescriptorManager->allocateDescriptorSet(_vkBatchPipelineManager->getDescriptorSetLayout(0));
    }

    const u32 framesInFlight = GetMaxFramesInFlight();

    //! Per frame in flight, matching the buffers they describe.
    _descSets.resize(framesInFlight);
    _lightDescSets.resize(framesInFlight);

    // Resizing to the same frame count preserves buffers across swapchain rebuilds.
    _batchVertexBuffers.resize(framesInFlight);
    _batchIndexBuffers.resize(framesInFlight);
    _batchVertexCapacity.resize(framesInFlight);
    _batchIndexCapacity.resize(framesInFlight);
    _batchVertexUsed.resize(framesInFlight);
    _batchIndexUsed.resize(framesInFlight);
    _batchRetiredBuffers.resize(framesInFlight);

    for (u32 i = 0; i < framesInFlight; ++i)
    {
        _descSets[i] =
            _vkDescriptorManager->allocateDescriptorSet(_vkGraphicsPipelineManager->getDescriptorSetLayout(0));

        VkDescriptorBufferInfo bufInfo = _vkUniformBufferManager->getDescriptorBufferInfo(i);
        _vkDescriptorManager->updateDescriptorSet(_descSets[i], 0, bufInfo.buffer, bufInfo.range, bufInfo.offset,
                                                  VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC);

        _lightDescSets[i] =
            _vkDescriptorManager->allocateDescriptorSet(_vkGraphicsPipelineManager->getDescriptorSetLayout(2));

        VkDescriptorBufferInfo lightInfo = _vkLightUniformBufferManager->getDescriptorBufferInfo(i);
        _vkDescriptorManager->updateDescriptorSet(_lightDescSets[i], 0, lightInfo.buffer, lightInfo.range,
                                                  lightInfo.offset);
    }

    std::vector<VkVertexInputBindingDescription> bindings = {VkVertexBufferManager::getBindingDescription()};
    auto attributes = VkVertexBufferManager::getAttributeDescriptions();

    PipelineOptions sceneOptions{};
    sceneOptions.depthTest = true;
    sceneOptions.cullBackFaces = true;
    sceneOptions.sampleCount = _msaaSamples;

    _vkGraphicsPipelineManager->createPipeline(*_vkRenderPassManager->getRenderPass(),
                                               *_vkSwapChainManager->getExtent2D(), bindings, attributes,
                                               VkVertexBufferManager::getAttributeDescriptionCount(), sceneOptions);

    createBatchPipeline();

    _pipelineReady = true;
}

void VulkanRenderer::createBatchPipeline()
{
    // Screen batches sit on the near plane; world batches test existing depth.
    // Blended texels never occlude subsequent geometry through depth writes.
    PipelineOptions options{};
    options.depthTest = true;
    options.depthWrite = false;
    options.depthCompare = VK_COMPARE_OP_LESS_OR_EQUAL;
    options.alphaBlend = true;
    options.sampleCount = _msaaSamples;
    _vkBatchPipelineManager->createPipeline(*_vkRenderPassManager->getRenderPass(), *_vkSwapChainManager->getExtent2D(),
                                            {batchBindingDescription()}, batchAttributeDescriptions(),
                                            MAX_ATTRIBUTE_DESCRIPTION_BATCH, options);
}

void VulkanRenderer::publishTexture(TextureHandle textureHandle)
{
    if (!isValidHandle(textureHandle))
        return;

    /*
     * A handle past the table's end cannot be sampled (see
     * textureArrayIndexOf(), which substitutes the fallback slot rather than
     * reading out of bounds). Creation is the only place that knows *which*
     * texture overflowed, so it is the only place the warning is actionable
     * -- and it is emitted once here rather than per-draw, which would flood
     * the log at frame rate.
     *
     * Deliberately a plain range test rather than a comparison against
     * _fallbackTexture: the fallback is itself published from inside its own
     * createSolidColorTexture() call, before initialize() has assigned
     * _fallbackTexture, so any check reading that member here sees an
     * invalid handle and would wrongly reject slot 0 -- leaving the one slot
     * every unbound draw samples permanently unwritten.
     */
    const u32 slot = textureHandle.value() - 1u;
    if (slot >= _bindlessTextureCapacity)
    {
        INK_WARN << "Bindless texture table full (" << _bindlessTextureCapacity << " slots); texture " << textureHandle
                 << " will render with the fallback texture.";
        return;
    }

    updateTextureDescriptorSets(textureHandle);
}

void VulkanRenderer::updateTextureDescriptorSets(TextureHandle textureHandle)
{
    //! The table outlives swapchain rebuilds, so a texture created mid-rebuild is still written.
    if (_bindlessTextureSet == VK_NULL_HANDLE)
        return;

    const auto *texture = _vkTextureManager->getTexture(textureHandle.value());
    if (!texture)
        return;

    _vkDescriptorManager->updateTextureArrayElement(_bindlessTextureSet, 0, textureArrayIndexOf(textureHandle),
                                                    texture->view, texture->sampler);
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

    VkImageCreateInfo imgInfo = {};
    imgInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imgInfo.imageType = VK_IMAGE_TYPE_2D;
    imgInfo.format = _depth.format;
    imgInfo.extent = {extent.width, extent.height, 1};
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

    VkImageCreateInfo imgInfo = {};
    imgInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imgInfo.imageType = VK_IMAGE_TYPE_2D;
    imgInfo.format = colorFormat;
    imgInfo.extent = {extent.width, extent.height, 1};
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
    if (!_msaaColor.isValid())
        return;

    _vkImageViewsManager->cleanupColorMsaaImageView();

    AllocatedImage colorImage{_msaaColor.image, _msaaColor.allocation};
    _memoryManager->destroyImage(colorImage);
    _msaaColor.reset();
}

void VulkanRenderer::setupCommandBuffers()
{
    _cmdBuffers = _vkCommandManager->createCommandBuffer();
    _vkRenderSyncManager->create(_imagesCount);
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

void VulkanRenderer::cleanup()
{
    if (_vkDeviceManager && _vkDeviceManager->getDevice())
        vkDeviceWaitIdle(*_vkDeviceManager->getDevice());

    /*
     * Before anything Vulkan is torn down. Destroying the pool joins its
     * worker threads, and those threads own command pools registered in
     * VkCommandManager -- letting the manager (and the device under it) go
     * first would leave them holding handles to a destroyed device.
     */
    _recordPool.reset();
    _chunkCmds.clear();
    _resolvedDraws.clear();

    clearSharedResources();

    _descSets.clear();
    _lightDescSets.clear();
    _bindlessTextureSet = VK_NULL_HANDLE;
    _vbNames.clear();
    _ibNames.clear();
    _pipelineReady = false;
    _renderPassActive = false;
    _frameBegun = false;
    _fallbackTexture = {};

#ifdef AURA_PROFILE_FRAME
    //! A device object, so it goes before the device does.
    _gpuTimer.destroy();
    _gpuTimerUnsupported = false;
#endif

#ifdef AURA_ENABLE_DEBUG_MODE
    /*
     * The allocator handle has to be dropped before vmaDestroyAllocator below,
     * since a report built afterwards would call vmaCalculateStatistics on a
     * destroyed allocator. The cumulative counters survive -- they live in
     * VkDeviceMemoryCounters, not here, which is what lets a report written
     * after teardown still show what the run allocated.
     */
    _debugMetrics.setAllocator(VK_NULL_HANDLE);
#endif

    //! Before the allocator shuts down below, since these hold VMA allocations.
    destroyBatchBuffers();

    if (_vkVertexBufferManager)
        _vkVertexBufferManager->cleanup();
    if (_vkIndexBufferManager)
        _vkIndexBufferManager->cleanup();
    if (_vkUniformBufferManager)
        _vkUniformBufferManager->cleanup();
    if (_vkLightUniformBufferManager)
        _vkLightUniformBufferManager->cleanup();
    if (_vkTextureManager)
        _vkTextureManager->cleanup();
    if (_depth.isValid())
        destroyDepthResources();
    if (_msaaColor.isValid())
        destroyMsaaColorResources();

    _vkDescriptorManager.reset();
    _vkFrameBuffersManager.reset();
    _vkBatchPipelineManager.reset();
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

    if (_memoryManager)
        _memoryManager->shutdown();

    _vkSurfaceManager.reset();
    _vkDeviceManager.reset();
    _vkInstance.reset();
    _memoryManager.reset();
    _windowManagerApi.reset();

    _isInitialized = false;
}

VertexBufferHandle VulkanRenderer::createVertexBuffer(std::vector<gfx::Vertex3D> &&vertices)
{
    auto handle = _nextVbHandle++;
    std::string name = "vb_" + std::to_string(handle.value());

    _vkVertexBufferManager->createVertexBuffer(name, _vkCommandManager->getThreadCommandPool(),
                                               _vkSwapChainManager->getSwapchainCreateInfoKHR()->imageSharingMode,
                                               _queueDataFromExclusiveFlags.front()->queues.front(),
                                               std::move(vertices), false);

    _vbNames[handle] = name;
    //! Resolve once here so the draw path never has to (see _vbByHandle).
    _vbByHandle.resize(handle.value());
    _vbByHandle[handle.value() - 1] = _vkVertexBufferManager->getVertexBuffer(name);
    return handle;
}

IndexBufferHandle VulkanRenderer::createIndexBuffer(std::vector<u16> &&indices)
{
    auto handle = _nextIbHandle++;
    std::string name = "ib_" + std::to_string(handle.value());

    _vkIndexBufferManager->createIndexBuffer(name, _vkCommandManager->getThreadCommandPool(),
                                             _vkSwapChainManager->getSwapchainCreateInfoKHR()->imageSharingMode,
                                             _queueDataFromExclusiveFlags.front()->queues.front(), std::move(indices),
                                             false);

    _ibNames[handle] = name;
    //! Resolve once here so the draw path never has to (see _ibByHandle).
    _ibByHandle.resize(handle.value());
    _ibByHandle[handle.value() - 1] = _vkIndexBufferManager->getIndexBuffer(name);
    return handle;
}

IndexBufferHandle VulkanRenderer::createIndexBuffer(std::vector<u32> &&indices)
{
    auto handle = _nextIbHandle++;
    std::string name = "ib_" + std::to_string(handle.value());

    _vkIndexBufferManager->createIndexBuffer(name, _vkCommandManager->getThreadCommandPool(),
                                             _vkSwapChainManager->getSwapchainCreateInfoKHR()->imageSharingMode,
                                             _queueDataFromExclusiveFlags.front()->queues.front(), std::move(indices),
                                             false);

    _ibNames[handle] = name;
    //! Resolve once here so the draw path never has to (see _ibByHandle).
    _ibByHandle.resize(handle.value());
    _ibByHandle[handle.value() - 1] = _vkIndexBufferManager->getIndexBuffer(name);
    return handle;
}

TextureHandle VulkanRenderer::createSolidColorTexture(u8 r, u8 g, u8 b, u8 a)
{
    const u8 pixel[4] = {r, g, b, a};
    return createTextureFromPixels(pixel, 1, 1);
}

TextureHandle VulkanRenderer::createTextureFromPixels(const u8 *rgbaPixels, u32 width, u32 height)
{
    if (!rgbaPixels || width == 0 || height == 0)
    {
        INK_ERROR << "VulkanRenderer: refusing to upload an empty texture";
        return {};
    }

    //! The manager issues the dense id itself, and the renderer adopts it as
    //! the public handle -- so a handle indexes straight into its storage.
    const TextureHandle handle{_vkTextureManager->createTextureFromPixels(rgbaPixels, width, height)};
    if (handle.value() == VkTextureManager::kInvalidTextureId)
        return {};

    publishTexture(handle);
    return handle;
}

TextureHandle VulkanRenderer::createDynamicTexture(u32 width, u32 height)
{
    if (width == 0 || height == 0)
    {
        INK_ERROR << "VulkanRenderer: refusing to allocate an empty dynamic texture";
        return {};
    }

    const TextureHandle handle{_vkTextureManager->createDynamicTexture(width, height)};
    if (handle.value() == VkTextureManager::kInvalidTextureId)
        return {};

    /*
     * Descriptor sets are allocated once, here, and never again: the image,
     * view and sampler behind them stay put for the texture's whole life, so
     * later updateTextureRegion() calls need no descriptor work. That is what
     * keeps a growing glyph atlas from draining the descriptor pool.
     */
    publishTexture(handle);
    return handle;
}

void VulkanRenderer::updateTextureRegion(TextureHandle handle, u32 x, u32 y, u32 width, u32 height,
                                         const u8 *rgbaPixels)
{
    if (!_vkTextureManager)
        return;

    _vkTextureManager->updateRegion(handle.value(), x, y, width, height, rgbaPixels);
}

TextureHandle VulkanRenderer::createCoverageTexture(u32 width, u32 height)
{
    if (!_vkTextureManager)
        return {};

    const TextureHandle handle{_vkTextureManager->createCoverageTexture(width, height)};
    if (handle.value() == VkTextureManager::kInvalidTextureId)
        return {};

    publishTexture(handle);
    return handle;
}

void VulkanRenderer::updateCoverageTextureRegion(TextureHandle handle, u32 x, u32 y, u32 width, u32 height,
                                                 const u8 *coverage)
{
    if (_vkTextureManager)
        _vkTextureManager->updateCoverageRegion(handle.value(), x, y, width, height, coverage);
}

void VulkanRenderer::beginFrame()
{
    _frameBegun = false;
    _renderPassActive = false;

    auto *windowFlags = _windowManagerApi->getWindowFlags();
    if (!windowFlags)
        return;

    // While backgrounded (Android onPause/minimize), the ANativeWindow can be
    // torn down at any moment. Skip Vulkan entirely rather than racing
    // acquire/present against a surface mid-teardown: vkAcquireNextImageKHR is
    // called with an unbounded timeout below, and blocking in it here would
    // stall this thread for as long as Android keeps the app backgrounded --
    // which is also the thread nativePause() on the UI thread is waiting on,
    // turning a normal minimize into an ANR-driven kill.
    if (windowFlags->minimized)
    {
        return;
    }

    if (windowFlags->surfaceLost)
    {
        windowFlags->surfaceLost = false;
        _targetStale = true;

        if (!_windowManagerApi->isSurfaceAvailable())
        {
            return;
        }

        if (!_windowManagerApi->waitUntilWindowReady())
        {
            return;
        }

        try
        {
            recreateSurfaceAndSwapchain();
        }
        catch (const AuraException &e)
        {
            // The freshly (re)created VkSurfaceKHR can still be bound to an
            // ANativeWindow/BufferQueue that Android is mid-abandoning
            // waitUntilWindowReady() only checks the window pointer is
            // stable, not that Vulkan can actually query it yet. Retry via
            // the same surfaceLost path next frame instead of letting this
            // escape the render loop and abort the process.
            INK_ERROR << "recreateSurfaceAndSwapchain failed, will retry: " << e.what();
            windowFlags->surfaceLost = true;
        }

        return;
    }

    if (windowFlags->resized)
    {
        windowFlags->resized = false;
        _targetStale = true;

        try
        {
            handleWindowChanges();
        }
        catch (const AuraException &e)
        {
            // A plain resize can reuse a VkSurfaceKHR whose backing window
            // already died out from under it e.g. the transient portrait
            // relayout Android does mid-resume on an orientation-locked
            // Activity, which fires a resize without ever tripping
            // surfaceLost first. Only surfaceLost's path actually rebuilds
            // the surface (not just the swapchain), so route recovery
            // through it rather than letting the exception reach the top of
            // the render loop and abort the process.
            INK_ERROR << "handleWindowChanges failed, forcing surface recreation: " << e.what();
            windowFlags->surfaceLost = true;
        }

        return;
    }

    if (!_pipelineReady)
    {
        return;
    }

    {
        AURA_FRAME_SCOPE(FramePhase::WaitFence);
        _vkRenderSyncManager->waitForFences(_currentFrame);
    }

    /*
     * Immediately after the fence wait and nowhere else. This slot's previous
     * submission has just been proven complete, so its two timestamps are
     * guaranteed readable and the read costs nothing; asking for them any
     * earlier would mean blocking the CPU on the GPU purely to measure it.
     * The reported GPU time therefore trails by the frames in flight.
     */
#ifdef AURA_PROFILE_FRAME
    _gpuTimer.resolve(_currentFrame);
    if (_gpuTimingEnabled && !_gpuTimer.isReady() && !_gpuTimerUnsupported)
    {
        //! Per queue family: a family reporting no timestampValidBits cannot write them at all.
        _gpuTimerUnsupported =
            !_gpuTimer.initialize(*_vkDeviceManager->getDevice(), *_vkDeviceManager->getPhysicalDevice(),
                                  _graphicsIndexFamily, GetMaxFramesInFlight());
    }
#endif

    u32 imageIndex = 0;
    {
        AURA_FRAME_SCOPE(FramePhase::Acquire);
        imageIndex = _vkSwapChainManager->acquireNextImage(
            _vkRenderSyncManager->getImageAvailableSemaphores()[_currentFrame], windowFlags);
    }

    if (imageIndex >= _imagesCount)
    {
        return;
    }

    _currentImageIndex = imageIndex;
    _frameBegun = true;

    _vkRenderSyncManager->resetFences(_currentFrame);

    /*
     * Safe only because the fence wait above has already retired this frame
     * slot's previous submission: the reset recycles every command buffer in
     * the frame's pools, on every thread that recorded into them.
     */
    _vkCommandManager->resetRenderPools(_currentFrame);

    VkCommandBuffer cmd = _cmdBuffers[_currentFrame];
    VkCommandManager::beginCommandBuffer(cmd);

#ifdef AURA_PROFILE_FRAME
    //! The first command in the frame's primary buffer, so the opening
    //! timestamp brackets everything the GPU does for this frame.
    if (_gpuTimingEnabled)
        _gpuTimer.writeBegin(cmd, _currentFrame);
#endif

    //! A reset pool discards every recorded bind, so nothing may be assumed
    //! still bound in the command buffer that starts here.
    _recorded.reset();
    _sceneCmd = VK_NULL_HANDLE;

    /*
     * Past this frame slot's fence, so anything the previous use of it left
     * behind is finished with. Both halves of the batches' frame state belong
     * here: the running offsets start over, and the buffers a mid-frame grow
     * orphaned are only safe to free now.
     */
    _batchVertexUsed[_currentFrame] = 0;
    _batchIndexUsed[_currentFrame] = 0;
    //! This slot's camera ring starts over; the first setTransform() or beginRenderPass() fills slot 0.
    _transformSlotsUsed = 0;

    for (AllocatedBuffer &retired : _batchRetiredBuffers[_currentFrame])
        _memoryManager->destroyBuffer(retired);

    _batchRetiredBuffers[_currentFrame].clear();
}

void VulkanRenderer::beginRenderPass()
{
    AURA_FRAME_SCOPE(FramePhase::BeginPass);

    _renderPassActive = false;
    if (!_frameBegun || !_pipelineReady)
        return;

    //! endRenderPass() appends the scene buffer after ending it, where it must not throw.
    _chunkCmds.reserve(1);
    VkCommandBuffer cmd = _cmdBuffers[_currentFrame];
    VkClearValue clearColor = {{{_clearR, _clearG, _clearB, _clearA}}};

    VkFramebuffer framebuffer = _vkFrameBuffersManager->getFrameBuffers()[_currentImageIndex];

    _vkRenderPassManager->beginRenderPass(cmd, framebuffer, *_vkSwapChainManager->getExtent2D(), &clearColor,
                                          /*useSecondaryCommandBuffers=*/true);

    _renderPassActive = true;

    _sceneCmd = _vkCommandManager->acquireSecondaryCommandBuffer(_currentFrame);
    VkCommandManager::beginSecondaryCommandBuffer(_sceneCmd, *_vkRenderPassManager->getRenderPass(), framebuffer);

    // Secondary buffers inherit no pipeline bindings.
    _recorded.reset();

    if (_transformSlotsUsed == 0)
        publishTransform();
}

void VulkanRenderer::endRenderPass()
{
    AURA_FRAME_SCOPE(FramePhase::EndPass);

    if (!_frameBegun || !_renderPassActive)
        return;

    VkCommandBuffer cmd = _cmdBuffers[_currentFrame];

    if (_sceneCmd != VK_NULL_HANDLE)
    {
        VkCommandManager::endCommandBuffer(_sceneCmd);
        _chunkCmds.push_back(_sceneCmd);
    }

    if (!_chunkCmds.empty())
        vkCmdExecuteCommands(cmd, static_cast<u32>(_chunkCmds.size()), _chunkCmds.data());

    VkRenderPassManager::endRenderPass(cmd);
    _renderPassActive = false;

    _sceneCmd = VK_NULL_HANDLE;
    _chunkCmds.clear();
}

void VulkanRenderer::endFrame()
{
    if (!_frameBegun)
        return;

    _vkTextureManager->flushUploads();

    VkCommandBuffer cmd = _cmdBuffers[_currentFrame];

#ifdef AURA_PROFILE_FRAME
    //! The last command before the buffer closes: paired with the one in
    //! beginFrame(), the difference is the frame's GPU wall time. A no-op when
    //! this slot wrote no opening timestamp.
    _gpuTimer.writeEnd(cmd, _currentFrame);
#endif

    VkCommandManager::endCommandBuffer(cmd);

    VkQueue graphicsQueue = _queueDataFromExclusiveFlags.front()->queues.front();
    {
        AURA_FRAME_SCOPE(FramePhase::Submit);
        VkQueueManager::submitCmdIntoQueue(graphicsQueue, &cmd,
                                           &_vkRenderSyncManager->getImageAvailableSemaphores()[_currentFrame],
                                           //! Per image, not per frame slot: this one is consumed by the
                                           //! present below, whose completion the frame fence does not cover.
                                           &_vkRenderSyncManager->getRenderFinishedSemaphores()[_currentImageIndex],
                                           _vkRenderSyncManager->getInFlightFences()[_currentFrame]);
    }

    {
        AURA_FRAME_SCOPE(FramePhase::Present);
        _vkSwapChainManager->presentBackToSwapChain(
            graphicsQueue, &_vkRenderSyncManager->getRenderFinishedSemaphores()[_currentImageIndex], _currentImageIndex,
            _windowManagerApi->getWindowFlags());
    }

    AURA_FRAME_END();

    //! A present that fails raises resized or surfaceLost, which needsFrame() reports.
    _targetStale = false;
    _frameBegun = false;
    _renderPassActive = false;
    advanceFrame();
}

bool VulkanRenderer::needsFrame() const noexcept
{
    const wma::WindowFlags *flags = _windowManagerApi->getWindowFlags();
    return _targetStale || (flags && (flags->resized || flags->surfaceLost));
}

void VulkanRenderer::setTransform(const gfx::TransformUBO &ubo)
{
    const bool cameraChanged = ubo.view != _currentTransform.view || ubo.proj != _currentTransform.proj;
    _currentTransform = ubo;
    //! The model is a push constant, so only a new camera takes a slot; earlier meshes keep theirs.
    if (_frameBegun && (cameraChanged || _transformSlotsUsed == 0))
        publishTransform();
}

void VulkanRenderer::publishTransform()
{
    //! Recorded draws read their slot at GPU time, so a full ring is never reused.
    if (_transformSlotsUsed < kTransformSlots)
        _vkUniformBufferManager->updateUniformBuffer(_currentFrame, _currentTransform, _transformSlotsUsed++);
}

void VulkanRenderer::bindVertexBuffer(VertexBufferHandle handle)
{
    _currentVertexBuffer = handle;
}
void VulkanRenderer::bindIndexBuffer(IndexBufferHandle handle)
{
    _currentIndexBuffer = handle;
}
void VulkanRenderer::bindTexture(TextureHandle handle)
{
    _currentTexture = handle;
}

void VulkanRenderer::bindDrawState(VkCommandBuffer cmd)
{
    /*
     * Immediate-mode draws (bindVertexBuffer/bindTexture/setTransform then
     * drawIndexed) resolve the renderer's current selection into the same
     * ResolvedDraw the batched path builds, then share one bind implementation
     * with it. Keeping a single copy of that logic is what stops the two paths'
     * redundancy filters from disagreeing with what was actually recorded.
     */
    ResolvedDraw draw;
    draw.model = _currentTransform.model;
    draw.textureIndex = textureArrayIndexOf(_currentTexture);

    if (const VertexBufferInfo *vb = vertexBufferOf(_currentVertexBuffer))
        draw.vertexBuffer = vb->buffer;

    if (const IndexBufferInfo *ib = indexBufferOf(_currentIndexBuffer))
    {
        draw.indexBuffer = ib->buffer;
        draw.indexType = ib->indexType;
    }

    aura3d::vk::bindDrawState(cmd, _recorded, sceneBindings(), draw);
}

SceneBindings VulkanRenderer::sceneBindings() const
{
    return {_vkGraphicsPipelineManager.get(),
            {_descSets[_currentFrame], _bindlessTextureSet, _lightDescSets[_currentFrame]},
            *_vkSwapChainManager->getExtent2D(),
            static_cast<u32>(_vkUniformBufferManager->getSlotStride() *
                             (_transformSlotsUsed > 0 ? _transformSlotsUsed - 1 : 0))};
}

void VulkanRenderer::drawIndexed(u32 indexCount, u32 instanceCount)
{
    if (!_frameBegun || !_renderPassActive || !_pipelineReady || _sceneCmd == VK_NULL_HANDLE)
        return;

    //! The subpass was begun in SECONDARY_COMMAND_BUFFERS mode, so this must
    //! record into the scene secondary buffer, never into _cmdBuffers.
    VkCommandBuffer cmd = _sceneCmd;
    bindDrawState(cmd);

    //! Viewport/scissor were recorded once by bindDrawState(), so this is the
    //! bare vkCmdDrawIndexed rather than the extent-taking convenience overload.
    vkCmdDrawIndexed(cmd, indexCount, instanceCount, 0, 0, 0);
}

void VulkanRenderer::draw(u32 vertexCount, u32 instanceCount)
{
    if (!_frameBegun || !_renderPassActive || !_pipelineReady || _sceneCmd == VK_NULL_HANDLE)
        return;

    //! See drawIndexed(): the pass records through secondary buffers.
    VkCommandBuffer cmd = _sceneCmd;
    bindDrawState(cmd);

    vkCmdDraw(cmd, vertexCount, instanceCount, 0, 0);
}

void VulkanRenderer::drawMeshes(std::span<const DrawItem> items)
{
    AURA_FRAME_SCOPE(FramePhase::RecordScene);

    if (!_frameBegun || !_renderPassActive || !_pipelineReady || items.empty() || _sceneCmd == VK_NULL_HANDLE)
        return;

    /*
     * Resolve every handle up front, on this thread. Two reasons, both
     * load-bearing: the workers then never read _meshes/_materials/_vbByHandle
     * (containers this thread may grow between frames), and the inner
     * recording loop becomes a linear walk over a compact POD array instead of
     * a chain of handle lookups. Resolution is a few array loads per item --
     * cheap next to the vkCmd* calls it feeds, which is the work worth
     * spreading across threads.
     */
    _resolvedDraws.clear();
    _resolvedDraws.reserve(items.size());

    //! Each draw carries its model; _currentTransform stays the caller's for later World batches.
    for (const DrawItem &item : items)
    {
        if (isValidHandle(item.material))
            bindMaterial(item.material);

        const MeshRecord *mesh = getMesh(item.mesh);
        if (!mesh)
            continue;

        _currentVertexBuffer = mesh->vertexBuffer;
        _currentIndexBuffer = mesh->indexBuffer;
        const VertexBufferInfo *vb = vertexBufferOf(mesh->vertexBuffer);
        const IndexBufferInfo *ib = indexBufferOf(mesh->indexBuffer);
        if (!vb || !ib)
            continue;

        ResolvedDraw &draw = _resolvedDraws.emplace_back();
        draw.model = item.model;
        draw.vertexBuffer = vb->buffer;
        draw.indexBuffer = ib->buffer;
        draw.indexType = ib->indexType;
        draw.indexCount = mesh->indexCount;
        draw.textureIndex = textureArrayIndexOf(_currentTexture);
    }

    if (_resolvedDraws.empty())
        return;

    const SceneBindings bindings = sceneBindings();
    VkRenderPass renderPass = *_vkRenderPassManager->getRenderPass();
    VkFramebuffer framebuffer = _vkFrameBuffersManager->getFrameBuffers()[_currentImageIndex];

    // Small batches stay inline to avoid worker wakeups and secondary-buffer setup.
    constexpr size_t kMinDrawsToThread = 512;
    constexpr size_t kMinDrawsPerChunk = 128;

    const size_t maxChunks = _resolvedDraws.size() / kMinDrawsPerChunk;
    const size_t chunkCount = (_resolvedDraws.size() < kMinDrawsToThread)
                                  ? 1
                                  : std::min<size_t>(_recordWorkerCount, std::max<size_t>(maxChunks, 1));

    if (chunkCount > 1 && !_recordPool)
    {
        try
        {
            _recordPool = std::make_unique<ink::ParallelProcessor>(static_cast<size_t>(_recordWorkerCount));
        }
        catch (const std::system_error &e)
        {
            INK_WARN << "Recording workers unavailable, recording inline: " << e.what();
            _recordWorkerCount = 1;
        }
    }

    if (chunkCount <= 1 || !_recordPool)
    {
        for (const ResolvedDraw &draw : _resolvedDraws)
        {
            aura3d::vk::bindDrawState(_sceneCmd, _recorded, bindings, draw);
            vkCmdDrawIndexed(_sceneCmd, draw.indexCount, 1, 0, 0, 0);
        }
        return;
    }

    /*
     * Contiguous chunks, remainder spread over the first few rather than
     * dumped on the last, so no worker gets a visibly longer slice. Chunk
     * order is preserved in _chunkCmds and replayed in that order by
     * endRenderPass(), which is what makes the parallel result identical to
     * the serial one.
     */
    const size_t perChunk = _resolvedDraws.size() / chunkCount;
    const size_t remainder = _resolvedDraws.size() % chunkCount;

    _chunkCmds.reserve(_chunkCmds.size() + chunkCount + 2);

    // Seal the preceding serial segment before appending worker chunks.
    VkCommandManager::endCommandBuffer(_sceneCmd);
    _chunkCmds.push_back(_sceneCmd);
    _sceneCmd = VK_NULL_HANDLE;

    const size_t firstChunkCmd = _chunkCmds.size();
    _chunkCmds.resize(firstChunkCmd + chunkCount);

    try
    {
        const auto record = [&](size_t chunk)
        {
            const size_t offset = chunk * perChunk + std::min(chunk, remainder);
            const size_t count = perChunk + (chunk < remainder ? 1 : 0);
            const std::span<const ResolvedDraw> slice(_resolvedDraws.data() + offset, count);
            // Each participant, including the caller, acquires from its own command pool.
            VkCommandBuffer cmd = _vkCommandManager->acquireSecondaryCommandBuffer(_currentFrame);
            _chunkCmds[firstChunkCmd + chunk] = cmd;
            recordChunk(cmd, renderPass, framebuffer, bindings, slice);
        };
        // run() joins before returning or throwing; std::ref avoids allocating the callback.
        _recordPool->run(chunkCount, std::ref(record));
    }
    catch (...)
    {
        /*
         * A chunk that threw mid-recording was never ended, and replaying it
         * would be invalid, so the whole batch is dropped; its buffers go back
         * with the pool reset. The scene segment is reopened regardless: the
         * rest of the frame must still have somewhere to record.
         */
        _chunkCmds.resize(firstChunkCmd);
        _sceneCmd = _vkCommandManager->acquireSecondaryCommandBuffer(_currentFrame);
        VkCommandManager::beginSecondaryCommandBuffer(_sceneCmd, renderPass, framebuffer);
        _recorded.reset();
        throw;
    }

    _sceneCmd = _vkCommandManager->acquireSecondaryCommandBuffer(_currentFrame);
    VkCommandManager::beginSecondaryCommandBuffer(_sceneCmd, renderPass, framebuffer);
    _recorded.reset();
}

std::optional<std::pair<u32, u32>> VulkanRenderer::uploadBatch(std::span<const gfx::BatchVertex> vertices,
                                                               std::span<const u32> indices)
{
    const u32 frame = _currentFrame;
    // Write-combined mapped memory avoids a staging copy for transient geometry. System RAM rather
    // than device-local BAR memory: the GPU reads each byte once during the draw, while a BAR copy
    // crosses PCIe as the CPU writes (measured 2.8x slower on a discrete GPU).
    constexpr VmaAllocationCreateFlags kDynamicFlags =
        VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;

    // Every recorded batch keeps its own slice until the frame fence completes.
    const VkDeviceSize vertexOffset = _batchVertexUsed[frame];
    const VkDeviceSize indexOffset = _batchIndexUsed[frame];
    const VkDeviceSize vertexEnd = vertexOffset + vertices.size_bytes();
    const VkDeviceSize indexEnd = indexOffset + indices.size_bytes();

    // Old draws retain the old buffer; only new batches use the replacement.
    const auto grow = [&](AllocatedBuffer &buffer, VkDeviceSize &capacity, VkDeviceSize needed, VkDeviceSize minimum,
                          VkBufferUsageFlags usage)
    {
        if (needed <= capacity)
            return;
        auto &retired = _batchRetiredBuffers[frame];
        retired.reserve(retired.size() + 1);
        const VkDeviceSize newCapacity = std::max({needed, minimum, capacity * 2});
        const AllocatedBuffer replacement = _memoryManager->createBuffer(
            newCapacity, usage, VK_SHARING_MODE_EXCLUSIVE, VMA_MEMORY_USAGE_AUTO_PREFER_HOST, kDynamicFlags);
        if (buffer.buffer != VK_NULL_HANDLE)
            retired.push_back(buffer);
        buffer = replacement;
        capacity = newCapacity;
    };
    grow(_batchVertexBuffers[frame], _batchVertexCapacity[frame], vertexEnd, 65536, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
    grow(_batchIndexBuffers[frame], _batchIndexCapacity[frame], indexEnd, 16384, VK_BUFFER_USAGE_INDEX_BUFFER_BIT);

    AllocatedBuffer &vertexBuffer = _batchVertexBuffers[frame];
    AllocatedBuffer &indexBuffer = _batchIndexBuffers[frame];
    if (!vertexBuffer.mappedData || !indexBuffer.mappedData)
        return std::nullopt;

    std::memcpy(static_cast<u8 *>(vertexBuffer.mappedData) + vertexOffset, vertices.data(), vertices.size_bytes());
    std::memcpy(static_cast<u8 *>(indexBuffer.mappedData) + indexOffset, indices.data(), indices.size_bytes());

    VK_RESULT_CHECK(vmaFlushAllocation(_memoryManager->getAllocator(), vertexBuffer.allocation, vertexOffset,
                                       vertices.size_bytes()));
    VK_RESULT_CHECK(
        vmaFlushAllocation(_memoryManager->getAllocator(), indexBuffer.allocation, indexOffset, indices.size_bytes()));

    _batchVertexUsed[frame] = vertexEnd;
    _batchIndexUsed[frame] = indexEnd;
    //! Both offsets advance in whole elements, so they divide exactly.
    return std::pair{static_cast<u32>(vertexOffset / sizeof(gfx::BatchVertex)),
                     static_cast<u32>(indexOffset / sizeof(u32))};
}

void VulkanRenderer::destroyBatchBuffers()
{
    if (!_memoryManager || !_memoryManager->isInitialized())
        return;

    //! Bounded by the vector's own size rather than GetMaxFramesInFlight():
    //! this can run before createDescriptorSets() ever has (a teardown after
    //! a failed partial init), when these are still empty, and .size() is
    //! then correctly 0 rather than indexing off the end.
    for (u32 frame = 0; frame < _batchVertexBuffers.size(); ++frame)
    {
        if (_batchVertexBuffers[frame].buffer != VK_NULL_HANDLE)
            _memoryManager->destroyBuffer(_batchVertexBuffers[frame]);
        if (_batchIndexBuffers[frame].buffer != VK_NULL_HANDLE)
            _memoryManager->destroyBuffer(_batchIndexBuffers[frame]);

        //! Anything a mid-frame grow orphaned is still owed a free: teardown is
        //! past every fence, so this is the last and safest chance to take it.
        for (AllocatedBuffer &retired : _batchRetiredBuffers[frame])
            _memoryManager->destroyBuffer(retired);

        _batchRetiredBuffers[frame].clear();

        _batchVertexBuffers[frame] = {};
        _batchIndexBuffers[frame] = {};
        _batchVertexCapacity[frame] = 0;
        _batchIndexCapacity[frame] = 0;
        _batchVertexUsed[frame] = 0;
        _batchIndexUsed[frame] = 0;
    }
}

glm::uvec2 VulkanRenderer::renderTargetSize() const noexcept
{
    if (!_vkSwapChainManager || !_vkSwapChainManager->getExtent2D())
        return glm::uvec2{0};
    const VkExtent2D extent = *_vkSwapChainManager->getExtent2D();
    return {extent.width, extent.height};
}

void VulkanRenderer::drawBatch(std::span<const gfx::BatchVertex> vertices, std::span<const u32> indices,
                               TextureHandle texture, gfx::BatchSpace space)
{
    AURA_FRAME_SCOPE(space == gfx::BatchSpace::Screen ? FramePhase::RecordOverlay : FramePhase::RecordScene);

    if (!_frameBegun || !_renderPassActive || !_pipelineReady || _sceneCmd == VK_NULL_HANDLE ||
        _bindlessTextureSet == VK_NULL_HANDLE || vertices.empty() || indices.empty())
        return;

    const auto first = uploadBatch(vertices, indices);
    if (!first)
        return;

    VkCommandBuffer cmd = _sceneCmd;
    const VkPipeline pipeline = _vkBatchPipelineManager->getPipeline();
    if (_recorded.pipeline != pipeline)
    {
        _vkBatchPipelineManager->cmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS);
        _vkBatchPipelineManager->cmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, 0, 1, &_bindlessTextureSet,
                                                       0, nullptr);
        _recorded.pipeline = pipeline;
        _recorded.staticSetsBound = false;
    }
    if (!_recorded.viewportSet)
    {
        VkGraphicsPipelineManager::cmdSetViewportAndScissor(cmd, *_vkSwapChainManager->getExtent2D());
        _recorded.viewportSet = true;
    }

    const BatchPushConstants pushConstants{batchTransform(space), textureArrayIndexOf(texture)};
    _vkBatchPipelineManager->cmdPushConstants(cmd, &pushConstants);

    //! Bound once per run of batches: each draw addresses its own slice by offset.
    const VkBuffer vertexBuffer = _batchVertexBuffers[_currentFrame].buffer;
    const VkBuffer indexBuffer = _batchIndexBuffers[_currentFrame].buffer;
    if (_recorded.vertexBuffer != vertexBuffer)
    {
        constexpr VkDeviceSize kOrigin = 0;
        vkCmdBindVertexBuffers(cmd, 0, 1, &vertexBuffer, &kOrigin);
        _recorded.vertexBuffer = vertexBuffer;
    }
    if (_recorded.indexBuffer != indexBuffer || _recorded.indexType != VK_INDEX_TYPE_UINT32)
    {
        vkCmdBindIndexBuffer(cmd, indexBuffer, 0, VK_INDEX_TYPE_UINT32);
        _recorded.indexBuffer = indexBuffer;
        _recorded.indexType = VK_INDEX_TYPE_UINT32;
    }

    vkCmdDrawIndexed(cmd, static_cast<u32>(indices.size()), 1, first->second, static_cast<i32>(first->first), 0);
}

void VulkanRenderer::setClearColor(f32 r, f32 g, f32 b, f32 a)
{
    _clearR = r;
    _clearG = g;
    _clearB = b;
    _clearA = a;
}

wma::IWindowManager *VulkanRenderer::getWindowManager()
{
    return _windowManagerApi.get();
}
RendererChoice VulkanRenderer::getBackendType() const
{
    return RendererChoice::VULKAN;
}
VkVertexBufferManager *VulkanRenderer::getVertexBufferManager()
{
    return _vkVertexBufferManager.get();
}
VkIndexBufferManager *VulkanRenderer::getIndexBufferManager()
{
    return _vkIndexBufferManager.get();
}
VkUniformBufferManager *VulkanRenderer::getUniformBufferManager()
{
    return _vkUniformBufferManager.get();
}
VkDescriptorManager *VulkanRenderer::getDescriptorManager()
{
    return _vkDescriptorManager.get();
}
VkGraphicsPipelineManager *VulkanRenderer::getGraphicsPipelineManager()
{
    return _vkGraphicsPipelineManager.get();
}
VkSwapChainManager *VulkanRenderer::getSwapChainManager()
{
    return _vkSwapChainManager.get();
}
VkRenderPassManager *VulkanRenderer::getRenderPassManager()
{
    return _vkRenderPassManager.get();
}
VkFrameBuffersManager *VulkanRenderer::getFrameBuffersManager()
{
    return _vkFrameBuffersManager.get();
}
VkCommandManager *VulkanRenderer::getCommandManager()
{
    return _vkCommandManager.get();
}
VkRenderSyncManager *VulkanRenderer::getRenderSyncManager()
{
    return _vkRenderSyncManager.get();
}
VkTextureManager *VulkanRenderer::getTextureManager()
{
    return _vkTextureManager.get();
}
VkDeviceManager *VulkanRenderer::getDeviceManager()
{
    return _vkDeviceManager.get();
}
VulkanMemoryManager *VulkanRenderer::getMemoryManager()
{
    return _memoryManager.get();
}
const std::vector<aura3d::vk::QueueData *> &VulkanRenderer::getQueues() const
{
    return _queueDataFromExclusiveFlags;
}
VkFixedArray<VkCommandBuffer> &VulkanRenderer::getCommandBuffers()
{
    return _cmdBuffers;
}
#ifdef AURA_PROFILE_FRAME
GpuTimingStats VulkanRenderer::gpuTiming() const noexcept
{
    return _gpuTimingEnabled ? _gpuTimer.stats() : GpuTimingStats{};
}
#endif

u32 VulkanRenderer::getCurrentFrame() const
{
    return _currentFrame;
}
void VulkanRenderer::advanceFrame()
{
    //! Not GetMaxFramesInFlight(): a settings lookup has no place on the per-frame path.
    _currentFrame = (_currentFrame + 1) % static_cast<u32>(_cmdBuffers.size());
}

} // namespace vk
} // namespace aura3d
