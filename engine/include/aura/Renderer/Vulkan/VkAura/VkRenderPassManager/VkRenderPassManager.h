#ifndef VKRENDERPASSMANAGER_H
#define VKRENDERPASSMANAGER_H

#pragma once

#include <vulkan/vulkan.h>
#include "aura/Renderer/Vulkan/VkAura/VkMemory/VkHostAllocator/VkHostAllocator.h"

namespace aura3d {
namespace vk {

class VkRenderPassManager
{
public:
    VkRenderPassManager(VkHostAllocator* vkHostAllocator, VkDevice* device);
    ~VkRenderPassManager();

    void createRenderPass(VkFormat swapchainImageFormat,
                          bool enableDepth = false,
                          VkFormat depthFormat = VK_FORMAT_D32_SFLOAT);

    void beginRenderPass(VkCommandBuffer commandBuffer,
                         VkFramebuffer framebuffer,
                         VkExtent2D swapChainExtent,
                         const VkClearValue* clearColorValue = nullptr);

    bool hasDepth() const { return _hasDepth; }

    static void endRenderPass(VkCommandBuffer commandBuffer);

    VkRenderPass* getRenderPass();

    void cleanup();

private:
    VkHostAllocator* vkHostAllocator;
    VkDevice* _device;

    VkRenderPass _renderPass = VK_NULL_HANDLE;
    bool _hasDepth = false;
};

}
}

#endif // VKRENDERPASSMANAGER_H
