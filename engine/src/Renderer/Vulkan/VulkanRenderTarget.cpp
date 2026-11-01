#include "aura/Renderer/Vulkan/VulkanRenderer.h"

#include <algorithm>
#include <array>

#include "aura/Core/AuraException/AuraException.h"
#include "aura/Core/Profiling/FrameProfiler.h"
#include "aura/aura.h"

namespace aura3d
{
namespace vk
{

VkRenderPass VulkanRenderer::targetRenderPass(VkFormat colour, bool depth)
{
    for (const TargetPass &cached : _targetPasses)
        if (cached.colour == colour && cached.depth == depth)
            return cached.pass;

    const VkRenderPass pass = colour == VK_FORMAT_UNDEFINED ? buildDepthOnlyPass() : buildColourPass(colour, depth);
    const u32 colourCount = colour == VK_FORMAT_UNDEFINED ? 0u : 1u;
    _targetPasses.push_back({colour, depth, pass});

    _vkGraphicsPipelineManager->addVariant(pass, colourCount);
    _vkBatchPipelineManager->addVariant(pass, colourCount);
    _vkInstancedPipelineManager->addVariant(pass, colourCount);
    for (CustomPipeline &custom : _customPipelines)
        custom.manager->addVariant(pass, colourCount);
    return pass;
}

VkRenderPass VulkanRenderer::buildColourPass(VkFormat colour, bool depth)
{
    //! The colour image lives in SHADER_READ_ONLY_OPTIMAL between passes, so it can be sampled any time.
    const std::array<VkAttachmentDescription, 2> attachments{
        VkAttachmentDescription{
            .format = colour,
            .samples = VK_SAMPLE_COUNT_1_BIT,
            .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
            .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
            .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
            .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
            .initialLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            .finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        },
        VkAttachmentDescription{
            .format = _depth.format,
            .samples = VK_SAMPLE_COUNT_1_BIT,
            .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
            .storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
            .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
            .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
            .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            .finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
        }};
    const VkAttachmentReference colourRef{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    const VkAttachmentReference depthRef{1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
    const VkSubpassDescription subpass{
        .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
        .colorAttachmentCount = 1,
        .pColorAttachments = &colourRef,
        .pDepthStencilAttachment = depth ? &depthRef : nullptr,
    };

    /*
     * In: earlier passes sampled the colour image and wrote the depth buffer this pass clears.
     * Out: later passes sample what this one drew, and readPixels() copies it.
     */
    constexpr VkPipelineStageFlags kDepthStages =
        VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    const std::array<VkSubpassDependency, 2> dependencies{
        VkSubpassDependency{
            .srcSubpass = VK_SUBPASS_EXTERNAL,
            .dstSubpass = 0,
            .srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | kDepthStages,
            .dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | kDepthStages,
            .srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
            .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
        },
        VkSubpassDependency{
            .srcSubpass = 0,
            .dstSubpass = VK_SUBPASS_EXTERNAL,
            .srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            .dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT,
            .srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
            .dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_TRANSFER_READ_BIT,
        }};
    const VkRenderPassCreateInfo info{
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
        .attachmentCount = depth ? 2u : 1u,
        .pAttachments = attachments.data(),
        .subpassCount = 1,
        .pSubpasses = &subpass,
        .dependencyCount = static_cast<u32>(dependencies.size()),
        .pDependencies = dependencies.data(),
    };
    VkRenderPass pass = VK_NULL_HANDLE;
    VK_RESULT_CHECK(vkCreateRenderPass(*_vkDeviceManager->getDevice(), &info, nullptr, &pass));
    return pass;
}

VkRenderPass VulkanRenderer::buildDepthOnlyPass()
{
    //! Cleared at the start, then left for any later pass to sample.
    const VkAttachmentDescription attachment{
        .format = _depth.format,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
        .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
    };
    const VkAttachmentReference depthRef{0, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
    const VkSubpassDescription subpass{
        .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
        .pDepthStencilAttachment = &depthRef,
    };
    constexpr VkPipelineStageFlags kDepthStages =
        VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    //! In: earlier passes that sampled this image finish before it is cleared. Out: later passes see what it drew.
    const std::array<VkSubpassDependency, 2> dependencies{
        VkSubpassDependency{
            .srcSubpass = VK_SUBPASS_EXTERNAL,
            .dstSubpass = 0,
            .srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            .dstStageMask = kDepthStages,
            .srcAccessMask = 0,
            .dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT,
        },
        VkSubpassDependency{
            .srcSubpass = 0,
            .dstSubpass = VK_SUBPASS_EXTERNAL,
            .srcStageMask = VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
            .dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            .srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
            .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
        }};
    const VkRenderPassCreateInfo info{
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
        .attachmentCount = 1,
        .pAttachments = &attachment,
        .subpassCount = 1,
        .pSubpasses = &subpass,
        .dependencyCount = static_cast<u32>(dependencies.size()),
        .pDependencies = dependencies.data(),
    };
    VkRenderPass pass = VK_NULL_HANDLE;
    VK_RESULT_CHECK(vkCreateRenderPass(*_vkDeviceManager->getDevice(), &info, nullptr, &pass));
    return pass;
}

RenderTargetHandle VulkanRenderer::createRenderTarget(const RenderTargetDesc &desc)
{
    if (!_isInitialized || desc.width == 0 || desc.height == 0)
    {
        INK_ERROR << "VulkanRenderer: createRenderTarget needs an initialised renderer and a non-empty size";
        return {};
    }
    RenderTarget target;
    target.desc = desc;
    if (!buildRenderTarget(target))
        return {};
    _renderTargets.push_back(target);
    return RenderTargetHandle{static_cast<u32>(_renderTargets.size())};
}

bool VulkanRenderer::resizeRenderTarget(RenderTargetHandle handle, u32 width, u32 height)
{
    if (!isValidHandle(handle) || handle.value() > _renderTargets.size() || width == 0 || height == 0 ||
        _frameState == FrameState::InPass)
        return false;
    RenderTarget &target = _renderTargets[handle.value() - 1];
    if (target.desc.width == width && target.desc.height == height)
        return true;
    //! Earlier frames may still draw into or sample the old images; this one must not have used it yet.
    vkDeviceWaitIdle(*_vkDeviceManager->getDevice());
    _vkTextureManager->flushUploads();
    destroyTargetAttachments(target);
    target.desc.width = width;
    target.desc.height = height;
    return buildRenderTarget(target);
}

bool VulkanRenderer::buildDepthTarget(RenderTarget &target)
{
    const RenderTargetDesc &desc = target.desc;
    //! A resize rebuilds the image in its old slot, so the handle and its bindless index stay.
    const TextureHandle depth{_vkTextureManager->createDepthAttachment(
        desc.width, desc.height, _depth.format, isValidHandle(target.colour) ? target.colour.value() : 0)};
    if (depth.value() == VkTextureManager::kInvalidTextureId)
        return false;
    publishTexture(depth);
    target.extent = {desc.width, desc.height};
    target.colour = depth;
    target.pass = targetRenderPass(VK_FORMAT_UNDEFINED, true);
    VkImageView view = _vkTextureManager->getTexture(depth.value())->view;
    const VkFramebufferCreateInfo framebufferInfo{
        .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
        .renderPass = target.pass,
        .attachmentCount = 1,
        .pAttachments = &view,
        .width = desc.width,
        .height = desc.height,
        .layers = 1,
    };
    VK_RESULT_CHECK(
        vkCreateFramebuffer(*_vkDeviceManager->getDevice(), &framebufferInfo, nullptr, &target.framebuffer));
    return true;
}

bool VulkanRenderer::buildRenderTarget(RenderTarget &target)
{
    const RenderTargetDesc &desc = target.desc;
    if (desc.format == TextureFormat::Depth)
        return buildDepthTarget(target);
    //! A resize rebuilds the colour image in its old slot, so the handle and its bindless index stay.
    const TextureHandle colour{_vkTextureManager->createAttachment(
        desc.width, desc.height, desc.format, isValidHandle(target.colour) ? target.colour.value() : 0)};
    if (colour.value() == VkTextureManager::kInvalidTextureId)
        return false;
    //! Before the pass is begun, so the transition to SHADER_READ_ONLY is submitted ahead of it.
    _vkTextureManager->flushUploads();
    publishTexture(colour);

    VkDevice device = *_vkDeviceManager->getDevice();
    target.extent = {desc.width, desc.height};
    target.colour = colour;
    target.pass = targetRenderPass(_vkTextureManager->getTexture(colour.value())->format, desc.depth);
    std::array<VkImageView, 2> views{_vkTextureManager->getTexture(colour.value())->view, VK_NULL_HANDLE};
    if (desc.depth)
    {
        target.depth = _memoryManager->createImage2D(_depth.format, target.extent, VK_SAMPLE_COUNT_1_BIT,
                                                     VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT);
        const VkImageViewCreateInfo viewInfo{
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image = target.depth.image,
            .viewType = VK_IMAGE_VIEW_TYPE_2D,
            .format = _depth.format,
            .subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1},
        };
        VK_RESULT_CHECK(vkCreateImageView(device, &viewInfo, nullptr, &target.depthView));
        views[1] = target.depthView;
    }
    const VkFramebufferCreateInfo framebufferInfo{
        .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
        .renderPass = target.pass,
        .attachmentCount = desc.depth ? 2u : 1u,
        .pAttachments = views.data(),
        .width = desc.width,
        .height = desc.height,
        .layers = 1,
    };
    VK_RESULT_CHECK(vkCreateFramebuffer(device, &framebufferInfo, nullptr, &target.framebuffer));
    return true;
}

void VulkanRenderer::destroyTargetAttachments(RenderTarget &target) noexcept
{
    VkDevice device = *_vkDeviceManager->getDevice();
    if (target.framebuffer != VK_NULL_HANDLE)
        vkDestroyFramebuffer(device, target.framebuffer, nullptr);
    if (target.depthView != VK_NULL_HANDLE)
        vkDestroyImageView(device, target.depthView, nullptr);
    if (target.depth.image != VK_NULL_HANDLE)
        _memoryManager->destroyImage(target.depth);
    target.framebuffer = VK_NULL_HANDLE;
    target.depthView = VK_NULL_HANDLE;
    target.depth = {};
}

glm::uvec2 VulkanRenderer::targetSize(RenderTargetHandle target) const
{
    return isValidHandle(target) && target.value() <= _renderTargets.size()
               ? glm::uvec2{_renderTargets[target.value() - 1].extent.width,
                            _renderTargets[target.value() - 1].extent.height}
               : glm::uvec2{0};
}

TextureHandle VulkanRenderer::renderTargetTexture(RenderTargetHandle target) const
{
    return isValidHandle(target) && target.value() <= _renderTargets.size() ? _renderTargets[target.value() - 1].colour
                                                                            : TextureHandle{};
}

void VulkanRenderer::beginRenderPass(RenderTargetHandle target)
{
    if (!isValidHandle(target))
        return beginRenderPass();
    AURA_FRAME_SCOPE(FramePhase::BeginPass);

    if (_frameState == FrameState::Idle || target.value() > _renderTargets.size())
        return;
    _frameState = FrameState::Recording;
    if (!_pipelineReady)
        return;
    const RenderTarget &chosen = _renderTargets[target.value() - 1];
    beginPass(chosen.pass, chosen.framebuffer, chosen.extent, chosen.depthView != VK_NULL_HANDLE,
              chosen.desc.format == TextureFormat::Depth);
}

bool VulkanRenderer::readPixels(RenderTargetHandle target, std::span<u8> rgba)
{
    if (!isValidHandle(target) || target.value() > _renderTargets.size() || !_vkTextureManager ||
        _renderTargets[target.value() - 1].desc.format == TextureFormat::Depth)
        return false;
    //! Every frame that drew into the target has to have finished.
    vkDeviceWaitIdle(*_vkDeviceManager->getDevice());
    return _vkTextureManager->readPixels(_renderTargets[target.value() - 1].colour.value(), rgba);
}

void VulkanRenderer::destroyRenderTargets() noexcept
{
    if (!_vkDeviceManager || !_vkDeviceManager->getDevice())
        return;
    VkDevice device = *_vkDeviceManager->getDevice();
    for (RenderTarget &target : _renderTargets)
        destroyTargetAttachments(target);
    _renderTargets.clear();
    for (const TargetPass &cached : _targetPasses)
        vkDestroyRenderPass(device, cached.pass, nullptr);
    _targetPasses.clear();
}

} // namespace vk
} // namespace aura3d
