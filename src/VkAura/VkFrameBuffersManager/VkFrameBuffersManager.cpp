#include "VkFrameBuffersManager.h"

#include "AuraException/AuraException.h"

#include <plog/Log.h>

namespace aura3d {

VkFrameBuffersManager::VkFrameBuffersManager(VkDevice* device) :
    _device(device)
{
    // EMpty
}

VkFrameBuffersManager::~VkFrameBuffersManager()
{
    for (auto& framebuffer : _framebuffers) {
        vkDestroyFramebuffer(*_device, framebuffer, nullptr);
    }

    _device = nullptr;

    PLOG_DEBUG << "FrameBuffers destroyed.";
}

void VkFrameBuffersManager::createFrameBuffers(const std::vector<VkImageView>& imageViews,
                                               VkRenderPass renderPass,
                                               VkExtent2D frameExtent)
{
    _framebuffers.resize(imageViews.size());

    for (size_t i = 0; i < imageViews.size(); i++)
    {
        VkImageView attachments[] = {
            imageViews[i]
        };

        VkFramebufferCreateInfo framebufferInfo = {};
        framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebufferInfo.renderPass = renderPass;
        framebufferInfo.attachmentCount = 1;
        framebufferInfo.pAttachments = attachments;
        framebufferInfo.width = frameExtent.width;
        framebufferInfo.height = frameExtent.height;
        framebufferInfo.layers = 1;

        VK_RESULT_CHECK(vkCreateFramebuffer(*_device, &framebufferInfo, nullptr, &_framebuffers[i]));
    }
}

const std::vector<VkFramebuffer>& VkFrameBuffersManager::getFrameBuffers()
{
    return _framebuffers;
}

void VkFrameBuffersManager::cleanup()
{
    for (auto& framebuffer : _framebuffers) {
        vkDestroyFramebuffer(*_device, framebuffer, nullptr);
    }

    _framebuffers.clear();
}

}
