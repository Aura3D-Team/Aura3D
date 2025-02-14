#include "VkImageViewsManager.h"

#include "VkAura/VkException/VkException.h"

#include <plog/Log.h>

VkImageViewsManager::VkImageViewsManager(VkDevice* device)
    : _device(device)
{
    // empty
}

VkImageViewsManager::~VkImageViewsManager()
{
    for (VkImageView& imageView : _swapChainImageViews) {
        vkDestroyImageView(*_device, imageView, nullptr);
    }

    _device = nullptr;

    PLOG_DEBUG << "ImageViews destroyed.";
}

void VkImageViewsManager::createImageViews(const std::vector<VkImage>& swapChainImages, VkFormat swapChainImageFormat, VkImageAspectFlags aspectMask, uint32_t baseMipLevel, uint32_t levelCount, uint32_t baseArrayLayer, uint32_t layerCount)
{
    _swapChainImageViews.resize(swapChainImages.size());

    for (size_t i = 0; i < swapChainImages.size(); i++) {
        VkImageViewCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        createInfo.image = swapChainImages[i];
        createInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        createInfo.format = swapChainImageFormat;

        createInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;

        createInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        createInfo.subresourceRange.baseMipLevel = 0;
        createInfo.subresourceRange.levelCount = 1;
        createInfo.subresourceRange.baseArrayLayer = 0;
        createInfo.subresourceRange.layerCount = 1;

        VkResult result = vkCreateImageView(*_device, &createInfo, nullptr, &_swapChainImageViews[i]);
        VK_RESULT_CHECK(result);

        PLOG_DEBUG << "ImageView " << i << " created!";
    }
}

const std::vector<VkImageView>& VkImageViewsManager::getImageViews() const
{
    return _swapChainImageViews;
}

void VkImageViewsManager::clear()
{
    for (VkImageView& imageView : _swapChainImageViews) {
        vkDestroyImageView(*_device, imageView, nullptr);
    }
    _swapChainImageViews.clear();
}
