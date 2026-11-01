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
#include "aura/Core/Files.h"
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

    DescriptorBindingInfo materialBinding;
    materialBinding.binding = 1;
    materialBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    materialBinding.descriptorCount = 1;
    materialBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    pipeline.addDescriptorBinding(2, materialBinding);

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

    const bool enableValidation = settings->get(setting::vulkanValidation);

    _vmaConfig = VulkanMemoryManager::loadConfig(settings);
    _memoryManager = std::make_unique<VulkanMemoryManager>();

    createWindow(settings->windowTitle().c_str(), settings->windowBackend());
    createCoreObjects(enableValidation);

    // Only actually request it from VMA if the device genuinely supports it
    // (see VkDeviceManager::supportsBufferDeviceAddress)
    _vmaConfig.bufferDeviceAddress = _vmaConfig.bufferDeviceAddress && _vkDeviceManager->supportsBufferDeviceAddress();

    _msaaSamples =
        resolveSampleCount(settings->get(setting::rendererMsaa), _vkDeviceManager->getMaxUsableSampleCount());

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
    _pipelineCache = std::make_unique<VkPipelineCacheFile>(*dev, *_vkDeviceManager->getPhysicalDevice(),
                                                           AuraSettings::get()->get(setting::vulkanPipelineCache));

    _vkGraphicsPipelineManager = std::make_unique<VkGraphicsPipelineManager>(
        vk_shader3d_vert, vk_shader3d_vert_len, vk_shader3d_frag, vk_shader3d_frag_len, dev);
    declareScene3DInterface(*_vkGraphicsPipelineManager, _bindlessTextureCapacity);

    _vkBatchPipelineManager = std::make_unique<VkGraphicsPipelineManager>(vk_batch_vert, vk_batch_vert_len,
                                                                          vk_batch_frag, vk_batch_frag_len, dev);
    declareBatchInterface(*_vkBatchPipelineManager, _bindlessTextureCapacity);

    _vkInstancedPipelineManager = std::make_unique<VkGraphicsPipelineManager>(
        vk_shader3d_instanced_vert, vk_shader3d_instanced_vert_len, vk_shader3d_frag, vk_shader3d_frag_len, dev);
    declareScene3DInterface(*_vkInstancedPipelineManager, _bindlessTextureCapacity);

    _vkVertexBufferManager = std::make_unique<VkVertexBufferManager>(_memoryManager.get(), dev);
    _vkIndexBufferManager = std::make_unique<VkIndexBufferManager>(_memoryManager.get(), dev);
    _vkUniformBufferManager = std::make_unique<VkUniformBufferManager>(_memoryManager.get(), dev);
    _vkLightUniformBufferManager = std::make_unique<VkUniformBufferManager>(_memoryManager.get(), dev);
    _vkCommandManager = std::make_unique<VkCommandManager>(dev, _graphicsIndexFamily);

    _vkTextureManager = std::make_unique<VkTextureManager>(
        _memoryManager.get(), dev, _vkCommandManager->getThreadCommandPool(),
        _queueDataFromExclusiveFlags.front()->queues.front(), _vkDeviceManager->maxSamplerAnisotropy());

    _vkRenderSyncManager = std::make_unique<VkRenderSyncManager>(dev);

    //! Zero configured workers selects hardware concurrency.
    const int configuredThreads = AuraSettings::get()->get(setting::rendererCpuThreads);
    const unsigned detected = std::thread::hardware_concurrency();
    _recordWorkerCount = configuredThreads > 0 ? static_cast<u32>(configuredThreads) : (detected > 0 ? detected : 1u);

    //! The pool is built by the first drawMeshes() large enough to use it, so a
    //! scene that never reaches that size runs without the worker threads.
    INK_VERBOSE << "Command recording workers: " << _recordWorkerCount;

    _minDrawsToThread = static_cast<size_t>(AuraSettings::get()->get(setting::vulkanMinDrawsToThread));
    _minDrawsPerChunk = static_cast<size_t>(AuraSettings::get()->get(setting::vulkanMinDrawsPerChunk));
}

