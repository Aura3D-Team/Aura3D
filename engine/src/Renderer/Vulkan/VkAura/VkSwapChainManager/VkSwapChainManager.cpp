#include "aura/Renderer/Vulkan/VkAura/VkSwapChainManager/VkSwapChainManager.h"

#include "aura/Core/AuraException/AuraException.h"

#include <algorithm>

namespace aura3d {
namespace vk {

VkSwapChainManager::VkSwapChainManager(VkPhysicalDevice physicalDevice, VkDevice* device, VkSurfaceKHR vkSurface)
    : _swapChainSupportDetails({}), _swapChainCreateInfo({}),
    _swapChain(VK_NULL_HANDLE), _device(device),
    _choosedSurfaceFormat(), _choosedPresentMode(),
    _choosedExtent(), _swapChainImages({})
{
    initSwapChainSupportDetails(physicalDevice, vkSurface);
}

VkSwapChainManager::~VkSwapChainManager()
{
    cleanup();
    _device = nullptr;
    INK_INFO << "SwapChain cleaned";
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

void VkSwapChainManager::createSwapChain(wma::WindowDetails* windowDetails, VkSurfaceKHR surface, VkDeviceManager* vkDeviceManager, u32 layerCount)
{
    _choosedSurfaceFormat = _chooseSwapSurfaceFormat(_swapChainSupportDetails.formats);
    _choosedPresentMode = _chooseSwapPresentMode(_swapChainSupportDetails.presentModes, AuraSettings::get()->getVSyncMode());
    _choosedExtent = chooseSwapExtent(_swapChainSupportDetails.capabilities, windowDetails);

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

    // currentTransform reflects the device's physical orientation relative to
    // its natural one (e.g. Android reporting a 90-degree rotation when the
    // app forces landscape on a portrait-native device). Setting preTransform
    // to match it means "I will pre-rotate my own rendered content to
    // compensate
    _swapChainCreateInfo.preTransform =
        (_swapChainSupportDetails.capabilities.supportedTransforms & VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR)
            ? VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR
            : _swapChainSupportDetails.capabilities.currentTransform;
    _swapChainCreateInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    _swapChainCreateInfo.presentMode = _choosedPresentMode;
    _swapChainCreateInfo.clipped = VK_TRUE;
    _swapChainCreateInfo.oldSwapchain = VK_NULL_HANDLE;

    VK_RESULT_CHECK(vkCreateSwapchainKHR(*vkDeviceManager->getDevice(), &_swapChainCreateInfo, nullptr, &_swapChain));

    INK_DEBUG << "SwapChain successfuly created!";
    INK_DEBUG << "ImageCount: " << imageCount;
    INK_DEBUG << "ImageArrayLayers: " << _swapChainCreateInfo.imageArrayLayers;

    vkGetSwapchainImagesKHR(*_device, _swapChain, &imageCount, nullptr);
    _swapChainImages.resize(imageCount);

    vkGetSwapchainImagesKHR(*_device, _swapChain, &imageCount, _swapChainImages.data());
}

u32 VkSwapChainManager::acquireNextImage(VkSemaphore imageSemaphore, wma::WindowFlags* windowFlags)
{
    u32 imageIndex = UINT32_MAX;
    VkResult result = vkAcquireNextImageKHR(*_device, _swapChain, UINT64_MAX, imageSemaphore, VK_NULL_HANDLE, &imageIndex);

    if (result == VK_ERROR_SURFACE_LOST_KHR) 
    {
        windowFlags->surfaceLost = true;
        return imageIndex;
    }

    if (result == VK_ERROR_OUT_OF_DATE_KHR)
    {
        // A stale swapchain never repairs itself: nothing else observes
        // VK_ERROR_OUT_OF_DATE_KHR here, so unless a real
        // SDL_EVENT_WINDOW_RESIZED happens to fire too, this would otherwise
        // skip every frame forever (black screen, no recovery) instead of
        // driving the resized rebuild path in beginFrame().
        windowFlags->resized = true;
        return imageIndex;
    }

    if (result == VK_SUBOPTIMAL_KHR || windowFlags->resized) 
    {
        // VK_SUBOPTIMAL_KHR is advisory, not an error imageIndex is still
        // valid and the frame still renders fine. On this app's landscape-
        // locked Android surfaces this is the *permanent* steady state (see
        // createSwapChain()'s preTransform, which always prefers IDENTITY
        // over the surface's actual currentTransform), so treating it as
        // "needs a rebuild" would force a swapchain rebuild on literally
        // every frame and never let a single one actually present.
        return imageIndex;
    };

    VK_RESULT_CHECK(result);

    return imageIndex;
}

void VkSwapChainManager::presentBackToSwapChain(VkQueue queue, VkSemaphore* renderFinishedSemaphore, const u32& imageIndex, wma::WindowFlags* windowFlags)
{
    VkPresentInfoKHR presentInfo = {};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = renderFinishedSemaphore;
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &_swapChain;
    presentInfo.pImageIndices = &imageIndex;
    // presentInfo.pResults = nullptr; // Optional

    VkResult result = vkQueuePresentKHR(queue, &presentInfo);

    // The Android ANativeWindow backing the surface can be torn down mid-frame
    // when the Activity is backgrounded. Unlike acquireNextImage(), a lost/out
    // -of-date surface caught here was previously dropped on the floor, so the
    // next frame kept issuing Vulkan calls against a dead surface instead of
    // going through a recovery path in beginFrame().
    if (result == VK_ERROR_SURFACE_LOST_KHR)
        windowFlags->surfaceLost = true;
    else if (result == VK_ERROR_OUT_OF_DATE_KHR)
        windowFlags->resized = true;
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
        vkDestroySwapchainKHR(*_device, _swapChain, nullptr);
        // Without this, a second cleanup() call before a successful
        // createSwapChain() rebuild (e.g. recreateSurfaceAndSwapchain()'s
        // retry loop, when initSwapChainSupportDetails() keeps throwing and
        // never gets far enough to recreate the swapchain) destroys the same
        // already-freed handle again a double-free the Scudo allocator
        // aborts on (observed on-device during repeated surface-lost
        // recovery attempts).
        _swapChain = VK_NULL_HANDLE;
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

VkExtent2D VkSwapChainManager::chooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities, wma::WindowDetails* windowDetails)
{
    VkExtent2D actualExtent = {
        static_cast<u32>(windowDetails->width),
        static_cast<u32>(windowDetails->height)
    };

    actualExtent.width = INK_CLAMP(actualExtent.width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width);
    actualExtent.height = INK_CLAMP(actualExtent.height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height);

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

    INK_WARN << "Unavailable vkFormat " << std::to_string(vkFormat) << " , fallback to the first available vkFormat: " << std::to_string(availableFormats[0].format);

    return availableFormats[0];
}

VkPresentModeKHR VkSwapChainManager::_chooseSwapPresentMode(const std::vector<VkPresentModeKHR>& availablePresentModes, const VSyncMode mode)
{
    switch (mode)
    {
        case VSyncMode::AutoVsync:
            // FifoRelaxed, then Fifo (always supported).
            if (std::ranges::contains(availablePresentModes, VK_PRESENT_MODE_FIFO_RELAXED_KHR))
                return VK_PRESENT_MODE_FIFO_RELAXED_KHR;
            return VK_PRESENT_MODE_FIFO_KHR;

        case VSyncMode::AutoNoVsync:
            // Immediate, then Mailbox, then Fifo (always supported).
            if (std::ranges::contains(availablePresentModes, VK_PRESENT_MODE_IMMEDIATE_KHR))
                return VK_PRESENT_MODE_IMMEDIATE_KHR;
            if (std::ranges::contains(availablePresentModes, VK_PRESENT_MODE_MAILBOX_KHR))
                return VK_PRESENT_MODE_MAILBOX_KHR;
            return VK_PRESENT_MODE_FIFO_KHR;

        case VSyncMode::FifoRelaxed:
            if (std::ranges::contains(availablePresentModes, VK_PRESENT_MODE_FIFO_RELAXED_KHR))
                return VK_PRESENT_MODE_FIFO_RELAXED_KHR;
            INK_WARN << "VK_PRESENT_MODE_FIFO_RELAXED_KHR unsupported; falling back to VK_PRESENT_MODE_FIFO_KHR";
            return VK_PRESENT_MODE_FIFO_KHR;

        case VSyncMode::Immediate:
            if (std::ranges::contains(availablePresentModes, VK_PRESENT_MODE_IMMEDIATE_KHR))
                return VK_PRESENT_MODE_IMMEDIATE_KHR;
            INK_WARN << "VK_PRESENT_MODE_IMMEDIATE_KHR unsupported; falling back to VK_PRESENT_MODE_FIFO_KHR";
            return VK_PRESENT_MODE_FIFO_KHR;

        case VSyncMode::Mailbox:
            if (std::ranges::contains(availablePresentModes, VK_PRESENT_MODE_MAILBOX_KHR))
                return VK_PRESENT_MODE_MAILBOX_KHR;
            INK_WARN << "VK_PRESENT_MODE_MAILBOX_KHR unsupported; falling back to VK_PRESENT_MODE_FIFO_KHR";
            return VK_PRESENT_MODE_FIFO_KHR;

        case VSyncMode::Fifo:
            return VK_PRESENT_MODE_FIFO_KHR;
    }

    return VK_PRESENT_MODE_FIFO_KHR;
}

}
}
