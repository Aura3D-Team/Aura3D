#include "aura/Renderer/Vulkan/VkAura/VkShader/ShaderManager.h"

#include "aura/aura.h"
#include "aura/Core/AuraException/AuraException.h"

namespace aura3d {
namespace vk {

VkShaderManager::VkShaderManager(VkDevice* device) :
    _device(device), _vertShaderModule(VK_NULL_HANDLE),
    _fragShaderModule(VK_NULL_HANDLE), _shaderFileExtractor(ShaderSpirvExtractor())
{
    // Empty
}

VkShaderManager::~VkShaderManager()
{
    if (_vertShaderModule != VK_NULL_HANDLE
        || _fragShaderModule != VK_NULL_HANDLE)
    {
        reset();
    }
    _device = nullptr;
}

void VkShaderManager::createVertShaderModule(const std::string& filename)
{
    _shaderFileExtractor.readVertFile(filename);
    _vertShaderModule = _createShaderModule(*_device, _shaderFileExtractor.getVertByteCode());
}

void VkShaderManager::createFragShaderModule(const std::string& filename)
{
    _shaderFileExtractor.readFragFile(filename);
    _fragShaderModule = _createShaderModule(*_device, _shaderFileExtractor.getFragByteCode());
}

void VkShaderManager::createVertShaderModuleFromMemory(const unsigned char* data, u32 size)
{
    std::vector<char> code(data, data + size);
    _vertShaderModule = _createShaderModule(*_device, code);
}

void VkShaderManager::createFragShaderModuleFromMemory(const unsigned char* data, u32 size)
{
    std::vector<char> code(data, data + size);
    _fragShaderModule = _createShaderModule(*_device, code);
}


void VkShaderManager::reset()
{
    vkDestroyShaderModule(*_device, _vertShaderModule, nullptr);
    vkDestroyShaderModule(*_device, _fragShaderModule, nullptr);
    _vertShaderModule = VK_NULL_HANDLE;
    _fragShaderModule = VK_NULL_HANDLE;
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
    createInfo.pCode = reinterpret_cast<const u32*>(code.data());

    VkShaderModule shaderModule;
    VkResult result = vkCreateShaderModule(device, &createInfo, nullptr, &shaderModule);
    VK_RESULT_CHECK(result);

    return shaderModule;
}

}
}