bool VulkanRenderer::reloadShaders(const std::filesystem::path &directory)
{
    if (!_isInitialized || _frameState != FrameState::Idle)
        return false;
    vkDeviceWaitIdle(*_vkDeviceManager->getDevice());
    const std::filesystem::path spirv = directory / "vulkan";
    const auto reload = [&](VkGraphicsPipelineManager *manager, const char *vert, const char *frag)
    {
        const auto vertex = readFileBytes(spirv / vert);
        const auto fragment = readFileBytes(spirv / frag);
        if (!vertex || !fragment)
        {
            INK_ERROR << "VulkanRenderer: cannot read " << (spirv / (vertex ? frag : vert)).string();
            return false;
        }
        return manager->reloadShaders(*vertex, *fragment);
    };
    bool reloaded = reload(_vkGraphicsPipelineManager.get(), "vk_shader3d.vert.spv", "vk_shader3d.frag.spv");
    reloaded =
        reload(_vkInstancedPipelineManager.get(), "vk_shader3d_instanced.vert.spv", "vk_shader3d.frag.spv") && reloaded;
    return reloaded;
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
    _transformSlots = static_cast<u32>(AuraSettings::get()->get(setting::vulkanTransformSlots));

    const VkPhysicalDeviceProperties *properties = nullptr;
    vmaGetPhysicalDeviceProperties(_memoryManager->getAllocator(), &properties);
    _vkUniformBufferManager->createUniformBuffers(sharingMode, framesInFlight, sizeof(gfx::TransformUBO),
                                                  _transformSlots, properties->limits.minUniformBufferOffsetAlignment);

    for (u32 i = 0; i < framesInFlight; ++i)
    {
        _vkUniformBufferManager->updateUniformBuffer(i, _currentTransform);
    }
    _transformSlotsUsed = 0;

    _vkLightUniformBufferManager->createUniformBuffers(sharingMode, framesInFlight, sizeof(gfx::LightingUBO));

    //! Fixed size and written only at new entries, so frames in flight never see one change under them.
    if (_materialTable.buffer == VK_NULL_HANDLE)
    {
        _materialTable = _memoryManager->createBuffer(
            sizeof(gfx::MaterialUniforms) * kMaterialCapacity, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
            VK_SHARING_MODE_EXCLUSIVE, VMA_MEMORY_USAGE_AUTO_PREFER_HOST,
            VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT);
        writeMaterial(0, Material{});
        for (usize i = 0; i < _materials.size(); ++i)
            writeMaterial(static_cast<u32>(i + 1), _materials[i]);
    }

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
    {
        const gfx::LightingUBO block = lighting();
        _vkLightUniformBufferManager->updateUniformBufferRaw(i, &block, sizeof(block));
    }
}

void VulkanRenderer::setLights(std::span<const gfx::Light> lights)
{
    IRenderer::setLights(lights);
    updateLightUniformBuffers();
}

RendererCaps VulkanRenderer::caps() const noexcept
{
    return RendererCaps::Pbr | RendererCaps::Instancing | RendererCaps::RenderTargets | RendererCaps::Compute |
           RendererCaps::CustomPipelines;
}

MaterialHandle VulkanRenderer::createMaterial(const Material &material)
{
    const MaterialHandle handle = IRenderer::createMaterial(material);
    if (handle.value() >= kMaterialCapacity)
        INK_WARN << "VulkanRenderer: material " << handle.value() << " is past the table's " << kMaterialCapacity
                 << " entries; it draws as the default material";
    else
        writeMaterial(handle.value(), material);
    return handle;
}

