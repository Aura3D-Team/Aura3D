#include "VkInstanceManager.h"
#include "VkException/VkException.h"

VkInstanceManager::VkInstanceManager(VkInstanceData vkInstanceData)
    : _appInfo({}), _instanceInfo({}), _vkInstance(VK_NULL_HANDLE),
    _vkInstanceExtensions(vkInstanceData.vkInstanceExtensions), _vkValidationLayers(vkInstanceData.vkValidationLayers)
{
    // App Info
    _appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    _appInfo.pApplicationName = vkInstanceData.appName;
    _appInfo.applicationVersion = VK_MAKE_VERSION(vkInstanceData.appVersion[0], vkInstanceData.appVersion[1], vkInstanceData.appVersion[2]);
    _appInfo.pEngineName = vkInstanceData.engineName;
    _appInfo.engineVersion = VK_MAKE_VERSION(vkInstanceData.appVersion[0], vkInstanceData.appVersion[1], vkInstanceData.appVersion[2]);
    _appInfo.apiVersion = VK_MAKE_VERSION(vkInstanceData.appVersion[0], vkInstanceData.appVersion[1], vkInstanceData.appVersion[2]);

    // Instance Info config
    _instanceInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    _instanceInfo.pApplicationInfo = &_appInfo;

    // Extensions
    _instanceInfo.enabledExtensionCount = static_cast<uint32_t>(_vkInstanceExtensions.size());
    _instanceInfo.ppEnabledExtensionNames = _vkInstanceExtensions.data();

    // Validation Layers
    _instanceInfo.enabledLayerCount = static_cast<uint32_t>(_vkValidationLayers.size());
    _instanceInfo.ppEnabledLayerNames = _vkValidationLayers.data();

    VkResult vkResult = vkCreateInstance(&_instanceInfo, nullptr, &_vkInstance);
    if (vkResult != VK_SUCCESS) throw VkException(vkResult);
}

VkInstance VkInstanceManager::getVkInstance()
{
    return _vkInstance;
}

VkInstanceManager::~VkInstanceManager()
{
    if (_vkInstance != VK_NULL_HANDLE) {
        vkDestroyInstance(_vkInstance, nullptr);
    }
}
