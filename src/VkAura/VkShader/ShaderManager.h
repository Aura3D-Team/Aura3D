#ifndef SHADERMANAGER_H
#define SHADERMANAGER_H

#pragma once

#include <vulkan/vulkan.h>
#include <vector>

#include "ShaderSpirvExtractor.h"
#include <VkAura/VkMemory/VkHostAllocator/VkHostAllocator.h>

namespace aura3d {
namespace vk {

class VkShaderManager
{
public:
    VkShaderManager(VkHostAllocator* vkHostAllocator, VkDevice* device);

    ~VkShaderManager();

    void createVertShaderModule(const std::string& filename);
    void createFragShaderModule(const std::string& filename);

    void reset();

    VkShaderModule& getVertShaderModule();
    VkShaderModule& getFragShaderModule();

private:
    VkHostAllocator* vkHostAllocator;
    VkDevice* _device;

    VkShaderModule _vertShaderModule;
    VkShaderModule _fragShaderModule;

    ShaderSpirvExtractor _shaderFileExtractor;

    VkShaderModule _createShaderModule(VkDevice device, const std::vector<char>& code);
};

}
}

#endif // VKSHADERMANAGER_H
