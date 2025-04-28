#ifndef VKRENDERPASSMANAGER_H
#define VKRENDERPASSMANAGER_H

#pragma once

#include <vulkan/vulkan.h>
#include <VkAura/VkMemory/VkHostAllocator/VkHostAllocator.h>

namespace aura3d {

class VkRenderPassManager
{
public:
    VkRenderPassManager(VkHostAllocator* vkHostAllocator, VkDevice* device);
    ~VkRenderPassManager();

    void createRenderPass(VkFormat swapchainImageFormat);

    void beginRenderPass(VkCommandBuffer commandBuffer,
                         VkFramebuffer framebuffer,
                         VkExtent2D swapChainExtent,
                         const VkClearValue* clearColorValue = nullptr);

    static void endRenderPass(VkCommandBuffer commandBuffer);

    VkRenderPass* getRenderPass();

    void cleanup();

private:
    VkHostAllocator* vkHostAllocator;
    VkDevice* _device;

    VkRenderPass _renderPass;
};

}

#endif // VKRENDERPASSMANAGER_H
