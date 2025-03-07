#ifndef VKFRAMEBUFFERSMANAGER_H
#define VKFRAMEBUFFERSMANAGER_H

#pragma once

#include <vulkan/vulkan.h>
#include <vector>

namespace aura3d {

class VkFrameBuffersManager
{
public:
    VkFrameBuffersManager(VkDevice* device);
    ~VkFrameBuffersManager();

    void createFrameBuffers(const std::vector<VkImageView>& imageViews,
                            VkRenderPass renderPass,
                            VkExtent2D frameExtent);

    const std::vector<VkFramebuffer>& getFrameBuffers();

    void cleanup();

private:
    VkDevice* _device;
    std::vector<VkFramebuffer> _framebuffers;
};

}

#endif // VKFRAMEBUFFERSMANAGER_H
