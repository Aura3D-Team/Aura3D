#include "VkSwapChainManager.h"
#include "VkException/VkException.h"
#include "VkQueueManager/VkQueueManager.h"

#include <algorithm>
#include <plog/Log.h>

VkSwapChainManager::VkSwapChainManager(VkPhysicalDevice physicalDevice, VkDevice* device, VkSurfaceKHR vkSurface)
    : _swapChain(VK_NULL_HANDLE), _swapChainSupportDetails({}),
    _device(device), _swapChainCreateInfo({}),
    _swapChainImages({}), _choosedSurfaceFormat(),
    _choosedPresentMode(), _choosedExtent(), _vkImageViewsManager(nullptr)
{
    _initSwapChainSupportDetails(physicalDevice, vkSurface);
}

VkSwapChainManager::~VkSwapChainManager()
{
    if (_swapChain != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(*_device, _swapChain, nullptr);
        PLOG_DEBUG << "VkSwapChain deleted";
    }
    _device = nullptr;
}

SwapChainSupportDetails* VkSwapChainManager::getSwapChainSupportDetails()
{
    return &_swapChainSupportDetails;
}

void VkSwapChainManager::createSwapChain(GLFWwindow* window, VkSurfaceKHR surface, VkDeviceManager* vkDeviceManager, uint32_t layerCount)
{
    _choosedSurfaceFormat = _chooseSwapSurfaceFormat(_swapChainSupportDetails.formats);
    _choosedPresentMode = _chooseSwapPresentMode(_swapChainSupportDetails.presentModes);
    _choosedExtent = chooseSwapExtent(_swapChainSupportDetails.capabilities, window);

    uint32_t imageCount = _swapChainSupportDetails.capabilities.minImageCount + 1;

    if (_swapChainSupportDetails.capabilities.maxImageCount > 0
        && imageCount > _swapChainSupportDetails.capabilities.maxImageCount) {
        imageCount = _swapChainSupportDetails.capabilities.maxImageCount;
    }

    _swapChainCreateInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    _swapChainCreateInfo.surface = surface;

    _swapChainCreateInfo.minImageCount = imageCount;
    _swapChainCreateInfo.imageFormat = _choosedSurfaceFormat.format;
    _swapChainCreateInfo.imageColorSpace = _choosedSurfaceFormat.colorSpace;
    _swapChainCreateInfo.imageExtent = _choosedExtent;
    _swapChainCreateInfo.imageArrayLayers = std::min(layerCount ,_swapChainSupportDetails.capabilities.maxImageArrayLayers);
    _swapChainCreateInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

    VkDeviceData* vkDeviceCreationData = vkDeviceManager->getDeviceCreationData();
    std::vector<VkQueueFlags>& exclusiveQueueFlags = vkDeviceCreationData->exclusiveQueueFlags;
    std::vector<VkQueueFlags>& concurrentQueueFlags = vkDeviceCreationData->concurrentQueueFlags;

    std::vector<uint32_t> queueFamilyIndices;

    VkQueueFlags exclusiveMergedFlag = 0;
    for (const VkQueueFlags& f : exclusiveQueueFlags) {
        exclusiveMergedFlag |= f;
    }

    if (exclusiveMergedFlag != 0) {
        queueFamilyIndices.push_back(VkQueueManager::findQueueFamilyIndex(*vkDeviceManager->getPhysicalDevice(), exclusiveMergedFlag));
    }

    if (!concurrentQueueFlags.empty()) {
        _swapChainCreateInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;

        for (const uint32_t& concurrentFlag : concurrentQueueFlags) {
            queueFamilyIndices.push_back(VkQueueManager::findQueueFamilyIndex(*vkDeviceManager->getPhysicalDevice(), concurrentFlag));
        }

        _swapChainCreateInfo.queueFamilyIndexCount = static_cast<uint32_t>(queueFamilyIndices.size());
        _swapChainCreateInfo.pQueueFamilyIndices = queueFamilyIndices.data();

        PLOG_DEBUG << "VK_SHARING_MODE_CONCURRENT";
    }
    else {
        _swapChainCreateInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        _swapChainCreateInfo.queueFamilyIndexCount = static_cast<uint32_t>(queueFamilyIndices.size());
        _swapChainCreateInfo.pQueueFamilyIndices = queueFamilyIndices.data();

        PLOG_DEBUG << "VK_SHARING_MODE_EXCLUSIVE";
    }

    _swapChainCreateInfo.preTransform = _swapChainSupportDetails.capabilities.currentTransform;
    _swapChainCreateInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    _swapChainCreateInfo.presentMode = _choosedPresentMode;
    _swapChainCreateInfo.clipped = VK_TRUE;
    _swapChainCreateInfo.oldSwapchain = VK_NULL_HANDLE;

    VkResult result = vkCreateSwapchainKHR(*vkDeviceManager->getDevice(), &_swapChainCreateInfo, nullptr, &_swapChain);
    if (result != VK_SUCCESS) {
        throw VkException(result);
    }

    PLOG_INFO << "SwapChain successfuly created!";
    PLOG_DEBUG << "ImageCount: " << imageCount;
    PLOG_DEBUG << "ImageArrayLayers: " << _swapChainCreateInfo.imageArrayLayers;

    vkGetSwapchainImagesKHR(*_device, _swapChain, &imageCount, nullptr);
    _swapChainImages.resize(imageCount);

    vkGetSwapchainImagesKHR(*_device, _swapChain, &imageCount, _swapChainImages.data());

    _vkImageViewsManager = std::make_unique<VkImageViewsManager>(*_device, _swapChainImages, _choosedSurfaceFormat.format);
    _vkImageViewsManager->createImageViews(VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, layerCount);
}

