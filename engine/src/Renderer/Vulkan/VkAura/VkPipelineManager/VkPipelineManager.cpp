#include "aura/Renderer/Vulkan/VkAura/VkPipelineManager/VkPipelineManager.h"

#include "aura/aura.h"
#include "aura/Core/AuraException/AuraException.h"

namespace aura3d {
namespace vk {

VkPipelineManager::VkPipelineManager(VkDevice* device)
    : _device(device)
{
    // Empty
}

VkPipelineManager::~VkPipelineManager()
{
    cleanup();
}

void VkPipelineManager::createPipelineLayout()
{
    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 0; // No descriptor sets by default
    pipelineLayoutInfo.pSetLayouts = nullptr;
    pipelineLayoutInfo.pushConstantRangeCount = 0;
    pipelineLayoutInfo.pPushConstantRanges = nullptr;

   VK_RESULT_CHECK(vkCreatePipelineLayout(*_device, &pipelineLayoutInfo, nullptr, &_pipelineLayout));
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
}
