#ifndef VKGRAPHICSPIPELINEMANAGER_H
#define VKGRAPHICSPIPELINEMANAGER_H

#pragma once

#include <array>
#include <string>
#include <vector>
#include <unordered_map>

#include "aura/Renderer/Vulkan/VkAura/VkAuraCore.h"
#include "aura/Renderer/Vulkan/VkAura/VkPipelineManager/VkPipelineManager.h"
#include "aura/Renderer/Vulkan/VkAura/VkShader/ShaderManager.h"

namespace aura3d {
namespace vk {

class VkGraphicsPipelineManager : public VkPipelineManager
{
public:
    VkGraphicsPipelineManager(std::string shader_vert_spv,
                              std::string shader_frag_spv,
                              VkDevice* device);

    VkGraphicsPipelineManager(const unsigned char* vertData, u32 vertSize,
                              const unsigned char* fragData, u32 fragSize,
                              VkDevice* device);

    ~VkGraphicsPipelineManager();

    //! Add a descriptor binding to a specific set
    void addDescriptorBinding(u32 setIndex,
                              const DescriptorBindingInfo& bindingInfo);

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
    void cmdPushConstants(VkCommandBuffer commandBuffer, const void* data);

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

    void createPipeline(VkRenderPass renderPass,
                        VkExtent2D extent,
                        const std::vector<VkVertexInputBindingDescription>& vertexBindingDescArray,
                        const AttributeDescriptionArray<VkVertexInputAttributeDescription>& vertexAttributeDescArray,
                        u32 attributeDescriptionCount = MAX_ATTRIBUTE_DESCRIPTION,
                        const PipelineOptions& options = PipelineOptions{});

    //! Bind the pipeline and all descriptor sets to the command buffer
    void cmdBindPipeline(VkCommandBuffer commandBuffer, VkPipelineBindPoint bindPoint);

    //! Bind specific descriptor sets to the command buffer
    void cmdBindDescriptorSets(VkCommandBuffer commandBuffer,
                               VkPipelineBindPoint bindPoint,
                               u32 firstSet,
                               u32 descriptorSetCount,
                               const VkDescriptorSet* pDescriptorSets,
                               u32 dynamicOffsetCount = 0,
                               const u32* pDynamicOffsets = nullptr);

    /**
     * @brief Record the dynamic viewport and scissor covering @p extent.
     *
     * Dynamic state is sticky for the rest of the command buffer, so this
     * belongs once per render pass rather than in front of every draw. The
     * cmdIndexedDraw()/cmdDraw() overloads taking an extent still call it for
     * callers that do not track their own state.
     */
    static void cmdSetViewportAndScissor(VkCommandBuffer commandBuffer, VkExtent2D extent) noexcept;

    void cmdIndexedDraw(VkCommandBuffer commandBuffer,
                        VkExtent2D extent,
                        u32 indexCount,
                        u32 instanceCount,
                        u32 firstIndex,
                        i32 vertexOffset,
                        u32 firstInstance);

    //! Draw vertices
    void cmdDraw(VkCommandBuffer commandBuffer,
                 VkExtent2D extent,
                 u32 vertexCount,
                 u32 instanceCount = 1,
                 u32 firstVertex = 0,
                 u32 firstInstance = 0);

    //! Get pipeline layout (needed for descriptor set binding)
    VkPipelineLayout getPipelineLayout() const { return _pipelineLayout; }

    //! The pipeline object itself, so callers can detect a redundant rebind.
    [[nodiscard]] VkPipeline getPipeline() const noexcept { return _pipeline; }

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
};

}
}
#endif // VKGRAPHICSPIPELINEMANAGER_H
