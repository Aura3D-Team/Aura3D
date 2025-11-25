#ifndef VKGRAPHICSPIPELINEMANAGER_H
#define VKGRAPHICSPIPELINEMANAGER_H

#pragma once

#include <array>
#include <string>
#include <vector>
#include <unordered_map>

#include "VkAura/VkAuraCore.h"
#include "VkAura/VkPipelineManager/VkPipelineManager.h"
#include "VkAura/VkShader/ShaderManager.h"
#include "VkAura/VkMemory/VkHostAllocator/VkHostAllocator.h"

namespace aura3d {
namespace vk {

class VkGraphicsPipelineManager : public VkPipelineManager
{
public:
    VkGraphicsPipelineManager(VkHostAllocator* vkHostAllocator,
                              std::string shader_vert_spv,
                              std::string shader_frag_spv,
                              VkDevice* device);
    ~VkGraphicsPipelineManager();

    // Add a descriptor binding to a specific set
    void addDescriptorBinding(u32 setIndex,
                              const DescriptorBindingInfo& bindingInfo);

    // Create descriptor set layouts from the specified bindings
    void createDescriptorSetLayouts();

    // Create the pipeline with vertex input and descriptor set layouts
    void createPipeline(VkRenderPass renderPass,
                        VkExtent2D extent,
                        const std::vector<VkVertexInputBindingDescription>& vertexBindingDescArray,
                        const AttributeDescriptionArray<VkVertexInputAttributeDescription>& vertexAttributeDescArray);

    // Bind the pipeline and all descriptor sets to the command buffer
    void cmdBindPipeline(VkCommandBuffer commandBuffer, VkPipelineBindPoint bindPoint);

    // Bind specific descriptor sets to the command buffer
    void cmdBindDescriptorSets(VkCommandBuffer commandBuffer,
                               VkPipelineBindPoint bindPoint,
                               u32 firstSet,
                               u32 descriptorSetCount,
                               const VkDescriptorSet* pDescriptorSets,
                               u32 dynamicOffsetCount = 0,
                               const u32* pDynamicOffsets = nullptr);

    void cmdIndexedDraw(VkCommandBuffer commandBuffer,
                        VkExtent2D extent,
                        u32 indexCount,
                        u32 instanceCount,
                        u32 firstIndex,
                        i32 vertexOffset,
                        u32 firstInstance);

    // Draw vertices
    void cmdDraw(VkCommandBuffer commandBuffer,
                 VkExtent2D extent,
                 u32 vertexCount,
                 u32 instanceCount = 1,
                 u32 firstVertex = 0,
                 u32 firstInstance = 0);

    // Get pipeline layout (needed for descriptor set binding)
    VkPipelineLayout getPipelineLayout() const { return _pipelineLayout; }

    // Get descriptor set layout for a specific set
    VkDescriptorSetLayout getDescriptorSetLayout(u32 setIndex) const;

private:
    VkHostAllocator* vkHostAllocator;
    VkShaderManager _shaderManager;
    std::array<VkPipelineShaderStageCreateInfo, 2> _shaderStages;
    std::array<VkDynamicState, 2> _dynamicStates;

    // Storage for descriptor set layouts
    std::unordered_map<u32, DescriptorSetLayoutInfo> _descriptorSetLayoutInfos;
    std::unordered_map<u32, VkDescriptorSetLayout> _descriptorSetLayouts;

    // Initialize default dynamic states
    void initializeDynamicStates();
};

}
}
#endif // VKGRAPHICSPIPELINEMANAGER_H