void VkSwapChainManager::_initSwapChainSupportDetails(VkPhysicalDevice physicalDevice, VkSurfaceKHR vkSurface)
{
    VkResult result = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice, vkSurface, &_swapChainSupportDetails.capabilities);
    if (result != VK_SUCCESS) throw VkException(result);

    uint32_t formatCount = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, vkSurface, &formatCount, nullptr);

    if (formatCount != 0) {
        _swapChainSupportDetails.formats.resize(formatCount);
        result = vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, vkSurface, &formatCount, _swapChainSupportDetails.formats.data());
        if (result != VK_SUCCESS) throw VkException(result);
    }

    uint32_t presentModeCount = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, vkSurface, &presentModeCount, nullptr);

    if (presentModeCount != 0) {
        _swapChainSupportDetails.presentModes.resize(presentModeCount);
        result = vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, vkSurface, &presentModeCount, _swapChainSupportDetails.presentModes.data());
        if (result != VK_SUCCESS) throw VkException(result);
    }
}

VkExtent2D VkSwapChainManager::chooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities, GLFWwindow* window)
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

VkSurfaceFormatKHR VkSwapChainManager::_chooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& availableFormats,
                                                              const VkFormat vkFormat, const VkColorSpaceKHR vkColorSpace)
{
    if (availableFormats.empty()) {
        throw VkException("No available surface formats found.");
    }

    for (const VkSurfaceFormatKHR& format : availableFormats) {
        if (format.format == vkFormat && format.colorSpace == vkColorSpace) {
            return format;
        }
    }

    return availableFormats[0];
}

VkPresentModeKHR VkSwapChainManager::_chooseSwapPresentMode(const std::vector<VkPresentModeKHR>& availablePresentModes, const VkPresentModeKHR vkPresentMode)
{
    if (availablePresentModes.empty()) {
        throw VkException("No available present modes found.");
    }

    for (const VkPresentModeKHR& presentMode : availablePresentModes) {
        if (presentMode == vkPresentMode) {
            return presentMode;
        }
    }

    return VK_PRESENT_MODE_FIFO_KHR;
}
