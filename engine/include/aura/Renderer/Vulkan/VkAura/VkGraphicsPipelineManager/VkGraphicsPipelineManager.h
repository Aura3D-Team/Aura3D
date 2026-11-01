#ifndef VKGRAPHICSPIPELINEMANAGER_H
#define VKGRAPHICSPIPELINEMANAGER_H

#pragma once

#include <array>
#include <span>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "aura/Renderer/Vulkan/VkAura/VkAuraCore.h"
#include "aura/Renderer/Vulkan/VkAura/VkPipelineManager/VkPipelineManager.h"
#include "aura/Renderer/Vulkan/VkAura/VkShader/ShaderManager.h"

namespace aura3d
{
namespace vk
{

class VkGraphicsPipelineManager : public VkPipelineManager
{
  public:
    VkGraphicsPipelineManager(const std::string &shader_vert_spv, const std::string &shader_frag_spv, VkDevice *device);

    VkGraphicsPipelineManager(const unsigned char *vertData, u32 vertSize, const unsigned char *fragData, u32 fragSize,
                              VkDevice *device);

    ~VkGraphicsPipelineManager();

    //! Add a descriptor binding to a specific set
    void addDescriptorBinding(u32 setIndex, const DescriptorBindingInfo &bindingInfo);
    //! An empty set at @p setIndex, so a higher set can exist without the lower ones having a binding.
    void declareSet(u32 setIndex);

    /**
     * @brief Declares a push-constant range on the pipeline layout.
     *
     * Must be called before createDescriptorSetLayouts(), which is what builds
     * the layout. Vulkan guarantees at least 128 bytes.
     */
    void setPushConstantRange(VkShaderStageFlags stageFlags, u32 offset, u32 size);

    /**
     * @brief Records a push-constant write for the declared range.
     */
    void cmdPushConstants(VkCommandBuffer commandBuffer, const void *data);

    /**
     * @brief Discards every descriptor binding and push-constant range declared
     *        so far, along with any layouts already built from them.
     *
     * The constructor installs the 3D scene interface (sets 0/1/2 and a 128-byte
     * push-constant block). A pipeline whose shaders present a different
     * interface -- the unlit 2D overlay declares only set 0 and a 64-byte
     * range -- calls this first and then declares its own, rather than
     * inheriting bindings its shaders never reference.
     *
     * Must be called before createDescriptorSetLayouts().
     */
    void resetInterface();

    //! Create descriptor set layouts from the specified bindings
    void createDescriptorSetLayouts();

    void createPipeline(VkRenderPass renderPass, VkExtent2D extent,
                        const std::vector<VkVertexInputBindingDescription> &vertexBindingDescArray,
                        const AttributeDescriptionArray<VkVertexInputAttributeDescription> &vertexAttributeDescArray,
                        u32 attributeDescriptionCount = MAX_ATTRIBUTE_DESCRIPTION,
                        const PipelineOptions &options = PipelineOptions{});

    //! New SPIR-V for both stages, the pipeline and its variants rebuilt with what createPipeline() was given.
    //! False, with the old shaders still in use, when either module or pipeline is rejected. The GPU must be idle.
    bool reloadShaders(std::span<const std::byte> vertSpirv, std::span<const std::byte> fragSpirv);
    //! Builds, and rebuilds with every createPipeline(), a single-sampled copy for @p renderPass, which writes
    //! @p colorAttachments colour images: 0 for a depth-only pass.
    void addVariant(VkRenderPass renderPass, u32 colorAttachments = 1);
    //! getPipeline() and cmdBindPipeline() use @p renderPass's pipeline until the next call. Outside
    //! recording only: worker threads read the selection.
    void selectPass(VkRenderPass renderPass) noexcept;

    //! Bind the pipeline and all descriptor sets to the command buffer
    void cmdBindPipeline(VkCommandBuffer commandBuffer, VkPipelineBindPoint bindPoint);

    //! Bind specific descriptor sets to the command buffer
    void cmdBindDescriptorSets(VkCommandBuffer commandBuffer, VkPipelineBindPoint bindPoint, u32 firstSet,
                               u32 descriptorSetCount, const VkDescriptorSet *pDescriptorSets,
                               u32 dynamicOffsetCount = 0, const u32 *pDynamicOffsets = nullptr);

    /**
     * @brief Record the dynamic viewport and scissor covering @p extent.
     *
     * Dynamic state is sticky for the rest of the command buffer, so this
     * belongs once per render pass rather than in front of every draw. The
     * cmdIndexedDraw()/cmdDraw() overloads taking an extent still call it for
     * callers that do not track their own state.
     */
    static void cmdSetViewportAndScissor(VkCommandBuffer commandBuffer, VkExtent2D extent) noexcept;

    void cmdIndexedDraw(VkCommandBuffer commandBuffer, VkExtent2D extent, u32 indexCount, u32 instanceCount,
                        u32 firstIndex, i32 vertexOffset, u32 firstInstance);

    //! Draw vertices
    void cmdDraw(VkCommandBuffer commandBuffer, VkExtent2D extent, u32 vertexCount, u32 instanceCount = 1,
                 u32 firstVertex = 0, u32 firstInstance = 0);

    //! Get pipeline layout (needed for descriptor set binding)
    VkPipelineLayout getPipelineLayout() const
    {
        return _pipelineLayout;
    }

    //! The pipeline object itself, so callers can detect a redundant rebind.
    [[nodiscard]] VkPipeline getPipeline() const noexcept
    {
        return _pipeline;
    }

    //! Get descriptor set layout for a specific set
    VkDescriptorSetLayout getDescriptorSetLayout(u32 setIndex) const;

  private:
    void _init();

    VkShaderManager _shaderManager;
    std::array<VkPipelineShaderStageCreateInfo, 2> _shaderStages;
    std::array<VkDynamicState, 2> _dynamicStates;

    //! Storage for descriptor set layouts
    std::unordered_map<u32, DescriptorSetLayoutInfo> _descriptorSetLayoutInfos;
    std::unordered_map<u32, VkDescriptorSetLayout> _descriptorSetLayouts;

    //! Optional push-constant range folded into the pipeline layout.
    VkPushConstantRange _pushConstantRange{};
    bool _hasPushConstants = false;

    [[nodiscard]] VkPipeline build(VkRenderPass renderPass, const PipelineOptions &options);
    [[nodiscard]] VkPipeline buildVariant(VkRenderPass renderPass);
    void destroyPipelines() noexcept;

    //! What the last createPipeline() was given, so variants can be built from it.
    struct BuildInputs
    {
        VkExtent2D extent{};
        std::vector<VkVertexInputBindingDescription> bindings;
        AttributeDescriptionArray<VkVertexInputAttributeDescription> attributes{};
        u32 attributeCount = 0;
        PipelineOptions options{};
    };
    BuildInputs _inputs;
    VkRenderPass _mainPass = VK_NULL_HANDLE;
    VkPipeline _mainPipeline = VK_NULL_HANDLE;
    std::vector<std::pair<VkRenderPass, VkPipeline>> _variants;
    //! Colour attachment counts of the variants that are not the default.
    std::unordered_map<VkRenderPass, u32> _variantColour;
};

} // namespace vk
} // namespace aura3d
#endif // VKGRAPHICSPIPELINEMANAGER_H
