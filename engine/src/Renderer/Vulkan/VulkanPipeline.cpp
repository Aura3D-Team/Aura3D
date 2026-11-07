#include "aura/Renderer/Vulkan/VulkanRenderer.h"

#include <algorithm>
#include <array>
#include <cstring>

#include "aura/Core/AuraException/AuraException.h"
#include "aura/Core/Profiling/FrameProfiler.h"
#include "aura/aura.h"

namespace aura3d
{
namespace vk
{

namespace
{

//! Sets in a pool, and the descriptors of each type it is given room for per set: slack, not a limit, since a pool
//! is made to fit the request that outgrew the last.
constexpr u32 kSetsPerPool = 256;
constexpr u32 kDescriptorsPerSet = 16;
// NOLINTNEXTLINE(bugprone-signed-bitwise): Vulkan flag bits are C enums.
constexpr VkShaderStageFlags kStages = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

[[nodiscard]] bool isSpirv(std::span<const std::byte> code) noexcept
{
    constexpr u32 kMagic = 0x07230203u;
    u32 magic = 0;
    if (code.size() < sizeof(magic) || code.size() % 4 != 0)
        return false;
    std::memcpy(&magic, code.data(), sizeof(magic));
    return magic == kMagic;
}

} // namespace

PipelineHandle VulkanRenderer::createPipeline(const PipelineDesc &desc)
{
    if (!_isInitialized || !_pipelineReady)
    {
        INK_ERROR << "VulkanRenderer: createPipeline needs an initialised renderer";
        return {};
    }
    if (!isSpirv(desc.vertexSpirv) || !isSpirv(desc.fragmentSpirv))
    {
        INK_ERROR << "VulkanRenderer: createPipeline needs PipelineDesc::vertexSpirv and fragmentSpirv";
        return {};
    }
    const PipelineLimits limits = pipelineLimits();
    if (desc.textures > limits.textures || desc.buffers > limits.buffers || desc.constantBytes > limits.constantBytes ||
        desc.constantBytes % 16 != 0)
    {
        INK_ERROR << "VulkanRenderer: this device lets a pipeline read " << limits.textures << " textures, "
                  << limits.buffers << " uniform buffers and " << limits.constantBytes
                  << " bytes of constants (a multiple of 16)";
        return {};
    }

    CustomPipeline pipeline;
    pipeline.textures = desc.textures;
    pipeline.buffers = desc.buffers;
    pipeline.constantBytes = desc.constantBytes;
    pipeline.input = desc.input;
    pipeline.options.cache = _pipelineCache->handle();
    pipeline.options.state = desc.state;
    pipeline.options.sampleCount = _msaaSamples;
    if (desc.input == VertexInput::Mesh)
    {
        pipeline.bindings = {VkVertexBufferManager::getBindingDescription()};
        pipeline.attributes = VkVertexBufferManager::getAttributeDescriptions();
        pipeline.attributeCount = VkVertexBufferManager::getAttributeDescriptionCount();
    }
    try
    {
        pipeline.manager = std::make_unique<VkGraphicsPipelineManager>(
            reinterpret_cast<const unsigned char *>(desc.vertexSpirv.data()), static_cast<u32>(desc.vertexSpirv.size()),
            reinterpret_cast<const unsigned char *>(desc.fragmentSpirv.data()),
            static_cast<u32>(desc.fragmentSpirv.size()), _vkDeviceManager->getDevice());
        VkGraphicsPipelineManager &manager = *pipeline.manager;
        manager.resetInterface();
        //! Textures are set 0 and uniform buffers set 1; a pipeline with buffers alone still has an empty set 0.
        if (desc.textures == 0 && desc.buffers > 0)
            manager.declareSet(0);
        for (u32 i = 0; i < desc.textures; ++i)
        {
            DescriptorBindingInfo binding;
            binding.binding = i;
            binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            binding.descriptorCount = 1;
            binding.stageFlags = kStages;
            manager.addDescriptorBinding(0, binding);
        }
        for (u32 i = 0; i < desc.buffers; ++i)
        {
            DescriptorBindingInfo binding;
            binding.binding = i;
            binding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            binding.descriptorCount = 1;
            binding.stageFlags = kStages;
            manager.addDescriptorBinding(1, binding);
        }
        if (desc.constantBytes > 0)
            manager.setPushConstantRange(kStages, 0, desc.constantBytes);
        buildCustomPipeline(pipeline);
    }
    catch (const std::exception &error)
    {
        INK_ERROR << "VulkanRenderer: pipeline rejected: " << error.what();
        return {};
    }
    _customPipelines.push_back(std::move(pipeline));
    return PipelineHandle{static_cast<u32>(_customPipelines.size())};
}

void VulkanRenderer::buildCustomPipeline(CustomPipeline &pipeline)
{
    pipeline.manager->createPipeline(*_vkRenderPassManager->getRenderPass(), *_vkSwapChainManager->getExtent2D(),
                                     pipeline.bindings, pipeline.attributes, pipeline.attributeCount, pipeline.options);
    for (const TargetPass &target : _targetPasses)
        pipeline.manager->addVariant(target.pass, target.colour == VK_FORMAT_UNDEFINED ? 0u : 1u);
}

PipelineLimits VulkanRenderer::pipelineLimits() const noexcept
{
    if (!_isInitialized || !_memoryManager)
        return {};
    const VkPhysicalDeviceProperties *properties = nullptr;
    vmaGetPhysicalDeviceProperties(_memoryManager->getAllocator(), &properties);
    const VkPhysicalDeviceLimits &limits = properties->limits;
    //! A combined image sampler counts as a sampler and as a sampled image, in a stage and in a set.
    return {std::min({limits.maxPerStageDescriptorSamplers, limits.maxPerStageDescriptorSampledImages,
                      limits.maxDescriptorSetSamplers, limits.maxDescriptorSetSampledImages}),
            std::min(limits.maxPerStageDescriptorUniformBuffers, limits.maxDescriptorSetUniformBuffers),
            limits.maxPushConstantsSize & ~15u, limits.maxUniformBufferRange};
}

VkDescriptorSet VulkanRenderer::allocateCustomSet(VkDescriptorSetLayout layout, u32 descriptors)
{
    VkDevice device = *_vkDeviceManager->getDevice();
    if (_customPools.size() < _cmdBuffers.size())
        _customPools.resize(_cmdBuffers.size());
    std::vector<VkDescriptorPool> &pools = _customPools[_currentFrame];
    VkDescriptorSetAllocateInfo allocation{
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, .descriptorSetCount = 1, .pSetLayouts = &layout};
    VkDescriptorSet set = VK_NULL_HANDLE;
    if (!pools.empty())
    {
        allocation.descriptorPool = pools.back();
        if (vkAllocateDescriptorSets(device, &allocation, &set) == VK_SUCCESS)
            return set;
    }
    //! The last pool is full (or there is none): another, sized for this request.
    const u32 room = kSetsPerPool * std::max(descriptors, kDescriptorsPerSet);
    const std::array<VkDescriptorPoolSize, 2> sizes{
        VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, room},
        VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, room}};
    const VkDescriptorPoolCreateInfo poolInfo{.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
                                              .maxSets = kSetsPerPool,
                                              .poolSizeCount = static_cast<u32>(sizes.size()),
                                              .pPoolSizes = sizes.data()};
    VkDescriptorPool pool = VK_NULL_HANDLE;
    VK_RESULT_CHECK(vkCreateDescriptorPool(device, &poolInfo, nullptr, &pool));
    pools.push_back(pool);
    allocation.descriptorPool = pool;
    if (vkAllocateDescriptorSets(device, &allocation, &set) != VK_SUCCESS)
    {
        INK_ERROR << "VulkanRenderer: a descriptor set of " << descriptors << " bindings could not be allocated";
        return VK_NULL_HANDLE;
    }
    return set;
}

