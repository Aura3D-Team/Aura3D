#ifndef VKGRAPHICSPIPELINEMANAGER_H
#define VKGRAPHICSPIPELINEMANAGER_H

#pragma once

#include <array>
#include <string>
#include <vector>
#include <unordered_map>

#include "VkAura/VkAuraDefs.h"
#include <VkAura/VkPipelineManager/VkPipelineManager.h>
#include <VkAura/VkShaderManager/VkShaderManager.h>
#include <VkAura/VkSwapChainManager/VkSwapChainManager.h>
#include <VkAura/VkHostAllocator/VkHostAllocator.h>


namespace aura3d {

class VkGraphicsPipelineManager : public VkPipelineManager
{
public:
    VkGraphicsPipelineManager(VkHostAllocator* vkHostAllocator,
                              std::string shader_vert_spv,
                              std::string shader_frag_spv,
                              VkDevice* device);
    ~VkGraphicsPipelineManager();

    // Add a descriptor binding to a specific set
    void addDescriptorBinding(uint32_t setIndex,
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
                               uint32_t firstSet,
                               uint32_t descriptorSetCount,
                               const VkDescriptorSet* pDescriptorSets,
                               uint32_t dynamicOffsetCount = 0,
                               const uint32_t* pDynamicOffsets = nullptr);

    // Draw vertices
    void cmdDraw(VkCommandBuffer commandBuffer,
                 VkExtent2D extent,
                 uint32_t vertexCount,
                 uint32_t instanceCount = 1,
                 uint32_t firstVertex = 0,
                 uint32_t firstInstance = 0);

    // Get pipeline layout (needed for descriptor set binding)
    VkPipelineLayout getPipelineLayout() const { return _pipelineLayout; }

    // Get descriptor set layout for a specific set
    VkDescriptorSetLayout getDescriptorSetLayout(uint32_t setIndex) const;

private:
    VkHostAllocator* vkHostAllocator;
    VkShaderManager _shaderManager;
    std::array<VkPipelineShaderStageCreateInfo, 2> _shaderStages;
    std::array<VkDynamicState, 2> _dynamicStates;

    // Storage for descriptor set layouts
    std::unordered_map<uint32_t, DescriptorSetLayoutInfo> _descriptorSetLayoutInfos;
    std::unordered_map<uint32_t, VkDescriptorSetLayout> _descriptorSetLayouts;

    // Initialize default dynamic states
    void initializeDynamicStates();
};

}
#endif // VKGRAPHICSPIPELINEMANAGER_H