void VulkanRenderer::writeMaterial(u32 index, const Material &material)
{
    if (!_materialTable.mappedData || index >= kMaterialCapacity)
        return;
    gfx::MaterialUniforms entry = materialUniforms(material);
    entry.normalMap = textureArrayIndexOf(material.normalMap);
    entry.metallicRoughnessMap = textureArrayIndexOf(material.metallicRoughness);
    entry.emissiveMap = textureArrayIndexOf(material.emissiveMap);
    const VkDeviceSize offset = sizeof(entry) * index;
    std::memcpy(static_cast<u8 *>(_materialTable.mappedData) + offset, &entry, sizeof(entry));
    VK_RESULT_CHECK(
        vmaFlushAllocation(_memoryManager->getAllocator(), _materialTable.allocation, offset, sizeof(entry)));
}

u32 VulkanRenderer::materialIndexOf(MaterialHandle handle) const noexcept
{
    return isValidHandle(handle) && handle.value() < kMaterialCapacity ? handle.value() : 0u;
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
        _vkDescriptorManager->updateDescriptorSet(_lightDescSets[i], 1, _materialTable.buffer, VK_WHOLE_SIZE, 0,
                                                  VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
    }

    std::vector<VkVertexInputBindingDescription> bindings = {VkVertexBufferManager::getBindingDescription()};
    auto attributes = VkVertexBufferManager::getAttributeDescriptions();

    PipelineOptions sceneOptions{};
    sceneOptions.cache = _pipelineCache->handle();
    sceneOptions.state = PipelineState::DepthTest | PipelineState::DepthWrite | PipelineState::CullBackFaces;
    sceneOptions.sampleCount = _msaaSamples;

    _vkGraphicsPipelineManager->createPipeline(*_vkRenderPassManager->getRenderPass(),
                                               *_vkSwapChainManager->getExtent2D(), bindings, attributes,
                                               VkVertexBufferManager::getAttributeDescriptionCount(), sceneOptions);

    //! Binding 1 steps once per instance: a model matrix (locations 4-7) and a normal matrix's columns (8-10).
    bindings.push_back({1, sizeof(InstanceData), VK_VERTEX_INPUT_RATE_INSTANCE});
    for (u32 column = 0; column < 7; ++column)
        attributes[MAX_ATTRIBUTE_DESCRIPTION_3D + column] = {
            MAX_ATTRIBUTE_DESCRIPTION_3D + column, 1, VK_FORMAT_R32G32B32A32_SFLOAT,
            static_cast<u32>(column < 4 ? offsetof(InstanceData, model) + column * sizeof(glm::vec4)
                                        : offsetof(InstanceData, normalMatrix) + (column - 4) * sizeof(glm::vec4))};
    _vkInstancedPipelineManager->createPipeline(*_vkRenderPassManager->getRenderPass(),
                                                *_vkSwapChainManager->getExtent2D(), bindings, attributes,
                                                MAX_ATTRIBUTE_DESCRIPTION_INSTANCED, sceneOptions);

    createBatchPipeline();
    for (CustomPipeline &custom : _customPipelines)
        buildCustomPipeline(custom);

    _pipelineReady = true;
}

