#ifndef VKGRAPHICSPIPELINEMANAGER_H
#define VKGRAPHICSPIPELINEMANAGER_H

#pragma once

#include <vulkan/vulkan.h>
#include "VkAura/VkShaderManager/VkShaderManager.h"

class VkGraphicsPipelineManager
{
public:
    VkGraphicsPipelineManager(VkDevice* device);
    ~VkGraphicsPipelineManager();
private:
    VkShaderManager _shaderManager;
};

#endif // VKGRAPHICSPIPELINEMANAGER_H
