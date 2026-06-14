#include "aura/Renderer/Vulkan/VkAura/VkFrameBuffersManager/VkFrameBuffersManager.h"

#include "aura/aura.h"
#include "aura/Core/AuraException/AuraException.h"

namespace aura3d {
namespace vk {

VkFrameBuffersManager::VkFrameBuffersManager(VkDevice* device) :
    _device(device)
{
    // EMpty
}

VkFrameBuffersManager::~VkFrameBuffersManager()
{
    cleanup();

    _device = nullptr;

    INK_DEBUG << "FrameBuffers destroyed.";
}

void VkFrameBuffersManager::createFrameBuffers(const std::vector<VkImageView>& imageViews,
                                               VkRenderPass renderPass,
                                               VkExtent2D frameExtent,
                                               VkImageView depthImageView)
{
    _framebuffers.resize(imageViews.size());

    const bool hasDepth = (depthImageView != VK_NULL_HANDLE);

    for (size_t i = 0; i < imageViews.size(); i++)
    {
        std::vector<VkImageView> attachments = { imageViews[i] };
        if (hasDepth) {
            attachments.push_back(depthImageView);
        }

        VkFramebufferCreateInfo framebufferInfo = {};
        framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebufferInfo.renderPass = renderPass;
        framebufferInfo.attachmentCount = static_cast<u32>(attachments.size());
        framebufferInfo.pAttachments = attachments.data();
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
}
