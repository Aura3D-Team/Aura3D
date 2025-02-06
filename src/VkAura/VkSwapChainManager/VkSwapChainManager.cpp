#include "VkSwapChainManager.h"

#include <VkAura/VkException/VkException.h>
#include <VkAura/VkQueueManager/VkQueueManager.h>

#include <algorithm>
#include <plog/Log.h>

VkSwapChainManager::VkSwapChainManager(VkPhysicalDevice physicalDevice, VkDevice* device, VkSurfaceKHR vkSurface)
    : _swapChain(VK_NULL_HANDLE), _swapChainSupportDetails({}),
    _device(device), _swapChainCreateInfo({}),
    _swapChainImages({}), _choosedSurfaceFormat(),
    _choosedPresentMode(), _choosedExtent()
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

VkSwapchainCreateInfoKHR* VkSwapChainManager::getSwapchainCreateInfoKHR()
{
    return &_swapChainCreateInfo;
}

SwapChainSupportDetails* VkSwapChainManager::getSwapChainSupportDetails()
{
    return &_swapChainSupportDetails;
}

VkSurfaceFormatKHR* VkSwapChainManager::getChoosedSurfaceFormat()
{
    return &_choosedSurfaceFormat;
}

VkPresentModeKHR* VkSwapChainManager::getChoosedPresentMode()
{
    return &_choosedPresentMode;
}

const std::vector<VkImage>& VkSwapChainManager::getSwapChainImages()
{
    return _swapChainImages;
}

VkExtent2D* VkSwapChainManager::getExtent2D()
{
    return &_choosedExtent;
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
    _swapChainCreateInfo.imageArrayLayers = std::min(layerCount, _swapChainSupportDetails.capabilities.maxImageArrayLayers);
    _swapChainCreateInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

    VkDeviceData* vkDeviceCreationData = vkDeviceManager->getDeviceCreationData();
    VkQueueFlags& exclusiveQueueFlag = vkDeviceCreationData->exclusiveQueueFlags;
    std::vector<VkQueueFlags>& concurrentQueueFlags = vkDeviceCreationData->concurrentQueueFlags;

    std::vector<uint32_t> queueFamilyIndices;

    if (exclusiveQueueFlag != 0) {
        queueFamilyIndices.push_back(VkQueueManager::findQueueFamilyIndex(*vkDeviceManager->getPhysicalDevice(), exclusiveQueueFlag));
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
    VK_RESULT_CHECK(result);

    PLOG_INFO << "SwapChain successfuly created!";
    PLOG_DEBUG << "ImageCount: " << imageCount;
    PLOG_DEBUG << "ImageArrayLayers: " << _swapChainCreateInfo.imageArrayLayers;

    vkGetSwapchainImagesKHR(*_device, _swapChain, &imageCount, nullptr);
    _swapChainImages.resize(imageCount);

    vkGetSwapchainImagesKHR(*_device, _swapChain, &imageCount, _swapChainImages.data());
}

void VkSwapChainManager::recreateSwapChain(VkImageViewsManager* vkImageViewsManager,
                                           VkFrameBuffersManager* vkFrameBuffersManager,
                                           GLFWwindow* window,
                                           VkSurfaceKHR surface,
                                           VkDeviceManager* vkDeviceManager,
                                           VkRenderPass renderPass) {
    vkDeviceWaitIdle(*_device);

    vkFrameBuffersManager->clear();
    vkImageViewsManager->clear();

    vkDestroySwapchainKHR(*_device, _swapChain, nullptr);
    PLOG_WARNING << "VkSwapChain deleted in woindow looping";

    createSwapChain(window, surface, vkDeviceManager);
    vkImageViewsManager->createImageViews(VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, getSwapchainCreateInfoKHR()->minImageCount);
    vkFrameBuffersManager->createFrameBuffers(vkImageViewsManager->getImageViews(),
                                              renderPass,
                                              *getExtent2D());
}

const uint32_t VkSwapChainManager::acquireNextImage(VkSemaphore imageSemaphore)
{
    uint32_t imageIndex = UINT32_MAX;
    VkResult result = vkAcquireNextImageKHR(*_device, _swapChain, UINT64_MAX, imageSemaphore, VK_NULL_HANDLE, &imageIndex);

    if (result == VK_ERROR_OUT_OF_DATE_KHR) {
        return imageIndex;
    }

    VK_RESULT_CHECK(result);

    return imageIndex;
}

void VkSwapChainManager::presentBackToSwapChain(VkQueue queue, VkSemaphore* renderFinishedSemaphore, const uint32_t& imageIndex)
{
    VkPresentInfoKHR presentInfo = {};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = renderFinishedSemaphore;
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &_swapChain;
    presentInfo.pImageIndices = &imageIndex;
    presentInfo.pResults = nullptr; // Optional

    vkQueuePresentKHR(queue, &presentInfo);
}

void VkSwapChainManager::cmdPipelineBarrier(VkCommandBuffer commandBuffer, const uint32_t& imageIndex)
{
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = _swapChainImages[imageIndex];
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    barrier.dstAccessMask = 0;

    vkCmdPipelineBarrier(
        commandBuffer,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, // srcStageMask
        VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,            // dstStageMask
        0,
        0, nullptr,
        0, nullptr,
        1, &barrier
        );
}

void VkSwapChainManager::_initSwapChainSupportDetails(VkPhysicalDevice physicalDevice, VkSurfaceKHR vkSurface)
{
    VkResult result = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice, vkSurface, &_swapChainSupportDetails.capabilities);
    VK_RESULT_CHECK(result);

    uint32_t formatCount = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, vkSurface, &formatCount, nullptr);

    if (formatCount != 0) {
        _swapChainSupportDetails.formats.resize(formatCount);
        result = vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, vkSurface, &formatCount, _swapChainSupportDetails.formats.data());
        VK_RESULT_CHECK(result);
    }

    uint32_t presentModeCount = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, vkSurface, &presentModeCount, nullptr);

    if (presentModeCount != 0) {
        _swapChainSupportDetails.presentModes.resize(presentModeCount);
        result = vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, vkSurface, &presentModeCount, _swapChainSupportDetails.presentModes.data());
        VK_RESULT_CHECK(result);
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