void VulkanRenderer::createBatchPipeline()
{
    // Screen batches sit on the near plane; world batches test existing depth.
    // Blended texels never occlude subsequent geometry through depth writes.
    PipelineOptions options{};
    options.cache = _pipelineCache->handle();
    options.state = PipelineState::DepthTest | PipelineState::AlphaBlend;
    options.depthCompare = VK_COMPARE_OP_LESS_OR_EQUAL;
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

void VulkanRenderer::setupCommandBuffers()
{
    _cmdBuffers = _vkCommandManager->createCommandBuffer();
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
    _pipelineReady = false;
    _frameState = FrameState::Idle;
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
    //! Their framebuffers hold views the texture manager owns, so they go first.
    destroyRenderTargets();
    destroyCompute();
    destroyCustomPipelines();

    if (_vkVertexBufferManager)
        _vkVertexBufferManager->cleanup();
    if (_vkIndexBufferManager)
        _vkIndexBufferManager->cleanup();
    if (_vkUniformBufferManager)
        _vkUniformBufferManager->cleanup();
    if (_vkLightUniformBufferManager)
        _vkLightUniformBufferManager->cleanup();
    if (_materialTable.buffer != VK_NULL_HANDLE)
        _memoryManager->destroyBuffer(_materialTable);
    if (_vkTextureManager)
        _vkTextureManager->cleanup();
    if (_depth.isValid())
        destroyDepthResources();
    if (_msaaColor.isValid())
        destroyMsaaColorResources();

    _vkDescriptorManager.reset();
    _vkFrameBuffersManager.reset();
    _vkBatchPipelineManager.reset();
    _vkInstancedPipelineManager.reset();
    _vkGraphicsPipelineManager.reset();
    _pipelineCache.reset();
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
                                               std::move(vertices));

    //! Resolve once here so the draw path never has to (see _vbByHandle).
    _vbByHandle.resize(handle.value());
    _vbByHandle[handle.value() - 1] = _vkVertexBufferManager->getVertexBuffer(name);
    return handle;
}

template <typename IndexT> IndexBufferHandle VulkanRenderer::createIndexBufferOf(std::vector<IndexT> &&indices)
{
    auto handle = _nextIbHandle++;
    std::string name = "ib_" + std::to_string(handle.value());

    _vkIndexBufferManager->createIndexBuffer(name, _vkCommandManager->getThreadCommandPool(),
                                             _vkSwapChainManager->getSwapchainCreateInfoKHR()->imageSharingMode,
                                             _queueDataFromExclusiveFlags.front()->queues.front(), std::move(indices));

    //! Resolve once here so the draw path never has to (see _ibByHandle).
    _ibByHandle.resize(handle.value());
    _ibByHandle[handle.value() - 1] = _vkIndexBufferManager->getIndexBuffer(name);
    return handle;
}

IndexBufferHandle VulkanRenderer::createIndexBuffer(std::vector<u16> &&indices)
{
    return createIndexBufferOf(std::move(indices));
}

IndexBufferHandle VulkanRenderer::createIndexBuffer(std::vector<u32> &&indices)
{
    return createIndexBufferOf(std::move(indices));
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

TextureHandle VulkanRenderer::createTexture(const TextureDesc &desc, std::span<const std::byte> pixels)
{
    if (!_vkTextureManager)
        return {};
    if (desc.format == TextureFormat::Depth)
    {
        INK_ERROR << "VulkanRenderer: TextureFormat::Depth is for render targets";
        return {};
    }
    const TextureHandle handle{_vkTextureManager->createTexture(desc, pixels)};
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
    _frameState = FrameState::Idle;

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
            return;
        }
    }

    if (windowFlags->resized)
    {
        windowFlags->resized = false;
        _targetStale = true;

        //! Size events repeat without the size changing (a system bar appearing, a focus change), and a rebuild
        //! is a blank stretch the user sees; only an out-of-date swapchain or a different extent earns one.
        if (_vkSwapChainManager->outOfDate() || !swapchainMatchesSurface())
        {
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
                return;
            }
        }
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
    _frameState = FrameState::Recording;
    _windowPassRecorded = false;

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
    resetComputePool();
    resetCustomPool();
}

void VulkanRenderer::beginRenderPass()
{
    AURA_FRAME_SCOPE(FramePhase::BeginPass);

    if (_frameState == FrameState::Idle)
        return;
    _frameState = FrameState::Recording;
    if (!_pipelineReady)
        return;

    beginPass(*_vkRenderPassManager->getRenderPass(), _vkFrameBuffersManager->getFrameBuffers()[_currentImageIndex],
              *_vkSwapChainManager->getExtent2D(), _vkRenderPassManager->hasDepth());
    _windowPassRecorded = true;
}

