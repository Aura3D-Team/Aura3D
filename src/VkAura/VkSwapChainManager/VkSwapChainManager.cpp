#include "VkSwapChainManager.h"

#include <AuraException/AuraException.h>

#include <algorithm>
#include <ink/ink.hpp>

namespace aura3d {

VkSwapChainManager::VkSwapChainManager(VkHostAllocator* vkHostAllocator, VkPhysicalDevice physicalDevice, VkDevice* device, VkSurfaceKHR vkSurface)
    : vkHostAllocator(vkHostAllocator), _swapChain(VK_NULL_HANDLE), _swapChainSupportDetails({}),
    _device(device), _swapChainCreateInfo({}),
    _swapChainImages({}), _choosedSurfaceFormat(),
    _choosedPresentMode(), _choosedExtent()
{
    initSwapChainSupportDetails(physicalDevice, vkSurface);
}

VkSwapChainManager::~VkSwapChainManager()
{
    cleanup();

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

VkSwapchainKHR* VkSwapChainManager::getSwapChain()
{
    return &_swapChain;
}

VkExtent2D* VkSwapChainManager::getExtent2D()
{
    return &_choosedExtent;
}

void VkSwapChainManager::createSwapChain(WindowAPI* window, VkSurfaceKHR surface, VkDeviceManager* vkDeviceManager, u32 layerCount)
{
    _choosedSurfaceFormat = _chooseSwapSurfaceFormat(_swapChainSupportDetails.formats);
    _choosedPresentMode = _chooseSwapPresentMode(_swapChainSupportDetails.presentModes);
    _choosedExtent = chooseSwapExtent(_swapChainSupportDetails.capabilities, window);

    u32 imageCount = _swapChainSupportDetails.capabilities.minImageCount + 1;

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

    // if (_swapChainSupportDetails.capabilities.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_DST_BIT) {
    //     _swapChainCreateInfo.imageUsage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    // }

    VkDeviceData* vkDeviceCreationData = vkDeviceManager->getDeviceCreationData();
    VkQueueFlags& exclusiveQueueFlag = vkDeviceCreationData->exclusiveQueueFlags;
    std::vector<VkQueueFlags>& concurrentQueueFlags = vkDeviceCreationData->concurrentQueueFlags;

    std::vector<u32> queueFamilyIndices;

    if (exclusiveQueueFlag != 0) {
        queueFamilyIndices.push_back(VkQueueManager::findQueueFamilyIndex(*vkDeviceManager->getPhysicalDevice(), exclusiveQueueFlag));
    }

    if (!concurrentQueueFlags.empty()) {
        _swapChainCreateInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;

        for (const u32& concurrentFlag : concurrentQueueFlags) {
            queueFamilyIndices.push_back(VkQueueManager::findQueueFamilyIndex(*vkDeviceManager->getPhysicalDevice(), concurrentFlag));
        }

        _swapChainCreateInfo.queueFamilyIndexCount = static_cast<u32>(queueFamilyIndices.size());
        _swapChainCreateInfo.pQueueFamilyIndices = queueFamilyIndices.data();

        INK_DEBUG << "VK_SHARING_MODE_CONCURRENT";
    }
    else {
        _swapChainCreateInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        _swapChainCreateInfo.queueFamilyIndexCount = static_cast<u32>(queueFamilyIndices.size());
        _swapChainCreateInfo.pQueueFamilyIndices = queueFamilyIndices.data();

        INK_DEBUG << "VK_SHARING_MODE_EXCLUSIVE";
    }

    _swapChainCreateInfo.preTransform = _swapChainSupportDetails.capabilities.currentTransform;
    _swapChainCreateInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    _swapChainCreateInfo.presentMode = _choosedPresentMode;
    _swapChainCreateInfo.clipped = VK_TRUE;
    _swapChainCreateInfo.oldSwapchain = VK_NULL_HANDLE;

    VK_RESULT_CHECK(vkCreateSwapchainKHR(*vkDeviceManager->getDevice(), &_swapChainCreateInfo, vkHostAllocator->getCallbacks(), &_swapChain));

    INK_INFO << "SwapChain successfuly created!";
    INK_DEBUG << "ImageCount: " << imageCount;
    INK_DEBUG << "ImageArrayLayers: " << _swapChainCreateInfo.imageArrayLayers;

    vkGetSwapchainImagesKHR(*_device, _swapChain, &imageCount, nullptr);
    _swapChainImages.resize(imageCount);

    vkGetSwapchainImagesKHR(*_device, _swapChain, &imageCount, _swapChainImages.data());
}

const u32 VkSwapChainManager::acquireNextImage(VkSemaphore imageSemaphore, WindowFlags* windowFlags)
{
    u32 imageIndex = UINT32_MAX;
    VkResult result = vkAcquireNextImageKHR(*_device, _swapChain, UINT64_MAX, imageSemaphore, VK_NULL_HANDLE, &imageIndex);

    if (result == VK_ERROR_OUT_OF_DATE_KHR ||
        result == VK_SUBOPTIMAL_KHR ||
        windowFlags->resized) {
        return imageIndex;
    };

    VK_RESULT_CHECK(result);

    return imageIndex;
}

void VkSwapChainManager::presentBackToSwapChain(VkQueue queue, VkSemaphore* renderFinishedSemaphore, const u32& imageIndex)
{
    VkPresentInfoKHR presentInfo = {};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = renderFinishedSemaphore;
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &_swapChain;
    presentInfo.pImageIndices = &imageIndex;
    // presentInfo.pResults = nullptr; // Optional

    vkQueuePresentKHR(queue, &presentInfo);
}

void VkSwapChainManager::transitionImageLayout(
    VkCommandBuffer commandBuffer,
    const u32& imageIndex,
    VkImageLayout oldLayout,
    VkImageLayout newLayout,
    VkFixedArray<VkPipelineStageFlags> stages,
    VkFixedArray<VkAccessFlags> accessFlags)

{
    VkImageMemoryBarrier barrier = {};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = _swapChainImages[imageIndex];
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;

    barrier.srcAccessMask = accessFlags[0];
    barrier.dstAccessMask = accessFlags[1];

    // Some trasintions examples:

    // vkSwapChainManager->transitionImageLayout(cmdBuffers[currentFrame],
    //                                           imageIndex,
    //                                           VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
    //                                           VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
    //                                           {VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT},
    //                                           {VK_ACCESS_NONE, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT});


    // vkSwapChainManager->transitionImageLayout(cmdBuffers[currentFrame],
    //                                           imageIndex,
    //                                           VK_IMAGE_LAYOUT_UNDEFINED,
    //                                           VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
    //                                           {VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT},
    //                                           {VK_ACCESS_NONE, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT});

    // Transition image layout for presentation.
    // vkSwapChainManager->transitionImageLayout(cmdBuffers[currentFrame],
    //                                           imageIndex,
    //                                           VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
    //                                           VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
    //                                           {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT},
    //                                           {VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_NONE});

    vkCmdPipelineBarrier(
        commandBuffer,
        stages[0],
        stages[1],
        0, 0, nullptr, 0, nullptr, 1, &barrier
        );
}

void VkSwapChainManager::cleanup()
{
    if (_swapChain != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(*_device, _swapChain, vkHostAllocator->getCallbacks());
        INK_DEBUG << "VkSwapChain deleted";
    }

    _swapChainImages.clear();
}

void VkSwapChainManager::initSwapChainSupportDetails(VkPhysicalDevice physicalDevice, VkSurfaceKHR vkSurface)
{
    VkResult result = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice, vkSurface, &_swapChainSupportDetails.capabilities);
    VK_RESULT_CHECK(result);

    u32 formatCount = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, vkSurface, &formatCount, nullptr);

