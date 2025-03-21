#include "VkInstanceManager.h"
#include <set>
#include <ink/ink.hpp>

#include <aura.hpp>
#include "AuraException/AuraException.h"

namespace aura3d {

VkInstanceManager::VkInstanceManager(VkHostAllocator* vkHostAllocator,
                                     VkInstanceData vkInstanceData,
                                     bool enableValidationLayers)
    : vkHosAllocator(vkHostAllocator),
    _appInfo({}), _instanceInfo({}),
    _vkInstance(VK_NULL_HANDLE), _vkInstanceExtensions(std::move(vkInstanceData.vkInstanceExtensions)),
    _vkValidationLayers(std::move(vkInstanceData.vkValidationLayers)), _vkDebugger(nullptr)
{
    initializeAppInfo(vkInstanceData);
    initializeInstanceInfo();

    VkResult vkDebuggerPreparationResult = prepareVkDebugger(enableValidationLayers);

    validateInstanceExtensions();
    validateValidationLayers();

    VK_RESULT_CHECK(vkCreateInstance(&_instanceInfo, vkHosAllocator->getCallbacks(), &_vkInstance));

    if (vkDebuggerPreparationResult == VK_SUCCESS) {
        createDebuggerInstance(&_debugCreateInfo);
    }
}

VkInstanceManager::~VkInstanceManager()
{
    _vkDebugger.reset();
    if (_vkInstance != VK_NULL_HANDLE) {
        vkDestroyInstance(_vkInstance, vkHosAllocator->getCallbacks());
        _vkInstance = VK_NULL_HANDLE;
        INK_DEBUG << "VkInstance deleted";
    }
}

void VkInstanceManager::initializeAppInfo(const VkInstanceData& vkInstanceData) {
    _appInfo = {};
    _appInfo.pNext = nullptr;
    _appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    _appInfo.pApplicationName = vkInstanceData.appName;
    _appInfo.applicationVersion = VK_MAKE_VERSION(vkInstanceData.appVersion[0], vkInstanceData.appVersion[1], vkInstanceData.appVersion[2]);
    _appInfo.pEngineName = vkInstanceData.engineName;
    _appInfo.engineVersion = VK_MAKE_VERSION(vkInstanceData.appVersion[0], vkInstanceData.appVersion[1], vkInstanceData.appVersion[2]);
    _appInfo.apiVersion = VK_MAKE_VERSION(vkInstanceData.appVersion[0], vkInstanceData.appVersion[1], vkInstanceData.appVersion[2]);
}

void VkInstanceManager::initializeInstanceInfo() {
    _instanceInfo = {};
    _instanceInfo.pNext = nullptr;
    _instanceInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    _instanceInfo.pApplicationInfo = &_appInfo;
}

void VkInstanceManager::validateInstanceExtensions() {
    VkResult result = _checkInstanceExtensionSupport(_vkInstanceExtensions);
    VK_RESULT_CHECK(result);

    _instanceInfo.enabledExtensionCount = static_cast<uint32_t>(_vkInstanceExtensions.size());
    _instanceInfo.ppEnabledExtensionNames = _vkInstanceExtensions.data();
}

void VkInstanceManager::validateValidationLayers() {
    VkResult result = _checkValidationLayerSupport(_vkValidationLayers);
    VK_RESULT_CHECK(result);

    _instanceInfo.enabledLayerCount = static_cast<uint32_t>(_vkValidationLayers.size());
    _instanceInfo.ppEnabledLayerNames = _vkValidationLayers.data();
}

VkResult VkInstanceManager::prepareVkDebugger(const bool enableValidationLayers)
{
    VkResult result = VK_NOT_READY;
    if (enableValidationLayers) {
        result = _checkInstanceExtensionSupport({VK_EXT_DEBUG_UTILS_EXTENSION_NAME});
        if (result == VK_SUCCESS) {
            _vkInstanceExtensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
            _debugCreateInfo = VkDebugger::setupDebugMessenger();
            _instanceInfo.pNext = &_debugCreateInfo;
        } else {
            INK_WARN << "Warning: VK_EXT_DEBUG_UTILS_EXTENSION_NAME is not supported and will not be used.";
        }
    }
    return result;
}

void VkInstanceManager::createDebuggerInstance(VkDebugUtilsMessengerCreateInfoEXT* debugCreateInfo) {
    _vkDebugger = std::make_unique<VkDebugger>(&_vkInstance);
    VkResult result = _vkDebugger->createDebugUtilsMessengerEXT(debugCreateInfo, nullptr);
    VK_RESULT_CHECK(result);
}

VkInstance* VkInstanceManager::getVkInstance()
{
    return &_vkInstance;
}

VkApplicationInfo* VkInstanceManager::getAppInfo()
{
    return &_appInfo;
}

std::unique_ptr<VkDebugger>* VkInstanceManager::getVkDebugger()
{
    return &_vkDebugger;
}

VkResult VkInstanceManager::_checkValidationLayerSupport(const std::vector<const char*>& validationLayers) const
{
    uint32_t layerCount;
    vkEnumerateInstanceLayerProperties(&layerCount, nullptr);

    std::vector<VkLayerProperties> availableLayers(layerCount);
    vkEnumerateInstanceLayerProperties(&layerCount, availableLayers.data());

    uint32_t layerRequiredCount=0;
    for (const char* layerName : validationLayers) {
        for (const VkLayerProperties& layerProperties : availableLayers) {
            if (strcmp(layerName, layerProperties.layerName) == 0) {
                layerRequiredCount++;
            }
        }
    }

    return layerRequiredCount == validationLayers.size() ? VK_SUCCESS : VK_ERROR_LAYER_NOT_PRESENT;
}

VkResult VkInstanceManager::_checkInstanceExtensionSupport(const std::vector<const char*>& exts) const {
    uint32_t extensionCount = 0;

    vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, nullptr);
    std::vector<VkExtensionProperties> availableExtensions(extensionCount);

    vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, availableExtensions.data());
    std::set<std::string> requiredExtensions(exts.begin(), exts.end());

    for (const VkExtensionProperties& extension : availableExtensions) {
        requiredExtensions.erase(extension.extensionName);
    }

    return requiredExtensions.empty() ? VK_SUCCESS : VK_ERROR_EXTENSION_NOT_PRESENT;
}

}
