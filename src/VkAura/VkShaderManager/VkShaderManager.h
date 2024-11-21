#ifndef VKSHADERMANAGER_H
#define VKSHADERMANAGER_H

#pragma once

#include <vulkan/vulkan.h>
#include <vector>

#include "Utils/ShaderSpirvExtractor.h"

class VkShaderManager
{
public:
    VkShaderManager(VkDevice* device);
    ~VkShaderManager();

    void _createVertShaderModule(const std::string& filename);
    void _createFragShaderModule(const std::string& filename);

    VkShaderModule& getVertShaderModule();
    VkShaderModule& getFragShaderModule();

private:
    VkDevice* _device;

    VkShaderModule _vertShaderModule;
    VkShaderModule _fragShaderModule;

    ShaderSpirvExtractor _shaderFileExtractor;

    VkShaderModule _createShaderModule(VkDevice device, const std::vector<char>& code);
};

#endif // VKSHADERMANAGER_H