void VulkanRenderer::beginPass(VkRenderPass pass, VkFramebuffer framebuffer, VkExtent2D extent, bool clearDepth,
                               bool depthOnly)
{
    //! endRenderPass() appends the scene buffer after ending it, where it must not throw.
    _chunkCmds.reserve(1);
    VkCommandBuffer cmd = _cmdBuffers[_currentFrame];

    _vkGraphicsPipelineManager->selectPass(pass);
    _vkBatchPipelineManager->selectPass(pass);
    _vkInstancedPipelineManager->selectPass(pass);
    for (CustomPipeline &custom : _customPipelines)
        custom.manager->selectPass(pass);
    _passRenderPass = pass;
    _passFramebuffer = framebuffer;
    _passExtent = extent;

    //! The swapchain pass's depth attachment sits at index 1 with or without MSAA, like a target's; a depth-only
    //! pass has it alone, at index 0.
    const VkClearValue depthClear{.depthStencil = {1.0f, 0}};
    const std::array<VkClearValue, 2> clearValues =
        depthOnly
            ? std::array<VkClearValue, 2>{depthClear, depthClear}
            : std::array<VkClearValue, 2>{VkClearValue{.color = {{_clearR, _clearG, _clearB, _clearA}}}, depthClear};
    const VkRenderPassBeginInfo begin{
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
        .renderPass = pass,
        .framebuffer = framebuffer,
        .renderArea = {{0, 0}, extent},
        .clearValueCount = !depthOnly && clearDepth ? 2u : 1u,
        .pClearValues = clearValues.data(),
    };
    vkCmdBeginRenderPass(cmd, &begin, VK_SUBPASS_CONTENTS_SECONDARY_COMMAND_BUFFERS);

    _frameState = FrameState::InPass;

    _sceneCmd = _vkCommandManager->acquireSecondaryCommandBuffer(_currentFrame);
    VkCommandManager::beginSecondaryCommandBuffer(_sceneCmd, pass, framebuffer);

    // Secondary buffers inherit no pipeline bindings.
    _recorded.reset();

    if (_transformSlotsUsed == 0)
        publishTransform();
}

void VulkanRenderer::endRenderPass()
{
    AURA_FRAME_SCOPE(FramePhase::EndPass);

    if (_frameState != FrameState::InPass)
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
    _frameState = FrameState::Recording;

    _sceneCmd = VK_NULL_HANDLE;
    _chunkCmds.clear();
}

void VulkanRenderer::endFrame()
{
    if (_frameState == FrameState::Idle)
        return;
    //! The acquired image reaches PRESENT_SRC only through the window's pass: a frame that drew
    //! offscreen alone still runs it, cleared and empty.
    if (_frameState == FrameState::InPass)
        endRenderPass();
    if (!_windowPassRecorded && _pipelineReady)
    {
        beginRenderPass();
        endRenderPass();
    }

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

    //! A rotation can raise its size event before the surface has the new size; the swapchain was then built to the
    //! old one, and Android reports that only as the suboptimal result a rotated phone gives permanently.
    if (_vkSwapChainManager->takeUnverified() && !swapchainMatchesSurface())
        _windowManagerApi->getWindowFlags()->resized = true;

    AURA_FRAME_END();

    //! A present that fails raises resized or surfaceLost, which needsFrame() reports.
    _targetStale = false;
    _frameState = FrameState::Idle;
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
    if (_frameState != FrameState::Idle && (cameraChanged || _transformSlotsUsed == 0))
        publishTransform();
}

void VulkanRenderer::publishTransform()
{
    //! Recorded draws read their slot at GPU time, so a full ring is never reused.
    if (_transformSlotsUsed < _transformSlots)
        _vkUniformBufferManager->updateUniformBuffer(_currentFrame, _currentTransform, _transformSlotsUsed++);
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
    draw.materialIndex = materialIndexOf(_currentMaterialHandle);

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
            _passExtent,
            static_cast<u32>(_vkUniformBufferManager->getSlotStride() *
                             (_transformSlotsUsed > 0 ? _transformSlotsUsed - 1 : 0))};
}

