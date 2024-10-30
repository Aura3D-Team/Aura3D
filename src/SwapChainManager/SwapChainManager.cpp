#include "SwapChainManager.h"
#include "VkException/VkException.h"
#include <algorithm>

SwapChainManager::SwapChainManager(VkPhysicalDevice physicalDevice, VkSurfaceKHR vkSurface, VkDevice* device)
    : _swapChain(VK_NULL_HANDLE), _swapChainSupportDetails({}), _device(device)
{
    _initSwapChainSupportDetails(physicalDevice, vkSurface);


}

SwapChainManager::~SwapChainManager()
{
    if (_swapChain != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(*_device, _swapChain, nullptr);
    }
    _device = nullptr;
}

void SwapChainManager::_initSwapChainSupportDetails(VkPhysicalDevice physicalDevice, VkSurfaceKHR vkSurface)
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

SwapChainSupportDetails* SwapChainManager::getSwapChainSupportDetails()
{
    return &_swapChainSupportDetails;
}

VkExtent2D SwapChainManager::chooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities, GLFWwindow* window)
{
    if (capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max()) {
        return capabilities.currentExtent;
    }

    int width, height;
    glfwGetFramebufferSize(window, &width, &height);

    VkExtent2D actualExtent = {
        static_cast<uint32_t>(width),
        static_cast<uint32_t>(height)
    };

    actualExtent.width = std::clamp(
        actualExtent.width,
        capabilities.minImageExtent.width,
        capabilities.maxImageExtent.width
    );

    actualExtent.height = std::clamp(
        actualExtent.height,
        capabilities.minImageExtent.height,
        capabilities.maxImageExtent.height
    );

    return actualExtent;
}

VkSurfaceFormatKHR SwapChainManager::_chooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& availableFormats,
                                                              const VkFormat vkFormat, const VkColorSpaceKHR vkColorSpace)
{
    for (const VkSurfaceFormatKHR& format : availableFormats) {
        if (format.format == vkFormat && format.colorSpace == vkColorSpace) {
            return format;
        }
    }

    return availableFormats[0];
}

VkPresentModeKHR SwapChainManager::_chooseSwapPresentMode(const std::vector<VkPresentModeKHR>& availablePresentModes, const VkPresentModeKHR vkPresentMode)
{
    for (const VkPresentModeKHR& presentMode : availablePresentModes) {
        if (presentMode == vkPresentMode) {
            return presentMode;
        }
    }

    return VK_PRESENT_MODE_FIFO_KHR;
}
