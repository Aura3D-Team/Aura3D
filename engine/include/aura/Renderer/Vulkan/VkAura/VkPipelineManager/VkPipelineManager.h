#ifndef VKPIPELINEMANAGER_H
#define VKPIPELINEMANAGER_H

#pragma once

#include "aura/Renderer/Vulkan/VkAura/VkSwapChainManager/VkSwapChainManager.h"
#include <vulkan/vulkan.h>

namespace aura3d
{
namespace vk
{

class VkPipelineManager
{
  public:
    explicit VkPipelineManager(VkDevice *device);
    virtual ~VkPipelineManager();

    VkPipeline getPipeline() const
    {
        return _pipeline;
    }
    VkPipelineLayout getPipelineLayout() const
    {
        return _pipelineLayout;
    }

    void cleanup();

  protected:
    VkDevice *_device;
    VkPipeline _pipeline = VK_NULL_HANDLE;
    VkPipelineLayout _pipelineLayout = VK_NULL_HANDLE;

    virtual void createPipelineLayout();
};

} // namespace vk
} // namespace aura3d

#endif // VKPIPELINEMANAGER_H
