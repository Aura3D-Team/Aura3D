#ifndef VKGRAPHICSPIPELINEMANAGER_H
#define VKGRAPHICSPIPELINEMANAGER_H

#pragma once

#include <vulkan/vulkan.h>
#include <array>

#include "VkAura/VkShaderManager/VkShaderManager.h"

#define SIZE 2

class VkGraphicsPipelineManager
{
public:
    VkGraphicsPipelineManager(VkDevice* device);
    ~VkGraphicsPipelineManager();


private:
    VkDevice* _device;
    VkShaderManager _shaderManager;

    std::array<VkPipelineShaderStageCreateInfo, SIZE> _shaderStages;
    std::array<VkDynamicState, SIZE> _dynamicStates;

    VkPipelineLayout _pipelineLayout;
    VkPipeline _graphicsPipeline;


    void _createPipeline();
};

#endif // VKGRAPHICSPIPELINEMANAGER_H
