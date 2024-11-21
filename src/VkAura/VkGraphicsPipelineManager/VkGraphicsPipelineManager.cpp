#include "VkGraphicsPipelineManager.h"

VkGraphicsPipelineManager::VkGraphicsPipelineManager(VkDevice* device) : _shaderManager(VkShaderManager(device))
{
    // Empty
}

VkGraphicsPipelineManager::~VkGraphicsPipelineManager()
{
    // _device = nullptr;
}