void VulkanRenderer::drawIndexed(u32 indexCount, u32 instanceCount)
{
    if (_frameState != FrameState::InPass || !_pipelineReady || _sceneCmd == VK_NULL_HANDLE)
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
    if (_frameState != FrameState::InPass || !_pipelineReady || _sceneCmd == VK_NULL_HANDLE)
        return;

    //! See drawIndexed(): the pass records through secondary buffers.
    VkCommandBuffer cmd = _sceneCmd;
    bindDrawState(cmd);

    vkCmdDraw(cmd, vertexCount, instanceCount, 0, 0);
}

void VulkanRenderer::drawMeshes(std::span<const DrawItem> items)
{
    AURA_FRAME_SCOPE(FramePhase::RecordScene);

    if (_frameState != FrameState::InPass || !_pipelineReady || items.empty() || _sceneCmd == VK_NULL_HANDLE)
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
    const Frustum frustum = viewFrustum();
    for (const DrawItem &item : items)
    {
        if (isValidHandle(item.material))
            bindMaterial(item.material);

        const MeshRecord *mesh = getMesh(item.mesh);
        if (!mesh || culled(*mesh, item.model, frustum))
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
        draw.materialIndex = materialIndexOf(_currentMaterialHandle);
    }

    if (_resolvedDraws.empty())
        return;

    const SceneBindings bindings = sceneBindings();
    VkRenderPass renderPass = _passRenderPass;
    VkFramebuffer framebuffer = _passFramebuffer;

    // Small batches stay inline to avoid worker wakeups and secondary-buffer setup.
    const size_t maxChunks = _resolvedDraws.size() / _minDrawsPerChunk;
    const size_t chunkCount = (_resolvedDraws.size() < _minDrawsToThread)
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

void VulkanRenderer::drawMeshInstanced(MeshHandle mesh, MaterialHandle material, std::span<const glm::mat4> models)
{
    AURA_FRAME_SCOPE(FramePhase::RecordScene);
    if (_frameState != FrameState::InPass || !_pipelineReady || _sceneCmd == VK_NULL_HANDLE)
        return;
    if (isValidHandle(material))
        bindMaterial(material);
    const MeshRecord *record = getMesh(mesh);
    const VertexBufferInfo *vb = record ? vertexBufferOf(record->vertexBuffer) : nullptr;
    const IndexBufferInfo *ib = record ? indexBufferOf(record->indexBuffer) : nullptr;
    if (!vb || !ib)
        return;

    const Frustum frustum = viewFrustum();
    _instanceScratch.clear();
    for (const glm::mat4 &model : models)
        if (!culled(*record, model, frustum))
            _instanceScratch.push_back({model, glm::mat4(normalMatrixOf(model))});
    if (_instanceScratch.empty())
        return;
    const std::optional<VkDeviceSize> offset = uploadTransient(std::as_bytes(std::span(_instanceScratch)));
    if (!offset)
        return;

    ResolvedDraw draw;
    draw.vertexBuffer = vb->buffer;
    draw.indexBuffer = ib->buffer;
    draw.indexType = ib->indexType;
    draw.indexCount = record->indexCount;
    draw.textureIndex = textureArrayIndexOf(_currentTexture);
    draw.materialIndex = materialIndexOf(_currentMaterialHandle);
    SceneBindings bindings = sceneBindings();
    bindings.pipeline = _vkInstancedPipelineManager.get();
    aura3d::vk::bindDrawState(_sceneCmd, _recorded, bindings, draw);
    vkCmdBindVertexBuffers(_sceneCmd, 1, 1, &_batchVertexBuffers[_currentFrame].buffer, &*offset);
    vkCmdDrawIndexed(_sceneCmd, record->indexCount, static_cast<u32>(_instanceScratch.size()), 0, 0, 0);
}

RendererChoice VulkanRenderer::getBackendType() const
{
    return RendererChoice::VULKAN;
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
