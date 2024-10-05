#include "VkInstanceManager/VkInstanceManager.h"

VkInstanceManager::VkInstanceManager()
    : _appInfo({}), _instanceInfo({}), _vkInstance(VK_NULL_HANDLE)
{
    // App Info
    _appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    _appInfo.pApplicationName = "Aura3D";
    _appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    _appInfo.pEngineName = "Aura3DEngine";
    _appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    _appInfo.apiVersion = VK_MAKE_VERSION(1, 0, 0);

    // Instance Info config
    _instanceInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    _instanceInfo.pApplicationInfo = &_appInfo;
    // instanceInfo.enabledExtensionCount =
    // instanceInfo.ppEnabledExtensionNames =
    // instanceInfo.enabledLayerCount =
    // instanceInfo.ppEnabledLayerNames =

    VkResult result = vkCreateInstance(&_instanceInfo, nullptr, &_vkInstance);
    if (result != VK_SUCCESS) {
        throw std::runtime_error("Failed to create Vulkan instance!");
    }
}

VkInstanceManager::~VkInstanceManager()
{
    if (_vkInstance != VK_NULL_HANDLE) {
        vkDestroyInstance(_vkInstance, nullptr);
    }
}
