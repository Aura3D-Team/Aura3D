#ifndef VKFRAMEBUFFERSMANAGER_H
#define VKFRAMEBUFFERSMANAGER_H

#pragma once

#include <vulkan/vulkan.h>
#include <vector>

namespace aura3d {
namespace vk {

class VkFrameBuffersManager
{
public:
    VkFrameBuffersManager(VkDevice* device);
    ~VkFrameBuffersManager();

    /**
     * @param colorMsaaView  Transient multisampled color view (only when MSAA
     *                       is active). When set, it becomes attachment 0 (the
     *                       actual render target) and each entry in @p
     *                       imageViews becomes the resolve target instead --
     *                       must mirror the attachment order
     *                       VkRenderPassManager::createRenderPass() built the
     *                       render pass with.
     */
    void createFrameBuffers(const std::vector<VkImageView>& imageViews,
                            VkRenderPass renderPass,
                            VkExtent2D frameExtent,
                            VkImageView depthImageView = VK_NULL_HANDLE,
                            VkImageView colorMsaaView = VK_NULL_HANDLE);

    const std::vector<VkFramebuffer>& getFrameBuffers();

    void cleanup();

private:
    VkDevice* _device;
    std::vector<VkFramebuffer> _framebuffers;
};

}
}

#endif // VKFRAMEBUFFERSMANAGER_H
