#include "VkSurfaceManager.h"

#include <aura.hpp>
#include "AuraException/AuraException.h"

#include <plog/Log.h>

namespace aura3d {

VkSurfaceManager::VkSurfaceManager(VkInstance* vkInstance, GLFWwindow* window)
    : _vkInstance(vkInstance)
{
    VkResult result = glfwCreateWindowSurface(*_vkInstance, window, allocationCallbacks, &_vkSurface);
    VK_RESULT_CHECK(result);
}

VkSurfaceManager::VkSurfaceManager(VkInstance* vkInstance, SDL_Window* window)
    : _vkInstance(vkInstance)
{
    if (!SDL_Vulkan_CreateSurface(window, *_vkInstance, &_vkSurface)) {
        throw AuraException("Fail to create SDL Window surface! SDL Error: " + std::string(SDL_GetError()));
    }
}

VkSurfaceManager::~VkSurfaceManager() {
    if (_vkSurface != VK_NULL_HANDLE) {
        vkDestroySurfaceKHR(*_vkInstance, _vkSurface, allocationCallbacks);
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
