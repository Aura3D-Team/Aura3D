#ifndef VKIMAGEVIEWSMANAGER_H
#define VKIMAGEVIEWSMANAGER_H

#pragma once

#include <vulkan/vulkan.h>
#include <vector>

#include "aura/Renderer/Vulkan/VkAura/VkAuraCore.h"

namespace aura3d {
namespace vk {

/**
 * @brief Manages Vulkan image views for swapchain color images and the depth attachment.
 *
 * Owns two categories of views:
 *  - **Color views**: one VkImageView per swapchain image, created via createImageViews().
 *  - **Depth view**: a single VkImageView for the depth buffer, created via
 *    createDepthImageView() and valid only in 3D mode.
 *
 * All views are destroyed on cleanup(). The depth view can also be destroyed
 * independently with cleanupDepthImageView(), which is useful during swapchain
 * recreation without touching the color views.
 */
class VkImageViewsManager {
public:
    /**
     * @brief Constructs the manager.
     * @param device        Logical device used to create and destroy image views.
     */
    VkImageViewsManager(VkDevice* device);

    /**
     * @brief Destroys all owned image views and releases internal state.
     */
    ~VkImageViewsManager();

    /**
     * @brief Creates one color VkImageView per entry in @p images.
     *
     * Any previously created color views are implicitly replaced. The view
     * parameters (aspect mask, mip/layer ranges) are read from @p viewData.
     *
     * @param images    Swapchain images to wrap.
     * @param format    Surface format of the swapchain images.
     * @param viewData  Subresource range and aspect mask configuration.
     */
    void createImageViews(const std::vector<VkImage>& images,
                          VkFormat format,
                          ImageViewData viewData);

    /**
     * @brief Creates a depth VkImageView for the given @p image.
     *
     * If a depth view already exists it is destroyed before creating the new one.
     * The view always uses VK_IMAGE_ASPECT_DEPTH_BIT with a single mip level
     * and a single array layer.
     *
     * @param image  The depth VkImage to create a view for.
     * @param format Depth format (e.g. VK_FORMAT_D32_SFLOAT).
     */
    void createDepthImageView(VkImage image, VkFormat format);

    /**
     * @brief Returns all swapchain color image views.
     * @return Const reference to the internal view vector; valid until the next
     *         createImageViews() or cleanup() call.
     */
    const std::vector<VkImageView>& getImageViews() const { return _colorImageViews; }

    /**
     * @brief Returns the depth image view, or VK_NULL_HANDLE if not created.
     * @return The depth VkImageView.
     */
    VkImageView getDepthImageView() const { return _depthImageView; }

    /**
     * @brief Destroys the depth image view and resets it to VK_NULL_HANDLE.
     *
     * No-op if no depth view was created. Call this before recreating the
     * depth resources during a swapchain resize.
     */
    void cleanupDepthImageView();

    /**
     * @brief Creates the transient multisampled color VkImageView for @p image.
     *
     * If one already exists it is destroyed before creating the new one. Only
     * used when MSAA is active; the swapchain color views above remain the
     * resolve target in that case.
     *
     * @param image  The multisampled color VkImage to create a view for.
     * @param format Swapchain color format (must match).
     */
    void createColorMsaaImageView(VkImage image, VkFormat format);

    /**
     * @brief Returns the MSAA color image view, or VK_NULL_HANDLE if not created.
     */
    VkImageView getColorMsaaImageView() const { return _colorMsaaImageView; }

    /**
     * @brief Destroys the MSAA color image view and resets it to VK_NULL_HANDLE.
     *
     * No-op if none was created. Call this before recreating swapchain
     * resources during a resize.
     */
    void cleanupColorMsaaImageView();

    /**
     * @brief Destroys all color views, the depth view, and the MSAA color view,
     *        and clears internal state.
     */
    void cleanup();

private:
    /**
     * @brief Shared helper that allocates a single VkImageView with explicit parameters.
     *
     * @param image      Source VkImage.
     * @param format     Image format.
     * @param aspect     Aspect flags (color or depth).
     * @param baseMip    Base mip level.
     * @param mipLevels  Number of mip levels.
     * @param baseLayer  Base array layer.
     * @param layerCount Number of array layers.
     * @return Newly created VkImageView.
     */
    VkImageView createView(VkImage image, VkFormat format,
                           VkImageAspectFlags aspect,
                           u32 baseMip, u32 mipLevels,
                           u32 baseLayer, u32 layerCount);

private:
    VkDevice*        _device;

    std::vector<VkImageView> _colorImageViews;
    VkImageView              _depthImageView = VK_NULL_HANDLE;
    VkImageView              _colorMsaaImageView = VK_NULL_HANDLE;
};

}
}

#endif // VKIMAGEVIEWSMANAGER_H
