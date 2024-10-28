#include "SwapChainManager.h"
#include "VkException/VkException.h"

SwapChainManager::SwapChainManager(VkPhysicalDevice physicalDevice, VkSurfaceKHR vkSurface, VkDevice* device)
    : _swapChain(VK_NULL_HANDLE), _swapChainSupportDetails({}), _device(device)
{
    VkResult result = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice, vkSurface, &_swapChainSupportDetails.capabilities);
    if (result != VK_SUCCESS) throw VkException(result);

    uint32_t formatCount = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, vkSurface, &formatCount, nullptr);

    if (formatCount > 0) {
        result = vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, vkSurface, &formatCount, _swapChainSupportDetails.formats.data());
        if (result != VK_SUCCESS) throw VkException(result);
    }

    uint32_t presentModeCount = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, vkSurface, &presentModeCount, nullptr);

    if (presentModeCount > 0) {
        result = vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, vkSurface, &presentModeCount, _swapChainSupportDetails.presentModes.data());
        if (result != VK_SUCCESS) throw VkException(result);
    }
}

SwapChainManager::~SwapChainManager()
{
    if (_swapChain != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(*_device, _swapChain, nullptr);
    }
    _device = nullptr;
}

VkSurfaceFormatKHR SwapChainManager::_chooseBestSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& availableFormats)
{

}
