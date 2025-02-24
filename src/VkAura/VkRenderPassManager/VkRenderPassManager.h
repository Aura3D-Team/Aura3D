#ifndef VKRENDERPASSMANAGER_H
#define VKRENDERPASSMANAGER_H

#pragma once

#include <vulkan/vulkan.h>

namespace aura3d {

class VkRenderPassManager
{
public:
    VkRenderPassManager(VkDevice* device);
    ~VkRenderPassManager();

    void createRenderPass(VkFormat swapchainImageFormat);

    void beginRenderPass(VkCommandBuffer commandBuffer,
                         VkFramebuffer framebuffer,
                         VkExtent2D swapChainExtent);

    static void endRenderPass(VkCommandBuffer commandBuffer);

    VkRenderPass* getRenderPass();

private:
    VkDevice* _device;

    VkRenderPass _renderPass;
};

}

#endif // VKRENDERPASSMANAGER_H
