#include "VkShaderManager.h"

#include "VkAura/VkException/VkException.h"

VkShaderManager::VkShaderManager(VkDevice* device) :
    _vertShaderModule(VK_NULL_HANDLE), _fragShaderModule(VK_NULL_HANDLE),
    _shaderFileExtractor(ShaderSpirvExtractor()), _device(device)
{
    // Empty
}

VkShaderManager::~VkShaderManager()
{
    _device = nullptr;
}

void VkShaderManager::_createVertShaderModule(const std::string& filename)
{
    _shaderFileExtractor.readVertFile(filename);
    _vertShaderModule = _createShaderModule(*_device, _shaderFileExtractor.getVertByteCode());
}

void VkShaderManager::_createFragShaderModule(const std::string& filename)
{
    _shaderFileExtractor.readFragFile(filename);
    _fragShaderModule = _createShaderModule(*_device, _shaderFileExtractor.getFragByteCode());
}

VkShaderModule& VkShaderManager::getVertShaderModule()
{
    return _vertShaderModule;
}

VkShaderModule& VkShaderManager::getFragShaderModule()
{
    return _fragShaderModule;
}

VkShaderModule VkShaderManager::_createShaderModule(VkDevice device, const std::vector<char>& code)
{
    VkShaderModuleCreateInfo createInfo{};
    createInfo.pNext = nullptr;
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = code.size();
    createInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());

    VkShaderModule shaderModule;
    VkResult result = vkCreateShaderModule(device, &createInfo, nullptr, &shaderModule);
    if (result != VK_SUCCESS) {
        throw VkException(result);
    }

    return shaderModule;
}
