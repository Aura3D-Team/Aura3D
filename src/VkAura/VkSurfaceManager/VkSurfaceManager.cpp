#include "VkSurfaceManager.h"

#include <aura.hpp>
#include "AuraException/AuraException.h"

#include <ink/ink.hpp>

namespace aura3d {

VkSurfaceManager::VkSurfaceManager(VkHostAllocator* vkHostAllocator, VkInstance* vkInstance, GLFWwindow* window)
    : vkHostAllocator(vkHostAllocator), _vkInstance(vkInstance)
{
    VkResult result = glfwCreateWindowSurface(*_vkInstance, window, nullptr, &_vkSurface);
    VK_RESULT_CHECK(result);
}

VkSurfaceManager::VkSurfaceManager(VkHostAllocator* vkHostAllocator, VkInstance* vkInstance, SDL_Window* window)
    : vkHostAllocator(vkHostAllocator), _vkInstance(vkInstance)
{
    if (!SDL_Vulkan_CreateSurface(window, *_vkInstance, &_vkSurface)) {
        throw AuraException("Fail to create SDL Window surface! SDL Error: " + std::string(SDL_GetError()));
    }
}

VkSurfaceManager::~VkSurfaceManager() {
    if (_vkSurface != VK_NULL_HANDLE) {
        vkDestroySurfaceKHR(*_vkInstance, _vkSurface, nullptr);
        INK_DEBUG << "VkSurface deleted";
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
