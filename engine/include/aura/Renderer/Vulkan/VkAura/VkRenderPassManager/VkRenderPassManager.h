#ifndef VKRENDERPASSMANAGER_H
#define VKRENDERPASSMANAGER_H

#pragma once

#include <vulkan/vulkan.h>

namespace aura3d {
namespace vk {

class VkRenderPassManager
{
public:
    VkRenderPassManager(VkDevice* device);
    ~VkRenderPassManager();

    /**
     * @param sampleCount  MSAA sample count for the color/depth attachments.
     *                     VK_SAMPLE_COUNT_1_BIT (default) is the plain,
     *                     non-multisampled path: the swapchain image is
     *                     attachment 0 and is presented directly. Any higher
     *                     count adds a transient multisampled color attachment
     *                     at index 0 and moves the presented swapchain image to
     *                     a resolve attachment, written by the fixed-function
     *                     resolve at the end of the subpass.
     */
    void createRenderPass(VkFormat swapchainImageFormat,
                          bool enableDepth = false,
                          VkFormat depthFormat = VK_FORMAT_D32_SFLOAT,
                          VkSampleCountFlagBits sampleCount = VK_SAMPLE_COUNT_1_BIT);

    /**
     * @brief Begins the render pass on @p commandBuffer.
     *
     * @param useSecondaryCommandBuffers Selects how the subpass' commands will
     *        be supplied. When true, every draw must come from a secondary
     *        command buffer replayed with vkCmdExecuteCommands and recording a
     *        draw directly into @p commandBuffer becomes invalid -- Vulkan
     *        offers no mixed mode within one subpass instance.
     */
    void beginRenderPass(VkCommandBuffer commandBuffer,
                         VkFramebuffer framebuffer,
                         VkExtent2D swapChainExtent,
                         const VkClearValue* clearColorValue = nullptr,
                         bool useSecondaryCommandBuffers = false);

    bool hasDepth() const { return _hasDepth; }

    static void endRenderPass(VkCommandBuffer commandBuffer);

    VkRenderPass* getRenderPass();

    void cleanup();

private:
    VkDevice* _device;

    VkRenderPass _renderPass = VK_NULL_HANDLE;
    bool _hasDepth = false;
};

}
}

#endif // VKRENDERPASSMANAGER_H
