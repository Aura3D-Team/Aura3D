#include "VkImageViewsManager.h"

#include "aura.hpp"
#include "AuraException/AuraException.h"

namespace aura3d {
namespace vk {

VkImageViewsManager::VkImageViewsManager(VkHostAllocator* vkHostAllocator, VkDevice* device)
    : vkHostAllocator(vkHostAllocator), _device(device)
{
    // empty
}

VkImageViewsManager::~VkImageViewsManager()
{
    cleanup();

    _device = nullptr;

    INK_DEBUG << "ImageViews destroyed.";
}

void VkImageViewsManager::createImageViews(const std::vector<VkImage>& swapChainImages,
                                           VkFormat swapChainImageFormat,
                                           ImageViewData vkImageViewData)
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

        createInfo.subresourceRange.aspectMask = vkImageViewData.aspectMask;
        createInfo.subresourceRange.baseMipLevel = vkImageViewData.baseMipLevel;
        createInfo.subresourceRange.levelCount = vkImageViewData.levelCount;
        createInfo.subresourceRange.baseArrayLayer = vkImageViewData.baseArrayLayer;
        createInfo.subresourceRange.layerCount = vkImageViewData.layerCount;

        VkResult result = vkCreateImageView(*_device, &createInfo, vkHostAllocator->getCallbacks(), &_swapChainImageViews[i]);
        VK_RESULT_CHECK(result);

        INK_DEBUG << "ImageView " << i << " created!";
    }
}

const std::vector<VkImageView>& VkImageViewsManager::getImageViews() const
{
    return _swapChainImageViews;
}

void VkImageViewsManager::cleanup()
{
    for (VkImageView& imageView : _swapChainImageViews) {
        vkDestroyImageView(*_device, imageView, vkHostAllocator->getCallbacks());
    }
    _swapChainImageViews.clear();
}

}
}
