#include "aura/Renderer/Vulkan/VkAura/VkRenderPassManager/VkRenderPassManager.h"

#include <array>

#include "aura/aura.h"
#include "aura/Core/AuraException/AuraException.h"

namespace aura3d {
namespace vk {

VkRenderPassManager::VkRenderPassManager(VkHostAllocator* vkHostAllocator, VkDevice* device) :
    vkHostAllocator(vkHostAllocator), _device(device)
{
    // Empty
}

VkRenderPassManager::~VkRenderPassManager()
{
    cleanup();

    _device = nullptr;

    INK_DEBUG << "RenderPass Destroyed.";
}

VkRenderPass* VkRenderPassManager::getRenderPass()
{
    return &_renderPass;
}

void VkRenderPassManager::createRenderPass(VkFormat swapchainImageFormat,
                                           bool enableDepth,
                                           VkFormat depthFormat)
{
    _hasDepth = enableDepth;

    // Color Attachment (index 0)
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

    std::vector<VkAttachmentDescription> attachments = { colorAttachment };

    // Depth Attachment (index 1, optional)
    VkAttachmentDescription depthAttachment = {};
    VkAttachmentReference depthAttachmentRef = {};

    if (enableDepth) {
        depthAttachment.format = depthFormat;
        depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
        depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        depthAttachmentRef.attachment = 1;
        depthAttachmentRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        attachments.push_back(depthAttachment);
    }

    // Subpass
    VkSubpassDescription subpass = {};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorAttachmentRef;
    subpass.pDepthStencilAttachment = enableDepth ? &depthAttachmentRef : nullptr;

    // Dependencies
    VkSubpassDependency attachmentDependencyBegin = {};
    attachmentDependencyBegin.srcSubpass = VK_SUBPASS_EXTERNAL;
    attachmentDependencyBegin.dstSubpass = 0;
    attachmentDependencyBegin.srcStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
    attachmentDependencyBegin.srcAccessMask = VK_ACCESS_MEMORY_READ_BIT;
    attachmentDependencyBegin.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    attachmentDependencyBegin.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    if (enableDepth) {
        attachmentDependencyBegin.dstStageMask |= VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        attachmentDependencyBegin.dstAccessMask |= VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    }

    VkSubpassDependency attachmentDependencyEnd = {};
    attachmentDependencyEnd.srcSubpass = 0;
    attachmentDependencyEnd.dstSubpass = VK_SUBPASS_EXTERNAL;
    attachmentDependencyEnd.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    attachmentDependencyEnd.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    attachmentDependencyEnd.dstStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
    attachmentDependencyEnd.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;

    if (enableDepth) {
        attachmentDependencyEnd.srcStageMask |= VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        attachmentDependencyEnd.srcAccessMask |= VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    }

    std::vector<VkSubpassDependency> dependencies = { attachmentDependencyBegin, attachmentDependencyEnd };

    VkRenderPassCreateInfo renderPassInfo = {};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = static_cast<u32>(attachments.size());
    renderPassInfo.pAttachments = attachments.data();
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    renderPassInfo.dependencyCount = static_cast<u32>(dependencies.size());
    renderPassInfo.pDependencies = dependencies.data();

    VK_RESULT_CHECK(vkCreateRenderPass(*_device, &renderPassInfo, vkHostAllocator->getCallbacks(), &_renderPass));
}


void VkRenderPassManager::beginRenderPass(VkCommandBuffer commandBuffer,
                                          VkFramebuffer framebuffer,
                                          VkExtent2D swapChainExtent,
                                          const VkClearValue* clearColorValue)
{
    VkRenderPassBeginInfo renderPassBeginInfo = {};
    renderPassBeginInfo.pNext = nullptr;
    renderPassBeginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassBeginInfo.renderPass = _renderPass;
    renderPassBeginInfo.framebuffer = framebuffer;
    renderPassBeginInfo.renderArea.offset = {0, 0};
    renderPassBeginInfo.renderArea.extent = swapChainExtent;

    VkClearValue clearColor = {};
    if (clearColorValue) {
        clearColor = *clearColorValue;
    } else {
        clearColor.color = {{0.0f, 0.0f, 0.0f, 1.0f}};
    }

    if (_hasDepth) {
        std::array<VkClearValue, 2> clearValues = {};
        clearValues[0] = clearColor;
        clearValues[1].depthStencil = {1.0f, 0};
        renderPassBeginInfo.clearValueCount = static_cast<u32>(clearValues.size());
        renderPassBeginInfo.pClearValues = clearValues.data();
        vkCmdBeginRenderPass(commandBuffer, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);
    } else {
        renderPassBeginInfo.clearValueCount = 1;
        renderPassBeginInfo.pClearValues = &clearColor;
        vkCmdBeginRenderPass(commandBuffer, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);
    }
}

void VkRenderPassManager::endRenderPass(VkCommandBuffer commandBuffer) {
    vkCmdEndRenderPass(commandBuffer);
}

void VkRenderPassManager::cleanup()
{
    if (_renderPass != VK_NULL_HANDLE) {
        vkDestroyRenderPass(*_device, _renderPass, vkHostAllocator->getCallbacks());
    }
}

}
}
