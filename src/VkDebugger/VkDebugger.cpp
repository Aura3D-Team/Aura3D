#include "VkDebugger.h"
#include <iostream>

VkDebugger::VkDebugger(VkInstance* vkInstance)
    : _vkInstance(vkInstance), _debugMessenger(VK_NULL_HANDLE), _debugInfo()
{    
    _debugInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;

    _debugInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
                                 VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT |
                                 VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                                 VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;

    _debugInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                             VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                             VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;

    _debugInfo.pfnUserCallback = debugCallback;
}

VkDebugger::~VkDebugger()
{
    PFN_vkDestroyDebugUtilsMessengerEXT func = (PFN_vkDestroyDebugUtilsMessengerEXT) vkGetInstanceProcAddr(*_vkInstance, "vkDestroyDebugUtilsMessengerEXT");
    if (func != nullptr && _debugMessenger != VK_NULL_HANDLE) {
        func(*_vkInstance, _debugMessenger, nullptr);
        _debugMessenger = VK_NULL_HANDLE;
    }
    _vkInstance = nullptr;
}

VKAPI_ATTR VkBool32 VKAPI_CALL VkDebugger::debugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
    VkDebugUtilsMessageTypeFlagsEXT messageType,
    const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
    void* pUserData
)
{
    std::cerr << "validation layer: " << pCallbackData->pMessage << '\n';

    return VK_FALSE;
}

VkResult VkDebugger::createDebugUtilsMessengerEXT(VkAllocationCallbacks* pAllocator) {
    PFN_vkCreateDebugUtilsMessengerEXT func = (PFN_vkCreateDebugUtilsMessengerEXT) vkGetInstanceProcAddr(*_vkInstance, "vkCreateDebugUtilsMessengerEXT");
    if (func != nullptr) {
        return func(*_vkInstance, &_debugInfo, pAllocator, &_debugMessenger);
    } else {
        return VK_ERROR_EXTENSION_NOT_PRESENT;
    }
}

VkDebugUtilsMessengerEXT* VkDebugger::getVkDebugMessenger()
{
    return &_debugMessenger;
}

VkDebugUtilsMessengerCreateInfoEXT* VkDebugger::getVkDebugInfo()
{
    return &_debugInfo;
}
