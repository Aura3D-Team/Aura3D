#include "VkDebugger.h"
#include <iostream>

VkDebugger::VkDebugger(VkInstance* vkInstance) : _vkInstance(vkInstance), _pDebugMessenger(VK_NULL_HANDLE)
{
    // Empty
}

VkDebugger::~VkDebugger()
{
    if (_pDebugMessenger != VK_NULL_HANDLE) {
        vkDestroyDebugUtilsMessengerEXT(*_vkInstance, _pDebugMessenger, nullptr);
        _pDebugMessenger = VK_NULL_HANDLE;
    }
    _vkInstance = nullptr;
}

VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
    VkDebugUtilsMessageTypeFlagsEXT messageType,
    const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
    void* pUserData
)
{
    std::cerr << "validation layer: " << pCallbackData->pMessage << '\n';

    return VK_FALSE;
}

VkDebugUtilsMessengerCreateInfoEXT setupDebugMessenger() {
    VkDebugUtilsMessengerCreateInfoEXT createInfo{};

    createInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;

    createInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
                                 VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT |
                                 VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                                 VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;

    createInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                             VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                             VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;

    createInfo.pfnUserCallback = debugCallback;

    return createInfo;
}

VkResult VkDebugger::CreateDebugUtilsMessengerEXT(
    const VkDebugUtilsMessengerCreateInfoEXT* pCreateInfo,
    VkAllocationCallbacks* pAllocator
)
{
    return vkCreateDebugUtilsMessengerEXT(*_vkInstance, pCreateInfo, nullptr, &_pDebugMessenger);
}

VkDebugUtilsMessengerEXT* VkDebugger::getDebugMessenger()
{
    return &_pDebugMessenger;
}
