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

    void createFrameBuffers(const std::vector<VkImageView>& imageViews,
                            VkRenderPass renderPass,
                            VkExtent2D frameExtent,
                            VkImageView depthImageView = VK_NULL_HANDLE);

    const std::vector<VkFramebuffer>& getFrameBuffers();

    void cleanup();

private:
    VkDevice* _device;
    std::vector<VkFramebuffer> _framebuffers;
};

}
}

#endif // VKFRAMEBUFFERSMANAGER_H