bool VulkanRenderer::customSetsFor(const CustomPipeline &pipeline, u32 index, const PipelineDraw &draw)
{
    //! What the sets hold: one handle per texture, then one per buffer; a missing one is the invalid handle.
    CustomSet key;
    key.pipeline = index;
    key.handles.reserve(pipeline.textures + pipeline.buffers);
    for (u32 i = 0; i < pipeline.textures; ++i)
        key.handles.push_back(i < draw.textures.size() ? draw.textures[i].value() : TextureHandle{}.value());
    for (u32 i = 0; i < pipeline.buffers; ++i)
        key.handles.push_back(draw.buffers[i].value());
    if (_lastCustomSet.pipeline == key.pipeline && _lastCustomSet.handles == key.handles &&
        (pipeline.textures == 0 || _lastCustomSet.sets[0] != VK_NULL_HANDLE) &&
        (pipeline.buffers == 0 || _lastCustomSet.sets[1] != VK_NULL_HANDLE))
        return true;

    VkDevice device = *_vkDeviceManager->getDevice();
    std::vector<VkDescriptorImageInfo> images(pipeline.textures);
    std::vector<VkDescriptorBufferInfo> buffers(pipeline.buffers);
    std::vector<VkWriteDescriptorSet> writes;
    writes.reserve(pipeline.textures + pipeline.buffers);
    if (pipeline.textures > 0)
    {
        key.sets[0] = allocateCustomSet(pipeline.manager->getDescriptorSetLayout(0), pipeline.textures);
        if (key.sets[0] == VK_NULL_HANDLE)
            return false;
        for (u32 i = 0; i < pipeline.textures; ++i)
        {
            //! An unbound slot samples white, as an unbound texture does everywhere else.
            const auto *texture = i < draw.textures.size() && isValidHandle(draw.textures[i])
                                      ? _vkTextureManager->getTexture(draw.textures[i].value())
                                      : nullptr;
            if (!texture)
                texture = _vkTextureManager->getTexture(_fallbackTexture.value());
            images[i] = {texture->sampler, texture->view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
            writes.push_back({.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                              .dstSet = key.sets[0],
                              .dstBinding = i,
                              .descriptorCount = 1,
                              .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                              .pImageInfo = &images[i]});
        }
    }
    if (pipeline.buffers > 0)
    {
        key.sets[1] = allocateCustomSet(pipeline.manager->getDescriptorSetLayout(1), pipeline.buffers);
        if (key.sets[1] == VK_NULL_HANDLE)
            return false;
        const VkDeviceSize range = pipelineLimits().bufferBytes;
        for (u32 i = 0; i < pipeline.buffers; ++i)
        {
            const ComputeBuffer *buffer = computeBufferOf(draw.buffers[i]);
            buffers[i] = {buffer->buffer.buffer, 0, std::min(buffer->size, range)};
            writes.push_back({.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                              .dstSet = key.sets[1],
                              .dstBinding = i,
                              .descriptorCount = 1,
                              .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                              .pBufferInfo = &buffers[i]});
        }
    }
    vkUpdateDescriptorSets(device, static_cast<u32>(writes.size()), writes.data(), 0, nullptr);
    _lastCustomSet = std::move(key);
    return true;
}

void VulkanRenderer::drawPipeline(const PipelineDraw &draw)
{
    AURA_FRAME_SCOPE(FramePhase::RecordScene);
    if (_frameState != FrameState::InPass || !_pipelineReady || _sceneCmd == VK_NULL_HANDLE ||
        !isValidHandle(draw.pipeline) || draw.pipeline.value() > _customPipelines.size())
        return;
    const u32 index = draw.pipeline.value();
    CustomPipeline &pipeline = _customPipelines[index - 1];
    if (draw.constants.size() > pipeline.constantBytes)
    {
        INK_ERROR << "VulkanRenderer: drawPipeline given " << draw.constants.size()
                  << " bytes of constants; the pipeline has " << pipeline.constantBytes;
        return;
    }
    for (u32 i = 0; i < pipeline.buffers; ++i)
        if (i >= draw.buffers.size() || !computeBufferOf(draw.buffers[i]))
        {
            INK_ERROR << "VulkanRenderer: drawPipeline needs buffer " << i << " (createBuffer)";
            return;
        }

    const MeshRecord *record = nullptr;
    const VertexBufferInfo *vertices = nullptr;
    const IndexBufferInfo *indices = nullptr;
    if (pipeline.input == VertexInput::Mesh)
    {
        record = getMesh(draw.mesh);
        vertices = record ? vertexBufferOf(record->vertexBuffer) : nullptr;
        indices = record ? indexBufferOf(record->indexBuffer) : nullptr;
        if (!vertices || !indices)
            return;
    }
    const VkPipeline vkPipeline = pipeline.manager->getPipeline();
    if (vkPipeline == VK_NULL_HANDLE)
        return;
    if (pipeline.textures + pipeline.buffers > 0 && !customSetsFor(pipeline, index, draw))
        return;

    VkCommandBuffer cmd = _sceneCmd;
    const VkPipelineLayout layout = pipeline.manager->getPipelineLayout();
    if (_recorded.pipeline != vkPipeline)
    {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, vkPipeline);
        _recorded.pipeline = vkPipeline;
        //! Another layout: the built-in sets must be bound again before their next draw.
        _recorded.staticSetsBound = false;
    }
    if (!_recorded.viewportSet)
    {
        VkGraphicsPipelineManager::cmdSetViewportAndScissor(cmd, _passExtent);
        _recorded.viewportSet = true;
    }
    if (pipeline.textures > 0)
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 0, 1, &_lastCustomSet.sets[0], 0,
                                nullptr);
    if (pipeline.buffers > 0)
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 1, 1, &_lastCustomSet.sets[1], 0,
                                nullptr);
    if (pipeline.constantBytes > 0)
    {
        _customConstants.assign(pipeline.constantBytes, std::byte{0});
        std::memcpy(_customConstants.data(), draw.constants.data(), draw.constants.size());
        vkCmdPushConstants(cmd, layout, kStages, 0, pipeline.constantBytes, _customConstants.data());
    }
    //! Only a VertexInput::None pipeline gets here without a mesh: a mesh pipeline returned above without one.
    if (!vertices || !indices)
    {
        vkCmdDraw(cmd, draw.vertexCount, draw.instanceCount, 0, 0);
        return;
    }
    if (_recorded.vertexBuffer != vertices->buffer)
    {
        constexpr VkDeviceSize kOrigin = 0;
        vkCmdBindVertexBuffers(cmd, 0, 1, &vertices->buffer, &kOrigin);
        _recorded.vertexBuffer = vertices->buffer;
    }
    if (_recorded.indexBuffer != indices->buffer || _recorded.indexType != indices->indexType)
    {
        vkCmdBindIndexBuffer(cmd, indices->buffer, 0, indices->indexType);
        _recorded.indexBuffer = indices->buffer;
        _recorded.indexType = indices->indexType;
    }
    vkCmdDrawIndexed(cmd, record->indexCount, draw.instanceCount, 0, 0, 0);
}

void VulkanRenderer::resetCustomPool()
{
    _lastCustomSet = {};
    if (_currentFrame < _customPools.size())
        for (const VkDescriptorPool pool : _customPools[_currentFrame])
            VK_RESULT_CHECK(vkResetDescriptorPool(*_vkDeviceManager->getDevice(), pool, 0));
}

void VulkanRenderer::destroyCustomPipelines() noexcept
{
    if (!_vkDeviceManager || !_vkDeviceManager->getDevice())
        return;
    for (const std::vector<VkDescriptorPool> &pools : _customPools)
        for (const VkDescriptorPool pool : pools)
            vkDestroyDescriptorPool(*_vkDeviceManager->getDevice(), pool, nullptr);
    _customPools.clear();
    _lastCustomSet = {};
    _customPipelines.clear();
}

} // namespace vk
} // namespace aura3d
