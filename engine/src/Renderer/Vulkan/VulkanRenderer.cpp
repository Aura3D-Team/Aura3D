#include "aura/Renderer/Vulkan/VulkanRenderer.h"

#include <algorithm>
#include <bit>
#include <chrono>
#include <cstring>
#include <future>
#include <thread>

#include <glm/ext/matrix_clip_space.hpp>
#include <ink/ThreadPool.h>

#include "aura/aura.h"
#include "aura/Core/AuraException/AuraException.h"
#include "aura/Renderer/Vulkan/VkAura/EmbeddedSpirv.h"
#include "aura/Core/Profiling/FrameProfiler.h"

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

/**
 * @brief Declares the 3D scene pipeline interface: set 0 transform UBO, set 1
 *        bindless texture array, set 2 light UBO, and the model/normalMatrix
 *        push constant.
 *
 * The constructor's own _init() declares set 1 as a single, non-bindless
 * sampler; this replaces it. Used both by createResourceManagers() (the
 * pipeline built by default) and setupPipeline() (a hot-swapped custom
 * pipeline), so that either path's set 1 stays layout-compatible with the
 * persistent _bindlessTextureSet3D allocated once in createDescriptorSets() --
 * binding a descriptor set to a structurally different layout is invalid.
 */
void declareScene3DInterface(VkGraphicsPipelineManager& pipeline, u32 bindlessTextureCapacity)
{
    pipeline.resetInterface();

    DescriptorBindingInfo uboBinding;
    uboBinding.binding = 0;
    uboBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    uboBinding.descriptorCount = 1;
    uboBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    pipeline.addDescriptorBinding(0, uboBinding);

    DescriptorBindingInfo bindlessSampler3D;
    bindlessSampler3D.binding = 0;
    bindlessSampler3D.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindlessSampler3D.descriptorCount = bindlessTextureCapacity;
    bindlessSampler3D.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    bindlessSampler3D.bindingFlags = VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT
                                    | VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT;
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

void VulkanRenderer::initialize(AuraSettings* settings, const JobSystem* jobs)
{
    if (_isInitialized) return;

    //! Unused here: draw-call recording has its own dedicated pool
    //! (_recordPool below), sized and shaped for per-thread Vulkan command
    //! pools rather than JobSystem's generic band dispatch. See CPURenderer
    //! for the backend that does share the engine's pool.
    (void)jobs;

    const bool enableValidation = settings->getValidationLayers();

    _vmaConfig = VulkanMemoryManager::loadConfig(settings);
    _memoryManager = std::make_unique<VulkanMemoryManager>();

    createWindow(settings->getWindowTitle().c_str(), settings->getWindowBackend());
    setupInput();
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
     * bindTexture() given an invalid handle and drawBatch2D()'s untextured-batch
     * case both resolve to this same slot (see textureArrayIndexOf()), so a draw
     * that never bound a texture samples a slot that is always written,
     * instead of one descriptorBindingPartiallyBound only permits leaving
     * unwritten -- not dynamically sampling.
     */
    _fallbackTexture = createSolidColorTexture(255, 255, 255, 255);

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

    // A window handle isn't necessarily safe to build a surface from the moment
    // it exists, on Android the OS can have released the underlying native
    // window already, and the platform driver crashes on it rather than
    // failing cleanly. wma owns that platform knowledge; see
    // IWindowManager::waitUntilWindowReady.
    if (!_windowManagerApi->waitUntilWindowReady())
        throw std::runtime_error("VulkanRenderer: window never became ready for surface creation");

    _vkSurfaceManager = std::make_unique<VkSurfaceManager>(
        _vkInstance->getVkInstance(),
        _windowManagerApi->getBackendType(),
        _windowManagerApi->getWindowInstance(),
        _windowManagerApi->getNativeDisplayHandle());

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

#ifdef AURA_ENABLE_DEBUG_MODE
    /*
     * After the queue family is known, because timestamp support is per family
     * (VkQueueFamilyProperties::timestampValidBits) rather than per device --
     * a transfer-only family on some hardware writes no timestamps at all.
     *
     * A device that cannot timestamp is not an error: initialize() reports it
     * and GPU timing is simply marked unavailable in the report.
     */
    (void)_debugMetrics.timestamps().initialize(
        *_vkDeviceManager->getDevice(),
        *_vkDeviceManager->getPhysicalDevice(),
        _graphicsIndexFamily,
        GetMaxFramesInFlight());

    //! The raw device-memory counters come from VMA's callbacks regardless;
    //! this is what adds the suballocation and heap-budget detail.
    _debugMetrics.setAllocator(_memoryManager->getAllocator());
#endif
}

void VulkanRenderer::createResourceManagers()
{
    VkDevice* dev = _vkDeviceManager->getDevice();

    //! Resolved against this device's update-after-bind limits, not the
    //! kDesiredBindlessTextures constant -- see maxBindlessTextures(). Cached
    //! here so the pipeline layouts, the pool and the per-draw bounds check in
    //! textureArrayIndexOf() can never disagree about the table's size.
    _bindlessTextureCapacity = _vkDeviceManager->maxBindlessTextures();

    _vkSwapChainManager = std::make_unique<VkSwapChainManager>(
        *_vkDeviceManager->getPhysicalDevice(), dev, *_vkSurfaceManager->getSurface());
    _vkImageViewsManager = std::make_unique<VkImageViewsManager>(dev);
    _vkRenderPassManager = std::make_unique<VkRenderPassManager>(dev);
    _vkFrameBuffersManager = std::make_unique<VkFrameBuffersManager>(dev);
    _vkDescriptorManager = std::make_unique<VkDescriptorManager>(dev, _bindlessTextureCapacity);

    _vkGraphicsPipelineManager = std::make_unique<VkGraphicsPipelineManager>(
        vk_vert_3d, vk_vert_3d_len, vk_frag_3d, vk_frag_3d_len, dev);
    declareScene3DInterface(*_vkGraphicsPipelineManager, _bindlessTextureCapacity);

    _vkOverlay2DPipelineManager = std::make_unique<VkGraphicsPipelineManager>(
        vk_vert_2d, vk_vert_2d_len, vk_frag_2d, vk_frag_2d_len, dev);

    /*
     * Replace the 3D interface the constructor installed. The overlay shaders
     * reference exactly one descriptor -- a bindless texture array in set 0,
     * same reasoning as the 3D pipeline above -- and take their projection
     * (plus a texture-array index) from a push constant, so declaring the
     * scene's UBO and light sets here would build a layout whose bindings
     * nothing ever fills.
     */
    _vkOverlay2DPipelineManager->resetInterface();

    DescriptorBindingInfo overlaySampler;
    overlaySampler.binding = 0;
    overlaySampler.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    overlaySampler.descriptorCount = _bindlessTextureCapacity;
    overlaySampler.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    overlaySampler.bindingFlags = VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT
                                 | VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT;
    _vkOverlay2DPipelineManager->addDescriptorBinding(0, overlaySampler);

    _vkOverlay2DPipelineManager->setPushConstantRange(
        VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(Overlay2DPushConstants));

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

    /*
     * Recording workers. graphics.cpu_threads == 0 means "match the machine",
     * the same convention the software rasteriser uses for the same setting.
     * One context per worker, allocated once here: a context owns a bind cache
     * tied to the buffer it is recording, so they are never shared or resized
     * mid-frame.
     */
    const int configuredThreads = AuraSettings::get()->getCpuThreads();
    const unsigned detected = std::thread::hardware_concurrency();
    _recordWorkerCount = configuredThreads > 0
                             ? static_cast<u32>(configuredThreads)
                             : (detected > 0 ? detected : 1u);

    _recordingContexts.clear();
    _recordingContexts.reserve(_recordWorkerCount);
    for (u32 i = 0; i < _recordWorkerCount; ++i)
        _recordingContexts.push_back(std::make_unique<VkCommandRecordingContext>());

    //! Only worth a pool at all beyond one worker; drawMeshes() records inline
    //! when there is none.
    if (_recordWorkerCount > 1)
        _recordPool = std::make_unique<ink::ThreadPool>(static_cast<size_t>(_recordWorkerCount));

    INK_VERBOSE << "Command recording workers: " << _recordWorkerCount;
}

void VulkanRenderer::setupPipeline(const std::string& vertShaderPath, const std::string& fragShaderPath)
{
    _vkGraphicsPipelineManager = std::make_unique<VkGraphicsPipelineManager>(
        vertShaderPath, fragShaderPath, _vkDeviceManager->getDevice());
    //! Must match createResourceManagers()'s pipeline exactly: set 1 has to
    //! stay layout-compatible with the already-allocated, persistent
    //! _bindlessTextureSet3D, which this custom pipeline does not reallocate.
    declareScene3DInterface(*_vkGraphicsPipelineManager, _bindlessTextureCapacity);
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

    _vkUniformBufferManager->createUniformBuffers(sharingMode, framesInFlight);

    for (u32 i = 0; i < framesInFlight; ++i)
    {
        _vkUniformBufferManager->updateUniformBuffer(i, const_cast<gfx::TransformUBO&>(_currentTransform));
    }

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
    for (u32 i = 0; i < GetMaxFramesInFlight(); ++i)
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

    //! Allocated exactly once: destroySwapchainResources() no longer frees
    //! this (only _descSets/_lightDescSets are resize-sensitive), so every
    //! later call here from handleWindowChanges()/recreateSurfaceAndSwapchain()
    //! finds it already non-null and leaves it -- and every texture already
    //! written into it -- untouched.
    if (_bindlessTextureSet3D == VK_NULL_HANDLE) {
        _bindlessTextureSet3D = _vkDescriptorManager->allocateDescriptorSet(
            _vkGraphicsPipelineManager->getDescriptorSetLayout(1));
    }

    const u32 framesInFlight = GetMaxFramesInFlight();

    //! Per frame in flight, matching the buffers they describe.
    _descSets.resize(framesInFlight);
    _lightDescSets.resize(framesInFlight);

    /*
     * The overlay's per-frame buffers/capacities, sized here for the same
     * reason: this is the one place every frame-in-flight-indexed array in
     * this class gets its size from. A repeat call (handleWindowChanges(),
     * recreateSurfaceAndSwapchain()) resizes to the same count it already
     * has, which leaves every existing buffer handle and capacity untouched --
     * exactly as idempotent as _descSets.resize() above.
     */
    _overlay2DVertexBuffers.resize(framesInFlight);
    _overlay2DIndexBuffers.resize(framesInFlight);
    _overlay2DVertexCapacity.resize(framesInFlight);
    _overlay2DIndexCapacity.resize(framesInFlight);
    _overlay2DVertexUsed.resize(framesInFlight);
    _overlay2DIndexUsed.resize(framesInFlight);
    _overlay2DRetiredBuffers.resize(framesInFlight);

    for (u32 i = 0; i < framesInFlight; ++i) {
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

    //! Allocated exactly once -- see the matching comment in createDescriptorSets().
    if (_bindlessTextureSet2D == VK_NULL_HANDLE) {
        _bindlessTextureSet2D = _vkDescriptorManager->allocateDescriptorSet(
            _vkOverlay2DPipelineManager->getDescriptorSetLayout(0));
    }

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
        INK_WARN << "Bindless texture table full (" << _bindlessTextureCapacity
                 << " slots); texture " << textureHandle
                 << " will render with the fallback texture.";
        return;
    }

    updateTextureDescriptorSets(textureHandle);
    updateOverlay2DTextureDescriptorSets(textureHandle);
}

void VulkanRenderer::updateOverlay2DTextureDescriptorSets(TextureHandle textureHandle)
{
    if (!_vkOverlay2DPipelineManager || _bindlessTextureSet2D == VK_NULL_HANDLE)
        return;

    const auto* texture = _vkTextureManager->getTexture(textureHandle.value());
    if (!texture)
        return;

    _vkDescriptorManager->updateTextureArrayElement(
        _bindlessTextureSet2D, 0, textureArrayIndexOf(textureHandle), texture->view, texture->sampler);
}

void VulkanRenderer::updateTextureDescriptorSets(TextureHandle textureHandle)
{
    if (!_pipelineReady || !_vkGraphicsPipelineManager || _bindlessTextureSet3D == VK_NULL_HANDLE) return;

    const auto* texture = _vkTextureManager->getTexture(textureHandle.value());
    if (!texture) return;

    _vkDescriptorManager->updateTextureArrayElement(
        _bindlessTextureSet3D, 0, textureArrayIndexOf(textureHandle), texture->view, texture->sampler);
}

void VulkanRenderer::destroySwapchainResources()
{
    _pipelineReady = false;

    /*
     * Only the per-image transform/light sets are actually invalidated by a
     * resize (the swapchain image count can change) -- explicitly freeing
     * just those back to the pool, rather than destroying and recreating the
     * whole VkDescriptorManager as before, is what lets the persistent
     * bindless texture-array sets (_bindlessTextureSet3D/2D) -- and every
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
    _vkRenderSyncManager->create(_imagesCount);
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
        _vkInstance->getVkInstance(),
        _windowManagerApi->getBackendType(),
        _windowManagerApi->getWindowInstance(),
        _windowManagerApi->getNativeDisplayHandle());

    _vkSwapChainManager->initSwapChainSupportDetails(
        *_vkDeviceManager->getPhysicalDevice(),
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
    _recordingContexts.clear();
    _chunkCmds.clear();
    _replayList.clear();
    _resolvedDraws.clear();

    clearSharedResources();

    _descSets.clear();
    _lightDescSets.clear();
    _bindlessTextureSet3D = VK_NULL_HANDLE;
    _bindlessTextureSet2D = VK_NULL_HANDLE;
    _vbNames.clear();
    _ibNames.clear();
    _pipelineReady = false;
    _renderPassActive = false;
    _frameBegun = false;
    _fallbackTexture = {};

#ifdef AURA_ENABLE_DEBUG_MODE
    /*
     * The query pool is a device object, so it has to go before the device
     * does; the allocator handle has to be dropped before vmaDestroyAllocator
     * below, since a report built afterwards would call vmaCalculateStatistics
     * on a destroyed allocator. The cumulative counters survive both -- they
     * live in VkDeviceMemoryCounters, not here, which is what lets a report
     * written after teardown still show what the run allocated.
     */
    _debugMetrics.timestamps().destroy();
    _debugMetrics.setAllocator(VK_NULL_HANDLE);
#endif

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
    std::string name = "vb_" + std::to_string(handle.value());

    _vkVertexBufferManager->createVertexBuffer(
        name,
        _vkCommandManager->getThreadCommandPool(),
        _vkSwapChainManager->getSwapchainCreateInfoKHR()->imageSharingMode,
        _queueDataFromExclusiveFlags.front()->queues.front(),
        std::move(vertices),
        false);

    _vbNames[handle] = name;
    //! Resolve once here so the draw path never has to (see _vbByHandle).
    _vbByHandle.resize(handle.value());
    _vbByHandle[handle.value() - 1] = _vkVertexBufferManager->getVertexBuffer(name);
    return handle;
}

IndexBufferHandle VulkanRenderer::createIndexBuffer(std::vector<u16>&& indices)
{
    auto handle = _nextIbHandle++;
    std::string name = "ib_" + std::to_string(handle.value());

    _vkIndexBufferManager->createIndexBuffer(
        name,
        _vkCommandManager->getThreadCommandPool(),
        _vkSwapChainManager->getSwapchainCreateInfoKHR()->imageSharingMode,
        _queueDataFromExclusiveFlags.front()->queues.front(),
        std::move(indices),
        false);

    _ibNames[handle] = name;
    //! Resolve once here so the draw path never has to (see _ibByHandle).
    _ibByHandle.resize(handle.value());
    _ibByHandle[handle.value() - 1] = _vkIndexBufferManager->getIndexBuffer(name);
    return handle;
}

IndexBufferHandle VulkanRenderer::createIndexBuffer(std::vector<u32>&& indices)
{
    auto handle = _nextIbHandle++;
    std::string name = "ib_" + std::to_string(handle.value());

    _vkIndexBufferManager->createIndexBuffer(
        name,
        _vkCommandManager->getThreadCommandPool(),
        _vkSwapChainManager->getSwapchainCreateInfoKHR()->imageSharingMode,
        _queueDataFromExclusiveFlags.front()->queues.front(),
        std::move(indices),
        false
    );

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

TextureHandle VulkanRenderer::createTextureFromPixels(const u8* rgbaPixels, u32 width, u32 height)
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

void VulkanRenderer::updateTextureRegion(TextureHandle handle, u32 x, u32 y,
                                         u32 width, u32 height, const u8* rgbaPixels)
{
    if (!_vkTextureManager) return;

    _vkTextureManager->updateRegion(handle.value(), x, y, width, height, rgbaPixels);
}

void VulkanRenderer::beginFrame()
{
    _frameBegun = false;
    _renderPassActive = false;

    auto* windowFlags = _windowManagerApi->getWindowFlags();
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
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        if (!_windowManagerApi->isSurfaceAvailable()) {
            return;
        }

        if (!_windowManagerApi->waitUntilWindowReady()) {
            return;
        }

        try
        {
            recreateSurfaceAndSwapchain();
        }
        catch (const AuraException& e)
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
        // Debounce delay
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        try
        {
            handleWindowChanges();
        }
        catch (const AuraException& e)
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

#ifdef AURA_ENABLE_DEBUG_MODE
    /*
     * Immediately after the fence wait and nowhere else. This slot's previous
     * submission has just been proven complete, so its two timestamps are
     * guaranteed readable and the read costs nothing; asking for them any
     * earlier would mean blocking the CPU on the GPU purely to measure it.
     * The reported GPU time therefore trails by the frames in flight, which
     * over a benchmark's thousands of frames is not a distinction that matters.
     */
    _debugMetrics.timestamps().resolve(_currentFrame);
#endif

    u32 imageIndex = 0;
    {
        AURA_FRAME_SCOPE(FramePhase::Acquire);
        imageIndex = _vkSwapChainManager->acquireNextImage(
            _vkRenderSyncManager->getImageAvailableSemaphores()[_currentFrame],
            windowFlags);
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

#ifdef AURA_ENABLE_DEBUG_MODE
    //! The first command in the frame's primary buffer, so the opening
    //! timestamp brackets everything the GPU does for this frame.
    _debugMetrics.timestamps().writeBegin(cmd, _currentFrame);
#endif

    //! A reset pool discards every recorded bind, so nothing may be assumed
    //! still bound in the command buffer that starts here.
    _recorded.reset();
    _sceneCmd = VK_NULL_HANDLE;
    _overlayCmd = VK_NULL_HANDLE;
    _overlayStateBound = false;

    /*
     * Past this frame slot's fence, so anything the previous use of it left
     * behind is finished with. Both halves of the overlay's frame state belong
     * here: the running offsets start over, and the buffers a mid-frame grow
     * orphaned are only safe to free now.
     */
    _overlay2DVertexUsed[_currentFrame] = 0;
    _overlay2DIndexUsed[_currentFrame] = 0;

    for (AllocatedBuffer& retired : _overlay2DRetiredBuffers[_currentFrame])
        _memoryManager->destroyBuffer(retired);

    _overlay2DRetiredBuffers[_currentFrame].clear();
}

void VulkanRenderer::beginRenderPass()
{
    AURA_FRAME_SCOPE(FramePhase::BeginPass);

    _renderPassActive = false;
    if (!_frameBegun || !_pipelineReady) return;

    VkCommandBuffer cmd = _cmdBuffers[_currentFrame];
    VkClearValue clearColor = { { {_clearR, _clearG, _clearB, _clearA} } };

    VkFramebuffer framebuffer = _vkFrameBuffersManager->getFrameBuffers()[_currentImageIndex];

    _vkRenderPassManager->beginRenderPass(
        cmd,
        framebuffer,
        *_vkSwapChainManager->getExtent2D(),
        &clearColor,
        /*useSecondaryCommandBuffers=*/true);

    _renderPassActive = true;

    /*
     * Every draw this pass records goes here rather than into the primary
     * buffer above, which the SECONDARY_COMMAND_BUFFERS contents mode
     * forbids. Begun eagerly (rather than on the first draw) so the draw path
     * stays a straight-line record with no per-draw "is the buffer open yet"
     * branch; an empty secondary buffer is legal and costs a begin/end pair.
     */
    _sceneCmd = _vkCommandManager->acquireSecondaryCommandBuffer(_currentFrame);
    VkCommandManager::beginSecondaryCommandBuffer(
        _sceneCmd, *_vkRenderPassManager->getRenderPass(), framebuffer);

    /*
     * No pipeline pre-bind here: bindDrawState() binds it lazily in front of
     * the pass's first actual draw (and every draw after resolves to a no-op
     * against the cached VkPipeline, see RecordedState). Binding it
     * unconditionally on every beginRenderPass() -- even a pass with zero
     * draws, e.g. one full frame of nothing but the 2D overlay -- was a
     * guaranteed-redundant vkCmdBindPipeline the state cache had no way to
     * know had already happened.
     *
     * A freshly begun secondary buffer inherits no bindings whatsoever, so the
     * cache has to start empty regardless of what the last one left bound.
     */
    _recorded.reset();

    _vkUniformBufferManager->updateUniformBuffer(
        _currentFrame, const_cast<gfx::TransformUBO&>(_currentTransform));
}

void VulkanRenderer::endRenderPass()
{
    AURA_FRAME_SCOPE(FramePhase::EndPass);

    if (!_frameBegun || !_renderPassActive) return;

    VkCommandBuffer cmd = _cmdBuffers[_currentFrame];

    /*
     * Replay order is array order, and it is the whole reason the overlay is
     * recorded separately: it must composite over the scene, so its buffer
     * goes last.
     */
    std::vector<VkCommandBuffer>& secondaries = _replayList;
    secondaries.clear();

    if (_sceneCmd != VK_NULL_HANDLE)
    {
        VkCommandManager::endCommandBuffer(_sceneCmd);
        secondaries.push_back(_sceneCmd);
    }

    //! drawMeshes()' worker chunks, already ended by their contexts, in the
    //! order the batch was split -- which is what makes the threaded result
    //! identical to recording the same list serially.
    for (VkCommandBuffer chunkCmd : _chunkCmds)
    {
        if (chunkCmd != VK_NULL_HANDLE)
            secondaries.push_back(chunkCmd);
    }

    if (_overlayCmd != VK_NULL_HANDLE)
    {
        VkCommandManager::endCommandBuffer(_overlayCmd);
        secondaries.push_back(_overlayCmd);
    }

    //! vkCmdExecuteCommands requires a non-zero count.
    if (!secondaries.empty())
        vkCmdExecuteCommands(cmd, static_cast<u32>(secondaries.size()), secondaries.data());

    VkRenderPassManager::endRenderPass(cmd);
    _renderPassActive = false;

    _sceneCmd = VK_NULL_HANDLE;
    _overlayCmd = VK_NULL_HANDLE;
    _overlayStateBound = false;
    _chunkCmds.clear();
}

void VulkanRenderer::endFrame()
{
    if (!_frameBegun) return;

    VkCommandBuffer cmd = _cmdBuffers[_currentFrame];

#ifdef AURA_ENABLE_DEBUG_MODE
    //! The last command before the buffer closes: paired with the one in
    //! beginFrame(), the difference is the frame's GPU wall time.
    _debugMetrics.timestamps().writeEnd(cmd, _currentFrame);
#endif

    VkCommandManager::endCommandBuffer(cmd);

    VkQueue graphicsQueue = _queueDataFromExclusiveFlags.front()->queues.front();
    {
        AURA_FRAME_SCOPE(FramePhase::Submit);
        VkQueueManager::submitCmdIntoQueue(
            graphicsQueue, &cmd,
            &_vkRenderSyncManager->getImageAvailableSemaphores()[_currentFrame],
            //! Per image, not per frame slot: this one is consumed by the
            //! present below, whose completion the frame fence does not cover.
            &_vkRenderSyncManager->getRenderFinishedSemaphores()[_currentImageIndex],
            _vkRenderSyncManager->getInFlightFences()[_currentFrame]);
    }

    {
        AURA_FRAME_SCOPE(FramePhase::Present);
        _vkSwapChainManager->presentBackToSwapChain(
            graphicsQueue,
            &_vkRenderSyncManager->getRenderFinishedSemaphores()[_currentImageIndex],
            _currentImageIndex,
            _windowManagerApi->getWindowFlags());
    }

    AURA_FRAME_END();

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

    if (const VertexBufferInfo* vb = vertexBufferOf(_currentVertexBuffer))
        draw.vertexBuffer = vb->buffer;

    if (const IndexBufferInfo* ib = indexBufferOf(_currentIndexBuffer))
    {
        draw.indexBuffer = ib->buffer;
        draw.indexType = ib->indexType;
    }

    aura3d::vk::bindDrawState(cmd, _recorded, sceneBindings(), draw);
}

SceneBindings VulkanRenderer::sceneBindings() const
{
    SceneBindings bindings;
    bindings.pipeline = _vkGraphicsPipelineManager.get();
    bindings.textureTable = _bindlessTextureSet3D;
    bindings.extent = *_vkSwapChainManager->getExtent2D();

    if (_currentFrame < _descSets.size())
        bindings.transformSet = _descSets[_currentFrame];

    if (_currentFrame < _lightDescSets.size())
        bindings.lightSet = _lightDescSets[_currentFrame];

    return bindings;
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

    if (!_frameBegun || !_renderPassActive || !_pipelineReady || items.empty())
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

    for (const DrawItem& item : items)
    {
        const MeshRecord* mesh = getMesh(item.mesh);
        if (!mesh)
            continue;

        const VertexBufferInfo* vb = vertexBufferOf(mesh->vertexBuffer);
        const IndexBufferInfo* ib = indexBufferOf(mesh->indexBuffer);
        if (!vb || !ib)
            continue;

        //! An item without its own material inherits whatever bindMaterial()/
        //! bindTexture() last selected, matching the base implementation.
        TextureHandle texture = _currentTexture;
        if (isValidHandle(item.material))
        {
            if (const Material* material = getMaterial(item.material))
                texture = material->albedo;
        }

        ResolvedDraw& draw = _resolvedDraws.emplace_back();
        draw.model = item.model;
        draw.vertexBuffer = vb->buffer;
        draw.indexBuffer = ib->buffer;
        draw.indexType = ib->indexType;
        draw.indexCount = mesh->indexCount;
        draw.textureIndex = textureArrayIndexOf(texture);
    }

    if (_resolvedDraws.empty())
        return;

    const SceneBindings bindings = sceneBindings();
    VkRenderPass renderPass = *_vkRenderPassManager->getRenderPass();
    VkFramebuffer framebuffer = _vkFrameBuffersManager->getFrameBuffers()[_currentImageIndex];

    /*
     * Thresholds picked from measurement, not intuition (release build,
     * validation off, RTX 4060 / 22 logical cores, Sandbox stress scene):
     *
     *      objects   serial   16 workers
     *        2 000    232us        135us
     *       20 000   2408us        926us   (2.6x)
     *
     * The fan-out has a fixed cost of roughly 100-200us -- waking the pool,
     * one secondary command buffer begin/end per chunk, and a full state
     * rebind per chunk since a secondary buffer inherits no bindings. Below a
     * few hundred draws that cost is the entire budget, and at 2 000 objects
     * with only 2-4 workers the threaded path measured *slower* than serial.
     * So: stay inline unless the batch is genuinely large, and when it is,
     * use every worker rather than a token few.
     */
    constexpr size_t kMinDrawsToThread = 512;
    constexpr size_t kMinDrawsPerChunk = 128;

    const size_t maxChunks = _resolvedDraws.size() / kMinDrawsPerChunk;
    const size_t chunkCount = (_resolvedDraws.size() < kMinDrawsToThread)
                                  ? 1
                                  : std::min<size_t>(_recordWorkerCount, std::max<size_t>(maxChunks, 1));

    if (chunkCount <= 1 || !_recordPool)
    {
        for (const ResolvedDraw& draw : _resolvedDraws)
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

    const size_t firstChunkCmd = _chunkCmds.size();
    _chunkCmds.resize(firstChunkCmd + chunkCount);

    /*
     * _recordFutures is a member cleared (never shrunk) between frames, so the
     * steady-state frame reuses one allocation instead of building a fresh
     * vector of chunkCount std::futures -- each of which carries a shared
     * state -- on every single frame.
     */
    _recordFutures.clear();
    _recordFutures.reserve(chunkCount);

    size_t offset = 0;
    for (size_t chunk = 0; chunk < chunkCount; ++chunk)
    {
        const size_t count = perChunk + (chunk < remainder ? 1 : 0);
        const std::span<const ResolvedDraw> slice(_resolvedDraws.data() + offset, count);
        offset += count;

        VkCommandRecordingContext* context = _recordingContexts[chunk].get();
        VkCommandBuffer* slot = &_chunkCmds[firstChunkCmd + chunk];

        _recordFutures.push_back(_recordPool->submit(
            [this, context, slot, slice, &bindings, renderPass, framebuffer] {
                /*
                 * Allocated on the worker thread on purpose: the buffer must
                 * come from a pool owned by the thread that records into it,
                 * and VkCommandManager keys its pools by thread id to
                 * guarantee exactly that.
                 */
                VkCommandBuffer cmd = _vkCommandManager->acquireSecondaryCommandBuffer(_currentFrame);
                *slot = cmd;
                context->recordChunk(cmd, renderPass, framebuffer, bindings, slice);
            }));
    }

    //! Blocks until the whole batch is recorded: the buffers have to be closed
    //! before endRenderPass() can replay them, and `bindings` is captured by
    //! reference so it must outlive every worker.
    for (std::future<void>& future : _recordFutures)
        future.get();

    /*
     * The scene buffer's cache describes only what *it* recorded. Nothing was
     * added to it here -- the chunks are separate buffers -- so it stays valid
     * and a later immediate-mode draw can still skip redundant binds.
     */
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

    /*
     * The outgoing buffer is retired rather than destroyed: a grow can happen
     * part-way through a frame, and the draws already recorded into _overlayCmd
     * have bound the old handle. Freeing it here would leave the replay at
     * endRenderPass() reading memory the allocator has taken back. beginFrame()
     * releases the retired list once the slot's fence has passed.
     *
     * The batches already written to the old buffer stay there and stay valid,
     * which is why nothing is copied across -- the running offset simply
     * continues into the new buffer, and the prefix it skips goes unread.
     */
    if (vertexBytes > _overlay2DVertexCapacity[frame])
    {
        if (_overlay2DVertexBuffers[frame].buffer != VK_NULL_HANDLE)
            _overlay2DRetiredBuffers[frame].push_back(_overlay2DVertexBuffers[frame]);

        _overlay2DVertexBuffers[frame] = _memoryManager->createBuffer(
            vertexBytes, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, sharingMode,
            VMA_MEMORY_USAGE_AUTO, kDynamicFlags);
        _overlay2DVertexCapacity[frame] = vertexBytes;
    }

    if (indexBytes > _overlay2DIndexCapacity[frame])
    {
        if (_overlay2DIndexBuffers[frame].buffer != VK_NULL_HANDLE)
            _overlay2DRetiredBuffers[frame].push_back(_overlay2DIndexBuffers[frame]);

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

    //! Bounded by the vector's own size rather than GetMaxFramesInFlight():
    //! this can run before createDescriptorSets() ever has (a teardown after
    //! a failed partial init), when these are still empty, and .size() is
    //! then correctly 0 rather than indexing off the end.
    for (u32 frame = 0; frame < _overlay2DVertexBuffers.size(); ++frame)
    {
        if (_overlay2DVertexBuffers[frame].buffer != VK_NULL_HANDLE)
            _memoryManager->destroyBuffer(_overlay2DVertexBuffers[frame]);
        if (_overlay2DIndexBuffers[frame].buffer != VK_NULL_HANDLE)
            _memoryManager->destroyBuffer(_overlay2DIndexBuffers[frame]);

        //! Anything a mid-frame grow orphaned is still owed a free: teardown is
        //! past every fence, so this is the last and safest chance to take it.
        for (AllocatedBuffer& retired : _overlay2DRetiredBuffers[frame])
            _memoryManager->destroyBuffer(retired);

        _overlay2DRetiredBuffers[frame].clear();

        _overlay2DVertexBuffers[frame] = {};
        _overlay2DIndexBuffers[frame] = {};
        _overlay2DVertexCapacity[frame] = 0;
        _overlay2DIndexCapacity[frame] = 0;
        _overlay2DVertexUsed[frame] = 0;
        _overlay2DIndexUsed[frame] = 0;
    }
}

void VulkanRenderer::drawBatch2D(std::span<const gfx::Vertex2D> vertices,
                                 std::span<const u32> indices,
                                 TextureHandle texture)
{
    AURA_FRAME_SCOPE(FramePhase::RecordOverlay);

    if (!_frameBegun || !_renderPassActive || !_pipelineReady || !_vkOverlay2DPipelineManager)
        return;

    if (vertices.empty() || indices.empty())
        return;

    //! An untextured batch still samples, so stand in an opaque white texel --
    //! the same reserved fallback slot 0 bindTexture() given an invalid handle
    //! uses (see textureArrayIndexOf()), guaranteed populated since initialize().
    const TextureHandle sampled = isValidHandle(texture) ? texture : _fallbackTexture;

    if (_bindlessTextureSet2D == VK_NULL_HANDLE)
        return; //! Overlay pipeline never finished setting up.

    const VkDeviceSize vertexBytes = static_cast<VkDeviceSize>(vertices.size_bytes());
    const VkDeviceSize indexBytes = static_cast<VkDeviceSize>(indices.size_bytes());

    /*
     * Appended, not overwritten. Every batch in the frame records into
     * _overlayCmd and none of them executes until endRenderPass() replays it,
     * so a batch that wrote at offset 0 would be reading whatever the last
     * batch of the frame left there by the time the GPU got to it -- the text
     * overlay drawing a slice of the UI's geometry, and so on.
     *
     * No alignment maths: vertexBytes is a whole number of Vertex2D and
     * indexBytes a whole number of u32, so the running totals stay aligned for
     * both binds by construction.
     */
    const VkDeviceSize vertexOffset = _overlay2DVertexUsed[_currentFrame];
    const VkDeviceSize indexOffset = _overlay2DIndexUsed[_currentFrame];

    ensureOverlay2DCapacity(_currentFrame, vertexOffset + vertexBytes, indexOffset + indexBytes);

    AllocatedBuffer& vertexBuffer = _overlay2DVertexBuffers[_currentFrame];
    AllocatedBuffer& indexBuffer = _overlay2DIndexBuffers[_currentFrame];
    if (!vertexBuffer.mappedData || !indexBuffer.mappedData)
        return;

    std::memcpy(static_cast<u8*>(vertexBuffer.mappedData) + vertexOffset,
                vertices.data(), static_cast<size_t>(vertexBytes));
    std::memcpy(static_cast<u8*>(indexBuffer.mappedData) + indexOffset,
                indices.data(), static_cast<size_t>(indexBytes));

    _overlay2DVertexUsed[_currentFrame] = vertexOffset + vertexBytes;
    _overlay2DIndexUsed[_currentFrame] = indexOffset + indexBytes;

    /*
     * Opened on the frame's first batch and reused by every batch after, so a
     * frame with no overlay never pays for one. Separate from _sceneCmd
     * because endRenderPass() has to replay it *after* the scene for the
     * overlay to composite on top.
     */
    if (_overlayCmd == VK_NULL_HANDLE)
    {
        _overlayCmd = _vkCommandManager->acquireSecondaryCommandBuffer(_currentFrame);
        VkCommandManager::beginSecondaryCommandBuffer(
            _overlayCmd,
            *_vkRenderPassManager->getRenderPass(),
            _vkFrameBuffersManager->getFrameBuffers()[_currentImageIndex]);
        _overlayStateBound = false;
    }

    VkCommandBuffer cmd = _overlayCmd;
    const VkExtent2D extent = *_vkSwapChainManager->getExtent2D();

    /*
     * Pipeline and texture table are identical for every batch in the frame,
     * and nothing else records into this buffer, so they are bound once.
     * Bindless is what makes that true: before it, a batch switching texture
     * meant rebinding a descriptor set here.
     */
    if (!_overlayStateBound)
    {
        _vkOverlay2DPipelineManager->cmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS);
        _vkOverlay2DPipelineManager->cmdBindDescriptorSets(
            cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, 0, 1, &_bindlessTextureSet2D, 0, nullptr);
        _overlayStateBound = true;
    }

    /*
     * Window pixels -> clip space. cmdIndexedDraw() sets a negative-height
     * viewport (see its comment) so every Vulkan draw shares OpenGL/GLM's
     * Y-up NDC convention; bottom=height/top=0 is exactly the same swap the
     * OpenGL overlay path uses for that reason. _ZO because Vulkan's depth
     * range is [0,1]; the actual depth is irrelevant with the test disabled.
     */
    Overlay2DPushConstants pushConstants;
    pushConstants.projection = glm::orthoRH_ZO(
        0.0f, static_cast<f32>(extent.width),
        static_cast<f32>(extent.height), 0.0f,
        0.0f, 1.0f);
    pushConstants.textureIndex = textureArrayIndexOf(sampled);

    _vkOverlay2DPipelineManager->cmdPushConstants(cmd, &pushConstants);

    //! Bound at this batch's own slice, so its indices stay batch-relative and
    //! firstIndex/vertexOffset below can both remain zero.
    vkCmdBindVertexBuffers(cmd, 0, 1, &vertexBuffer.buffer, &vertexOffset);
    vkCmdBindIndexBuffer(cmd, indexBuffer.buffer, indexOffset, VK_INDEX_TYPE_UINT32);

    //! The whole batch in one call -- the point of the exercise.
    _vkOverlay2DPipelineManager->cmdIndexedDraw(
        cmd, extent, static_cast<u32>(indices.size()), 1, 0, 0, 0);

    /*
     * _recorded is deliberately left alone. It tracks bindings in _sceneCmd,
     * and this function records into _overlayCmd -- a different command
     * buffer, whose binds cannot disturb the scene's. Invalidating it here (as
     * this did while both shared the primary buffer) would only force the next
     * 3D draw into a pointless full rebind.
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
void VulkanRenderer::advanceFrame() { _currentFrame = (_currentFrame + 1) % GetMaxFramesInFlight(); }

} // namespace vk
} // namespace aura3d
