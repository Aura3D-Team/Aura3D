#include "VkSurfaceManager.h"
#include "AuraException/AuraException.h"

#include <plog/Log.h>

namespace aura3d {

VkSurfaceManager::VkSurfaceManager(VkInstance* vkInstance, GLFWwindow* window)
    : _vkInstance(vkInstance)
{
    VkResult result = glfwCreateWindowSurface(*_vkInstance, window, nullptr, &_vkSurface);
    VK_RESULT_CHECK(result);
}

VkSurfaceManager::VkSurfaceManager(VkInstance* vkInstance, SDL_Window* window)
    : _vkInstance(vkInstance)
{
    if (SDL_Vulkan_CreateSurface(window, *_vkInstance, &_vkSurface))
    {
        PLOG_INFO << "SDL Window Surface created!";
    }
    else
    {
        throw AuraException("Fail to create SDL Window surfce!");
    }
}

VkSurfaceManager::~VkSurfaceManager() {
    if (_vkSurface != VK_NULL_HANDLE) {
        vkDestroySurfaceKHR(*_vkInstance, _vkSurface, nullptr);
        PLOG_DEBUG << "VkSurface deleted";
    }
    _vkInstance = nullptr;
}

VkSurfaceKHR* VkSurfaceManager::getSurface()
{
    return &_vkSurface;
}

VkBool32 VkSurfaceManager::getQueuePhysicalDeviceSurfaceSupport(VkPhysicalDevice physicalDevice, const int familyIndex) {
    VkBool32 presentSupport = false;
    vkGetPhysicalDeviceSurfaceSupportKHR(physicalDevice, familyIndex, _vkSurface, &presentSupport);
    return presentSupport;
}

}
