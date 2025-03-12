#include "VkRenderPassManager.h"

#include "AuraException/AuraException.h"

#include <plog/Log.h>

namespace aura3d {

VkRenderPassManager::VkRenderPassManager(VkDevice* device) :
    _device(device)
{
    // Empty
}

VkRenderPassManager::~VkRenderPassManager()
{
    cleanup();

    _device = nullptr;

    PLOG_DEBUG << "RenderPass Destroyed.";
}

VkRenderPass* VkRenderPassManager::getRenderPass()
{
    return &_renderPass;
}

void VkRenderPassManager::createRenderPass(VkFormat swapchainImageFormat) {
    // Color Attachment
    VkAttachmentDescription colorAttachment = {};
    colorAttachment.format = swapchainImageFormat;
    colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentReference colorAttachmentRef = {};
    colorAttachmentRef.attachment = 0;
    colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    // Subpass
    VkSubpassDescription subpass = {};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorAttachmentRef;

    // Dependencies
    VkSubpassDependency attachmentDependencyBegin = {};
    attachmentDependencyBegin.srcSubpass = VK_SUBPASS_EXTERNAL;
    attachmentDependencyBegin.dstSubpass = 0;
    attachmentDependencyBegin.srcStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
    attachmentDependencyBegin.srcAccessMask = VK_ACCESS_MEMORY_READ_BIT;
    attachmentDependencyBegin.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    attachmentDependencyBegin.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    VkSubpassDependency attachmentDependencyEnd = {};
    attachmentDependencyEnd.srcSubpass = 0;
    attachmentDependencyEnd.dstSubpass = VK_SUBPASS_EXTERNAL;
    attachmentDependencyEnd.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    attachmentDependencyEnd.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    attachmentDependencyEnd.dstStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
    attachmentDependencyEnd.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;

    std::vector<VkSubpassDependency> dependencies = {attachmentDependencyBegin, attachmentDependencyEnd};
    std::vector<VkAttachmentDescription> attachments = {colorAttachment};

    // Create Render Pass
    VkRenderPassCreateInfo renderPassInfo = {};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
    renderPassInfo.pAttachments = attachments.data();
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    renderPassInfo.dependencyCount = static_cast<uint32_t>(dependencies.size());
    renderPassInfo.pDependencies = dependencies.data();

    VK_RESULT_CHECK(vkCreateRenderPass(*_device, &renderPassInfo, nullptr, &_renderPass));
}


void VkRenderPassManager::beginRenderPass(VkCommandBuffer commandBuffer,
                     VkFramebuffer framebuffer,
                     VkExtent2D swapChainExtent)
{
    VkRenderPassBeginInfo renderPassBeginInfo = {};
    renderPassBeginInfo.pNext = nullptr;
    renderPassBeginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassBeginInfo.renderPass = _renderPass;
    renderPassBeginInfo.framebuffer = framebuffer;

    renderPassBeginInfo.renderArea.offset = {0, 0};
    renderPassBeginInfo.renderArea.extent = swapChainExtent;

    VkClearValue clearColor = {};
    clearColor.color = {0.0f, 0.0f, 0.0f, 1.0f};

    renderPassBeginInfo.clearValueCount = 1;
    renderPassBeginInfo.pClearValues = &clearColor;

    vkCmdBeginRenderPass(commandBuffer, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);
}

void VkRenderPassManager::endRenderPass(VkCommandBuffer commandBuffer) {
    vkCmdEndRenderPass(commandBuffer);
}

void VkRenderPassManager::cleanup()
{
    if (_renderPass != VK_NULL_HANDLE) {
        vkDestroyRenderPass(*_device, _renderPass, nullptr);
    }
}

}
