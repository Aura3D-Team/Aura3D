#include "VkDebugger.h"
#include <plog/Log.h>

VkDebugger::VkDebugger(VkInstance* vkInstance)
    : _vkInstance(vkInstance), _debugMessenger(VK_NULL_HANDLE)
{
    // Empty
}

VkDebugger::~VkDebugger()
{
    PFN_vkDestroyDebugUtilsMessengerEXT func = (PFN_vkDestroyDebugUtilsMessengerEXT) vkGetInstanceProcAddr(*_vkInstance, "vkDestroyDebugUtilsMessengerEXT");
    if (func != nullptr && _debugMessenger != VK_NULL_HANDLE) {
        func(*_vkInstance, _debugMessenger, nullptr);
        _debugMessenger = VK_NULL_HANDLE;
        PLOG_DEBUG << "VkDebugger deleted";
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
    switch (messageSeverity) {
        case VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT:
            PLOG_VERBOSE << "[Vulkan] " << pCallbackData->pMessage;
            break;
        case VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT:
            PLOG_INFO << "[Vulkan] " << pCallbackData->pMessage;
            break;
        case VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT:
            PLOG_WARNING << "[Vulkan] " << pCallbackData->pMessage;
            break;
        case VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT:
            PLOG_ERROR << "[Vulkan] " << pCallbackData->pMessage;
            break;
        default:
            PLOG_WARNING << "[Vulkan] Unknown message severity: " << pCallbackData->pMessage;
            break;
    }

    return VK_FALSE;
}

VkDebugUtilsMessengerCreateInfoEXT VkDebugger::setupDebugMessenger()
{
    VkDebugUtilsMessengerCreateInfoEXT debugInfo = {};
    debugInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;

    debugInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
                                 VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT |
                                 VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                                 VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;

    debugInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                             VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                             VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;

    debugInfo.pfnUserCallback = debugCallback;

    return debugInfo;
}

VkResult VkDebugger::createDebugUtilsMessengerEXT(VkDebugUtilsMessengerCreateInfoEXT* debugInfo, VkAllocationCallbacks* pAllocator) {
    PFN_vkCreateDebugUtilsMessengerEXT func = (PFN_vkCreateDebugUtilsMessengerEXT) vkGetInstanceProcAddr(*_vkInstance, "vkCreateDebugUtilsMessengerEXT");
    if (func != nullptr) {
        return func(*_vkInstance, debugInfo, pAllocator, &_debugMessenger);
    } else {
        return VK_ERROR_EXTENSION_NOT_PRESENT;
    }
}

VkDebugUtilsMessengerEXT* VkDebugger::getVkDebugMessenger()
{
    return &_debugMessenger;
}
