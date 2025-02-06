#include "VkRenderPassManager.h"

#include "VkAura/VkException/VkException.h"

#include <plog/Log.h>

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

    // depthAttachmentRef.attachment = 1; // Index of the depth attachment
    // depthAttachmentRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    // // Update render pass creation to include depth attachment
    // std::array<VkAttachmentDescription, 2> attachments = {
    //     _vkRenderPassData.colorAttachment, depthAttachment
    // };

    // Subpass definition
    VkSubpassDescription subpass = {};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorAttachmentRef;

    VkSubpassDependency dependency = {};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.srcAccessMask = 0;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    VkSubpassDependency dependencyEnd = {};
    dependencyEnd.srcSubpass = 0;
    dependencyEnd.dstSubpass = VK_SUBPASS_EXTERNAL;
    dependencyEnd.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependencyEnd.dstStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
    dependencyEnd.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    dependencyEnd.dstAccessMask = 0;

    VkSubpassDependency dependencies[] = {dependency, dependencyEnd};
    // Create the render pass
    VkRenderPassCreateInfo renderPassInfo = {};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.pNext = nullptr;
    renderPassInfo.flags = 0;
    renderPassInfo.attachmentCount = 1;
    renderPassInfo.pAttachments = &colorAttachment;
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    renderPassInfo.dependencyCount = 2;
    renderPassInfo.pDependencies = dependencies;

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
