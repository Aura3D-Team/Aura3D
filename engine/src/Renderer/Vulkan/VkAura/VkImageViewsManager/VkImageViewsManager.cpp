#include "aura/Renderer/Vulkan/VkAura/VkImageViewsManager/VkImageViewsManager.h"

#include "aura/aura.h"
#include "aura/Core/AuraException/AuraException.h"

namespace aura3d {
namespace vk {

VkImageViewsManager::VkImageViewsManager(VkDevice* device)
    : _device(device)
{}

VkImageViewsManager::~VkImageViewsManager()
{
    cleanup();
    _device = nullptr;
}

// ─────────────────────────────────────────────────────────────────────────────
// Internal helper – creates one VkImageView with explicit parameters
// ─────────────────────────────────────────────────────────────────────────────

VkImageView VkImageViewsManager::createView(VkImage image, VkFormat format,
                                             VkImageAspectFlags aspect,
                                             u32 baseMip,    u32 mipLevels,
                                             u32 baseLayer,  u32 layerCount)
{
    VkImageViewCreateInfo info = {};
    info.sType    = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    info.image    = image;
    info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    info.format   = format;

    info.components = {
        VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY,
        VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY
    };

    info.subresourceRange.aspectMask     = aspect;
    info.subresourceRange.baseMipLevel   = baseMip;
    info.subresourceRange.levelCount     = mipLevels;
    info.subresourceRange.baseArrayLayer = baseLayer;
    info.subresourceRange.layerCount     = layerCount;

    VkImageView view;
    VK_RESULT_CHECK(vkCreateImageView(*_device, &info, nullptr, &view));
    return view;
}

// ─────────────────────────────────────────────────────────────────────────────
// Color image views (one per swapchain image)
// ─────────────────────────────────────────────────────────────────────────────

void VkImageViewsManager::createImageViews(const std::vector<VkImage>& images,
                                           VkFormat format,
                                           ImageViewData viewData)
{
    _colorImageViews.resize(images.size());
    for (size_t i = 0; i < images.size(); ++i) {
        _colorImageViews[i] = createView(
            images[i], format,
            viewData.aspectMask,
            viewData.baseMipLevel, viewData.levelCount,
            viewData.baseArrayLayer, viewData.layerCount);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Depth image view (single, optional)
// ─────────────────────────────────────────────────────────────────────────────

void VkImageViewsManager::createDepthImageView(VkImage image, VkFormat format)
{
    cleanupDepthImageView();
    _depthImageView = createView(image, format,
                                  VK_IMAGE_ASPECT_DEPTH_BIT,
                                  0, 1,
                                  0, 1);
}

void VkImageViewsManager::cleanupDepthImageView()
{
    if (_depthImageView == VK_NULL_HANDLE) return;
    vkDestroyImageView(*_device, _depthImageView, nullptr);
    _depthImageView = VK_NULL_HANDLE;
}

// ─────────────────────────────────────────────────────────────────────────────
// Full cleanup
// ─────────────────────────────────────────────────────────────────────────────

void VkImageViewsManager::cleanup()
{
    cleanupDepthImageView();

    for (VkImageView view : _colorImageViews) {
        vkDestroyImageView(*_device, view, nullptr);
    }
    _colorImageViews.clear();
}

}
}