    if (formatCount != 0) {
        _swapChainSupportDetails.formats.resize(formatCount);
        result = vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, vkSurface, &formatCount, _swapChainSupportDetails.formats.data());
        VK_RESULT_CHECK(result);
    }

    u32 presentModeCount = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, vkSurface, &presentModeCount, nullptr);

    if (presentModeCount != 0) {
        _swapChainSupportDetails.presentModes.resize(presentModeCount);
        result = vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, vkSurface, &presentModeCount, _swapChainSupportDetails.presentModes.data());
        VK_RESULT_CHECK(result);
    }
}

VkExtent2D VkSwapChainManager::chooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities, WindowAPI* window)
{
    int width=0, height=0;
#ifdef SDL_WINDOW_MANAGER
    SDL_GetWindowSize(window, &width, &height);
#else
    glfwGetFramebufferSize(window, &width, &height);
#endif

    VkExtent2D actualExtent = {
        static_cast<u32>(width),
        static_cast<u32>(height)
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
        throw AuraException("No available surface formats found.");
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
        throw AuraException("No available present modes found.");
    }

    for (const VkPresentModeKHR& presentMode : availablePresentModes) {
        if (presentMode == vkPresentMode) {
            return presentMode;
        }
    }

    return VK_PRESENT_MODE_FIFO_KHR;
}

}
