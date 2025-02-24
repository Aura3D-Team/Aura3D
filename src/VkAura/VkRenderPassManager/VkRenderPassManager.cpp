#include "VkRenderPassManager.h"

#include "VkAura/VkException/VkException.h"

#include <plog/Log.h>

namespace aura3d {

VkRenderPassManager::VkRenderPassManager(VkDevice* device) :
    _device(device)
{
    // Empty
}

VkRenderPassManager::~VkRenderPassManager()
{
    if (_renderPass != VK_NULL_HANDLE) {
        vkDestroyRenderPass(*_device, _renderPass, nullptr);
    }

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

    // depthAttachmentRef.attachment = 1; // Index of the depth attachment
    // depthAttachmentRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    // // Update render pass creation to include depth attachment
    // std::array<VkAttachmentDescription, 2> attachments = {
    //     _vkRenderPassData.colorAttachment, depthAttachment
    // };

    // Dependencies (Layout transitions)
    VkSubpassDependency dependencyBegin = {};
    dependencyBegin.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependencyBegin.dstSubpass = 0;
    dependencyBegin.srcStageMask = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    dependencyBegin.srcAccessMask = VK_ACCESS_NONE;
    dependencyBegin.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependencyBegin.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    // Transition from rendering to presentation
    VkSubpassDependency dependencyEnd = {};
    dependencyEnd.srcSubpass = 0;
    dependencyEnd.dstSubpass = VK_SUBPASS_EXTERNAL;
    dependencyEnd.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependencyEnd.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    dependencyEnd.dstStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
    dependencyEnd.dstAccessMask = VK_ACCESS_NONE;  // No access required after render pass finishes

    // Transition back from presentation to rendering if needed
    VkSubpassDependency dependencyPresentation = {};
    dependencyPresentation.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependencyPresentation.dstSubpass = 0;
    dependencyPresentation.srcStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
    dependencyPresentation.srcAccessMask = VK_ACCESS_NONE;
    dependencyPresentation.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependencyPresentation.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    std::vector<VkSubpassDependency> dependencies = {dependencyBegin, dependencyEnd, dependencyPresentation};

    // Create Render Pass
    VkRenderPassCreateInfo renderPassInfo = {};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.pNext = nullptr;
    renderPassInfo.flags = 0;
    renderPassInfo.attachmentCount = 1;
    renderPassInfo.pAttachments = &colorAttachment;
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    // renderPassInfo.dependencyCount = 0;
    // renderPassInfo.pDependencies = nullptr;
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

}
