#ifndef VKDEBUGGER_H
#define VKDEBUGGER_H

#pragma once

#include <vulkan/vulkan.h>

class VkDebugger
{
public:
    VkDebugger(VkInstance* vkInstance);
    ~VkDebugger();

    static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
        VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
        VkDebugUtilsMessageTypeFlagsEXT messageType,
        const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
        void* pUserData
    );

    static VkDebugUtilsMessengerCreateInfoEXT setupDebugMessenger();

    VkResult CreateDebugUtilsMessengerEXT(
        const VkDebugUtilsMessengerCreateInfoEXT* pCreateInfo,
        VkAllocationCallbacks* pAllocator
    );

    VkDebugUtilsMessengerEXT* getDebugMessenger();

private:
    VkInstance* _vkInstance;

    VkDebugUtilsMessengerEXT _pDebugMessenger;
};

#endif // VKDEBUGGER_H
