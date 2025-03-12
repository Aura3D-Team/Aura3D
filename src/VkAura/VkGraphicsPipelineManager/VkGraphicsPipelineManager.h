#ifndef VKGRAPHICSPIPELINEMANAGER_H
#define VKGRAPHICSPIPELINEMANAGER_H

#include <VkAura/VkPipelineManager/VkPipelineManager.h>
#include <VkAura/VkShaderManager/VkShaderManager.h>
#include <VkAura/VkSwapChainManager/VkSwapChainManager.h>

#include <array>
#include <string>

namespace aura3d {

class VkGraphicsPipelineManager : public VkPipelineManager
{
public:
    VkGraphicsPipelineManager(std::string shader_vert_spv,
                              std::string shader_frag_spv,
                              VkDevice* device);
    ~VkGraphicsPipelineManager();

    void createPipeline(VkRenderPass renderPass,
                        VkExtent2D extent,
                        const std::vector<VkVertexInputBindingDescription>& vertexBindingDescArray,
                        const AttributeDescriptionArray<VkVertexInputAttributeDescription>& vertexAttributeDescArray);

    void cmdBindPipeline(VkCommandBuffer commandBuffer);

    void cmdDraw(VkCommandBuffer commandBuffer, VkExtent2D extent);

private:
    VkShaderManager _shaderManager;
    std::array<VkPipelineShaderStageCreateInfo, 2> _shaderStages;
    std::array<VkDynamicState, 2> _dynamicStates;
};

}

#endif // VKGRAPHICSPIPELINEMANAGER_H
