#ifndef SHADERMANAGER_H
#define SHADERMANAGER_H

#pragma once

#include <ink/ink.hpp>
#include <vector>
#include <vulkan/vulkan.h>

#include "ShaderSpirvExtractor.h"

namespace aura3d {
namespace vk {

class VkShaderManager
{
public:
    VkShaderManager(VkDevice* device);

    ~VkShaderManager();

    void createVertShaderModule(const std::string& filename);
    void createFragShaderModule(const std::string& filename);
    void createVertShaderModuleFromMemory(const unsigned char* data, u32 size);
    void createFragShaderModuleFromMemory(const unsigned char* data, u32 size);

    void reset();

    VkShaderModule& getVertShaderModule();
    VkShaderModule& getFragShaderModule();

private:
    VkDevice* _device;

    VkShaderModule _vertShaderModule;
    VkShaderModule _fragShaderModule;

    ShaderSpirvExtractor _shaderFileExtractor;

    VkShaderModule _createShaderModule(VkDevice device, const std::vector<char>& code);
};

}
}

#endif // VKSHADERMANAGER_H
