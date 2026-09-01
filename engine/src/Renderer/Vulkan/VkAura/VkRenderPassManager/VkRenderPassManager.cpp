#include "aura/Renderer/Vulkan/VkAura/VkRenderPassManager/VkRenderPassManager.h"

#include <array>
#include <vector>

#include "aura/aura.h"
#include "aura/Core/AuraException/AuraException.h"

namespace aura3d {
namespace vk {

VkRenderPassManager::VkRenderPassManager(VkDevice* device) :
    _device(device)
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
                                           VkFormat depthFormat,
                                           VkSampleCountFlagBits sampleCount)
{
    _hasDepth = enableDepth;
    const bool useMsaa = sampleCount > VK_SAMPLE_COUNT_1_BIT;

    // Color Attachment (index 0) -- the pipeline's actual render target. At 1x
    // this IS the presented swapchain image; at >1x it's a transient
    // multisampled surface resolved into a separate attachment below, so it is
    // never stored or presented directly.
    VkAttachmentDescription colorAttachment = {};
    colorAttachment.format = swapchainImageFormat;
    colorAttachment.samples = sampleCount;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = useMsaa ? VK_ATTACHMENT_STORE_OP_DONT_CARE : VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttachment.finalLayout = useMsaa ? VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL : VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentReference colorAttachmentRef = {};
    colorAttachmentRef.attachment = 0;
    colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    std::vector<VkAttachmentDescription> attachments = { colorAttachment };

    // Depth Attachment (optional)
    VkAttachmentDescription depthAttachment = {};
    VkAttachmentReference depthAttachmentRef = {};

    if (enableDepth) {
        depthAttachment.format = depthFormat;
        // Must match the color attachment's sample count: Vulkan requires every
        // attachment referenced by a subpass to share one sample count.
        depthAttachment.samples = sampleCount;
        depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        depthAttachmentRef.attachment = static_cast<u32>(attachments.size());
        depthAttachmentRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        attachments.push_back(depthAttachment);
    }

    // Resolve Attachment (only with MSAA): the actual presented image, written
    // once per pixel by the fixed-function multisample resolve at the end of
    // the subpass.
    VkAttachmentDescription resolveAttachment = {};
    VkAttachmentReference resolveAttachmentRef = {};

    if (useMsaa) 
    {
        resolveAttachment.format = swapchainImageFormat;
        resolveAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
        resolveAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        resolveAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        resolveAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        resolveAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        resolveAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        resolveAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

        resolveAttachmentRef.attachment = static_cast<u32>(attachments.size());
        resolveAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        attachments.push_back(resolveAttachment);
    }

    // Subpass
    VkSubpassDescription subpass = {};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorAttachmentRef;
    subpass.pDepthStencilAttachment = enableDepth ? &depthAttachmentRef : nullptr;
    subpass.pResolveAttachments = useMsaa ? &resolveAttachmentRef : nullptr;

    /*
     * Dependencies.
     *
     * The colour attachment is a different swapchain image each frame, but the
     * depth buffer is a single image every frame renders into. Once the CPU is
     * allowed to run ahead of the GPU, that makes consecutive frames' render
     * passes overlap on one resource, and the entry dependency has to order
     * this frame's depth clear after the *previous* frame's depth writes.
     *
     * Ordering only against reads (BOTTOM_OF_PIPE + MEMORY_READ, which is what
     * this did) does not do that: the previous frame's last depth write
     * happens in LATE_FRAGMENT_TESTS, and nothing named it. Synchronization
     * validation reports it as
     * "WRITE_AFTER_WRITE: vkCmdBeginRenderPass ... previously written at the
     * end of subpass 0 by the attachment storeOp". Adding a depth-buffer per
     * frame in flight would also fix it, at the cost of a full extra
     * depth/stencil surface; the dependency is free.
     */
    VkSubpassDependency attachmentDependencyBegin = {};
    attachmentDependencyBegin.srcSubpass = VK_SUBPASS_EXTERNAL;
    attachmentDependencyBegin.dstSubpass = 0;
    attachmentDependencyBegin.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    attachmentDependencyBegin.srcAccessMask = 0;
    attachmentDependencyBegin.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    attachmentDependencyBegin.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    if (enableDepth)
    {
        //! Both fragment-test stages on the source side: EARLY is where a
        //! depth clear/attachment load writes, LATE where the store does.
        attachmentDependencyBegin.srcStageMask |= VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT
                                                | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
        attachmentDependencyBegin.srcAccessMask |= VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        attachmentDependencyBegin.dstStageMask |= VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT
                                                | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
        attachmentDependencyBegin.dstAccessMask |= VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT
                                                 | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
    }

    VkSubpassDependency attachmentDependencyEnd = {};
    attachmentDependencyEnd.srcSubpass = 0;
    attachmentDependencyEnd.dstSubpass = VK_SUBPASS_EXTERNAL;
    attachmentDependencyEnd.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    attachmentDependencyEnd.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    attachmentDependencyEnd.dstStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
    attachmentDependencyEnd.dstAccessMask = 0;

    if (enableDepth)
    {
        //! LATE_FRAGMENT_TESTS, not EARLY: the depth storeOp at the end of the
        //! subpass writes there, and that is the write the next frame's pass
        //! must be ordered against.
        attachmentDependencyEnd.srcStageMask |= VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT
                                              | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
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

    VK_RESULT_CHECK(vkCreateRenderPass(*_device, &renderPassInfo, nullptr, &_renderPass));
}


void VkRenderPassManager::beginRenderPass(VkCommandBuffer commandBuffer,
                                          VkFramebuffer framebuffer,
                                          VkExtent2D swapChainExtent,
                                          const VkClearValue* clearColorValue,
                                          bool useSecondaryCommandBuffers)
{
    VkRenderPassBeginInfo renderPassBeginInfo = {};
    renderPassBeginInfo.pNext = nullptr;
    renderPassBeginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassBeginInfo.renderPass = _renderPass;
    renderPassBeginInfo.framebuffer = framebuffer;
    renderPassBeginInfo.renderArea.offset = {0, 0};
    renderPassBeginInfo.renderArea.extent = swapChainExtent;

    VkClearValue clearColor = {};

    if (clearColorValue)
        clearColor = *clearColorValue;
    else
        clearColor.color = {{0.0f, 0.0f, 0.0f, 1.0f}};

    /*
     * All-or-nothing per subpass instance: with SECONDARY_COMMAND_BUFFERS,
     * every command in the subpass must arrive via vkCmdExecuteCommands and a
     * direct vkCmdDraw* on the primary is invalid -- there is no mixed mode.
     */
    const VkSubpassContents contents = useSecondaryCommandBuffers
                                           ? VK_SUBPASS_CONTENTS_SECONDARY_COMMAND_BUFFERS
                                           : VK_SUBPASS_CONTENTS_INLINE;

    if (_hasDepth)
    {
        std::array<VkClearValue, 2> clearValues = {};
        clearValues[0] = clearColor;
        clearValues[1].depthStencil = {1.0f, 0};
        renderPassBeginInfo.clearValueCount = static_cast<u32>(clearValues.size());
        renderPassBeginInfo.pClearValues = clearValues.data();
        vkCmdBeginRenderPass(commandBuffer, &renderPassBeginInfo, contents);
    }
    else
    {
        renderPassBeginInfo.clearValueCount = 1;
        renderPassBeginInfo.pClearValues = &clearColor;
        vkCmdBeginRenderPass(commandBuffer, &renderPassBeginInfo, contents);
    }
}

void VkRenderPassManager::endRenderPass(VkCommandBuffer commandBuffer) {
    vkCmdEndRenderPass(commandBuffer);
}

void VkRenderPassManager::cleanup()
{
    if (_renderPass != VK_NULL_HANDLE) 
    {
        vkDestroyRenderPass(*_device, _renderPass, nullptr);
        // See VkSwapChainManager::cleanup() without nulling this, a
        // second cleanup() before the render pass is recreated (a failed
        // swapchain-recovery retry) double-destroys the same handle.
        _renderPass = VK_NULL_HANDLE;
    }
}

}
}
