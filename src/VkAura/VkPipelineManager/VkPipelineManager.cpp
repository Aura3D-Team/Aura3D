#include "VkPipelineManager.h"

#include <VkAura/VkException/VkException.h>

namespace aura3d {

VkPipelineManager::VkPipelineManager(VkDevice* device)
    : _device(device)
{
    // Empty
}

VkPipelineManager::~VkPipelineManager()
{
    if (_pipeline)
    {
        vkDestroyPipeline(*_device, _pipeline, nullptr);
    }
    if (_pipelineLayout)
    {
        vkDestroyPipelineLayout(*_device, _pipelineLayout, nullptr);
    }
}

void VkPipelineManager::createPipelineLayout()
{
    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 0; // No descriptor sets by default
    pipelineLayoutInfo.pSetLayouts = nullptr;
    pipelineLayoutInfo.pushConstantRangeCount = 0;
    pipelineLayoutInfo.pPushConstantRanges = nullptr;

    VkResult result = vkCreatePipelineLayout(*_device, &pipelineLayoutInfo, nullptr, &_pipelineLayout);
    VK_RESULT_CHECK(result);
}

void VkPipelineManager::cleanup()
{
    if (_pipeline)
    {
        vkDestroyPipeline(*_device, _pipeline, nullptr);
    }
    if (_pipelineLayout)
    {
        vkDestroyPipelineLayout(*_device, _pipelineLayout, nullptr);
    }
}

}
