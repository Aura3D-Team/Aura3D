#include "VkFrameBuffersManager.h"

#include <aura.hpp>
#include "AuraException/AuraException.h"

#include "AuraLogger/AuraLogger.h"

namespace aura3d {

VkFrameBuffersManager::VkFrameBuffersManager(VkHostAllocator* vkHostAllocator, VkDevice* device) :
    vkHostAllocator(vkHostAllocator), _device(device)
{
    // EMpty
}

VkFrameBuffersManager::~VkFrameBuffersManager()
{
    cleanup();

    _device = nullptr;

    AURA_DEBUG << "FrameBuffers destroyed.";
}

void VkFrameBuffersManager::createFrameBuffers(const std::vector<VkImageView>& imageViews,
                                               VkRenderPass renderPass,
                                               VkExtent2D frameExtent)
{
    _framebuffers.resize(imageViews.size());

    auto vkCallbacks = vkHostAllocator->getCallbacks();

    for (size_t i = 0; i < imageViews.size(); i++)
    {
        VkFramebufferCreateInfo framebufferInfo = {};
        framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebufferInfo.renderPass = renderPass;
        framebufferInfo.attachmentCount = 1;
        framebufferInfo.pAttachments = &imageViews[i];
        framebufferInfo.width = frameExtent.width;
        framebufferInfo.height = frameExtent.height;
        framebufferInfo.layers = 1;

        VK_RESULT_CHECK(vkCreateFramebuffer(*_device, &framebufferInfo, vkCallbacks, &_framebuffers[i]));
    }
}

const std::vector<VkFramebuffer>& VkFrameBuffersManager::getFrameBuffers()
{
    return _framebuffers;
}

void VkFrameBuffersManager::cleanup()
{
    auto vkCallbacks = vkHostAllocator->getCallbacks();
    for (auto& framebuffer : _framebuffers) {
        vkDestroyFramebuffer(*_device, framebuffer, vkCallbacks);
    }

    _framebuffers.clear();
}

}
