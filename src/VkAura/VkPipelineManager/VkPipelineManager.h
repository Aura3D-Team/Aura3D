#ifndef VKPIPELINEMANAGER_H
#define VKPIPELINEMANAGER_H

#include <vulkan/vulkan.h>
#include <VkAura/VkSwapChainManager/VkSwapChainManager.h>

namespace aura3d {

class VkPipelineManager
{
public:
    explicit VkPipelineManager(VkDevice* device);
    virtual ~VkPipelineManager();

    VkPipeline getPipeline() const { return _pipeline; }
    VkPipelineLayout getPipelineLayout() const { return _pipelineLayout; }

    void cleanup();

protected:
    VkDevice* _device;
    VkPipeline _pipeline = VK_NULL_HANDLE;
    VkPipelineLayout _pipelineLayout = VK_NULL_HANDLE;

    virtual void createPipelineLayout();
};

}

#endif // VKPIPELINEMANAGER_H
