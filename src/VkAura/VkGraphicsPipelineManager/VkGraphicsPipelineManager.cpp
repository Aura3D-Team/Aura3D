#include "VkGraphicsPipelineManager.h"

VkGraphicsPipelineManager::VkGraphicsPipelineManager(VkDevice* device)
    : _shaderManager(VkShaderManager(device)),
    _dynamicStates({ VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR})
{
    _shaderManager.createVertShaderModule(CMAKE_SOURCE_DIR "/shaders/vert/test_shader2d_vert.spv");
    _shaderManager.createFragShaderModule(CMAKE_SOURCE_DIR "/shaders/frag/test_shader2d_frag.spv");

    VkPipelineShaderStageCreateInfo vertShaderStageInfo;
    vertShaderStageInfo.pNext = nullptr;
    vertShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertShaderStageInfo.module = _shaderManager.getVertShaderModule();
    vertShaderStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo fragShaderStageInfo{};
    fragShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    fragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragShaderStageInfo.module = _shaderManager.getFragShaderModule();
    fragShaderStageInfo.pName = "main";

    _shaderStages = { vertShaderStageInfo, fragShaderStageInfo };

    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = static_cast<uint32_t>(SIZE);
    dynamicState.pDynamicStates = _dynamicStates.data();
}

void VkGraphicsPipelineManager::_createPipeline()
{
    // Set up pipeline layout, render pass, etc., and create the pipeline
}

VkGraphicsPipelineManager::~VkGraphicsPipelineManager()
{
    if (_graphicsPipeline)
    {
        vkDestroyPipeline(*_device, _graphicsPipeline, nullptr);
    }
    if (_pipelineLayout)
    {
        vkDestroyPipelineLayout(*_device, _pipelineLayout, nullptr);
    }
    _device = nullptr;
}

