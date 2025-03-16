#ifndef VKFRAMEBUFFERSMANAGER_H
#define VKFRAMEBUFFERSMANAGER_H

#pragma once

#include <vulkan/vulkan.h>
#include <vector>
#include <VkAura/VkHostAllocator/VkHostAllocator.h>

namespace aura3d {

class VkFrameBuffersManager
{
public:
    VkFrameBuffersManager(VkHostAllocator* vkHostAllocator, VkDevice* device);
    ~VkFrameBuffersManager();

    void createFrameBuffers(const std::vector<VkImageView>& imageViews,
                            VkRenderPass renderPass,
                            VkExtent2D frameExtent);

    const std::vector<VkFramebuffer>& getFrameBuffers();

    void cleanup();

private:
    VkHostAllocator* vkHostAllocator;
    VkDevice* _device;
    std::vector<VkFramebuffer> _framebuffers;
};

}

#endif // VKFRAMEBUFFERSMANAGER_H
