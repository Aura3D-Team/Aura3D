#include "aura/Renderer/Vulkan/VkAura/VkGraphicsPipelineManager/VkGraphicsPipelineManager.h"

#include "aura/aura.h"
#include "aura/Core/AuraException/AuraException.h"

namespace aura3d {
namespace vk {

VkGraphicsPipelineManager::VkGraphicsPipelineManager(std::string shader_vert_spv,
                                                     std::string shader_frag_spv,
                                                     VkDevice* device)
    : VkPipelineManager(device), _shaderManager(VkShaderManager(device))
{
    _shaderManager.createVertShaderModule(shader_vert_spv);
    _shaderManager.createFragShaderModule(shader_frag_spv);
_init();
}

VkGraphicsPipelineManager::VkGraphicsPipelineManager(const unsigned char* vertData, u32 vertSize,
                                                     const unsigned char* fragData, u32 fragSize,
                                                     VkDevice* device)
    : VkPipelineManager(device), _shaderManager(VkShaderManager(device))
{
    _shaderManager.createVertShaderModuleFromMemory(vertData, vertSize);
    _shaderManager.createFragShaderModuleFromMemory(fragData, fragSize);

    _init();
}

void VkGraphicsPipelineManager::_init()
{
    VkPipelineShaderStageCreateInfo vertShaderStageInfo{};
    vertShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertShaderStageInfo.module = _shaderManager.getVertShaderModule();
    vertShaderStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo fragShaderStageInfo{};
    fragShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    fragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragShaderStageInfo.module = _shaderManager.getFragShaderModule();
    fragShaderStageInfo.pName = "main";

    _shaderStages = { vertShaderStageInfo, fragShaderStageInfo };

    _dynamicStates = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };

    DescriptorBindingInfo uboBinding;
    uboBinding.binding = 0;
    uboBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    uboBinding.descriptorCount = 1;
    uboBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    addDescriptorBinding(0, uboBinding);

    DescriptorBindingInfo samplerBinding;
    samplerBinding.binding = 0;
    samplerBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    samplerBinding.descriptorCount = 1;
    samplerBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    addDescriptorBinding(1, samplerBinding);

    // set 2: directional light, read by the fragment stage.
    DescriptorBindingInfo lightBinding;
    lightBinding.binding = 0;
    lightBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    lightBinding.descriptorCount = 1;
    lightBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    addDescriptorBinding(2, lightBinding);

    /*
     * Per-draw transform. set 0 carries view/proj once per frame, while the
     * model and normal matrices arrive as push constants immediately before each
     * draw, which is what allows many objects with distinct transforms inside a
     * single render pass. Two mat4s is exactly the 128 bytes Vulkan guarantees.
     */
    setPushConstantRange(VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(PushConstantBlock));
}

VkGraphicsPipelineManager::~VkGraphicsPipelineManager()
{
    // Clean up descriptor set layouts
    for (auto& pair : _descriptorSetLayouts) {
        vkDestroyDescriptorSetLayout(*_device, pair.second, nullptr);
    }

    // Base class destructor will handle pipeline and pipeline layout
    INK_DEBUG << "Graphics Pipeline destroyed.";
}

void VkGraphicsPipelineManager::setPushConstantRange(VkShaderStageFlags stageFlags, u32 offset, u32 size)
{
    _pushConstantRange.stageFlags = stageFlags;
    _pushConstantRange.offset = offset;
    _pushConstantRange.size = size;
    _hasPushConstants = size > 0;
}

void VkGraphicsPipelineManager::cmdPushConstants(VkCommandBuffer commandBuffer, const void* data)
{
    if (!_hasPushConstants || _pipelineLayout == VK_NULL_HANDLE || !data)
        return;

    vkCmdPushConstants(commandBuffer,
                       _pipelineLayout,
                       _pushConstantRange.stageFlags,
                       _pushConstantRange.offset,
                       _pushConstantRange.size,
                       data);
}

void VkGraphicsPipelineManager::addDescriptorBinding(u32 setIndex, const DescriptorBindingInfo& bindingInfo)
{
    // If this set doesn't exist yet, create it
    if (_descriptorSetLayoutInfos.find(setIndex) == _descriptorSetLayoutInfos.end()) {
        DescriptorSetLayoutInfo setInfo;
        setInfo.setIndex = setIndex;
        _descriptorSetLayoutInfos[setIndex] = setInfo;
    }

    // Add the binding to the set
    _descriptorSetLayoutInfos[setIndex].bindings.push_back(bindingInfo);
}

void VkGraphicsPipelineManager::resetInterface()
{
    for (auto& pair : _descriptorSetLayouts) 
    {
        vkDestroyDescriptorSetLayout(*_device, pair.second, nullptr);
    }
    _descriptorSetLayouts.clear();
    _descriptorSetLayoutInfos.clear();

    _pushConstantRange = {};
    _hasPushConstants = false;
}

void VkGraphicsPipelineManager::createDescriptorSetLayouts()
{
    // Clean up any existing layouts
    for (auto& pair : _descriptorSetLayouts) {
        vkDestroyDescriptorSetLayout(*_device, pair.second, nullptr);
    }
    _descriptorSetLayouts.clear();

    // Create a layout for each descriptor set
    for (const auto& pair : _descriptorSetLayoutInfos)
    {
        const DescriptorSetLayoutInfo& setInfo = pair.second;

        // Convert our bindings to VkDescriptorSetLayoutBinding
        std::vector<VkDescriptorSetLayoutBinding> layoutBindings;
        for (const auto& binding : setInfo.bindings) {
            VkDescriptorSetLayoutBinding layoutBinding{};
            layoutBinding.binding = binding.binding;
            layoutBinding.descriptorType = binding.descriptorType;
            layoutBinding.descriptorCount = binding.descriptorCount;
            layoutBinding.stageFlags = binding.stageFlags;
            layoutBinding.pImmutableSamplers = binding.pImmutableSamplers;
            layoutBindings.push_back(layoutBinding);
        }

        // Create descriptor set layout
        VkDescriptorSetLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layoutInfo.bindingCount = static_cast<u32>(layoutBindings.size());
        layoutInfo.pBindings = layoutBindings.data();

        VkDescriptorSetLayout layout;
        VK_RESULT_CHECK(vkCreateDescriptorSetLayout(*_device, &layoutInfo, nullptr, &layout));

        // Store the layout
        _descriptorSetLayouts[setInfo.setIndex] = layout;

        INK_DEBUG << "Created descriptor set layout for set " << setInfo.setIndex
                   << " with " << layoutBindings.size() << " bindings";
    }

    // Create pipeline layout with all descriptor set layouts
    std::vector<VkDescriptorSetLayout> layouts;
    u32 maxSetIndex = 0;

    // Find the maximum set index to ensure we create a properly sized array
    for (const auto& pair : _descriptorSetLayouts) {
        if (pair.first > maxSetIndex) {
            maxSetIndex = pair.first;
        }
    }

    // Create a properly ordered array of layouts
    layouts.resize(maxSetIndex + 1);
    for (const auto& pair : _descriptorSetLayouts) {
        layouts[pair.first] = pair.second;
    }

    if (_pipelineLayout != VK_NULL_HANDLE)
    {
        vkDestroyPipelineLayout(
            *_device,
            _pipelineLayout,
            nullptr);

        _pipelineLayout = VK_NULL_HANDLE;
    }

    // Create pipeline layout
    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = static_cast<u32>(layouts.size());
    pipelineLayoutInfo.pSetLayouts = layouts.data();

    if (_hasPushConstants)
    {
        pipelineLayoutInfo.pushConstantRangeCount = 1;
        pipelineLayoutInfo.pPushConstantRanges = &_pushConstantRange;
    }

    // Create the pipeline layout
    VK_RESULT_CHECK(vkCreatePipelineLayout(*_device, &pipelineLayoutInfo, nullptr, &_pipelineLayout));

    INK_DEBUG << "Created pipeline layout with " << layouts.size()
              << " descriptor set layouts and "
              << (_hasPushConstants ? _pushConstantRange.size : 0u) << " push-constant bytes";
}

VkDescriptorSetLayout VkGraphicsPipelineManager::getDescriptorSetLayout(u32 setIndex) const
{
    auto it = _descriptorSetLayouts.find(setIndex);
    if (it != _descriptorSetLayouts.end()) {
        return it->second;
    }
    return VK_NULL_HANDLE;
}

void VkGraphicsPipelineManager::createPipeline(VkRenderPass renderPass,
                                               VkExtent2D extent,
                                               const std::vector<VkVertexInputBindingDescription>& vertexBindingDescArray,
                                               const AttributeDescriptionArray<VkVertexInputAttributeDescription>& vertexAttributeDescArray,
                                               u32 attributeDescriptionCount,
                                               const PipelineOptions& options)
{
    if (_pipeline != VK_NULL_HANDLE)
    {
        vkDestroyPipeline(
            *_device,
            _pipeline,
            nullptr);

        _pipeline = VK_NULL_HANDLE;
    }

    if (_descriptorSetLayouts.empty()) {
        createDescriptorSetLayouts();
    }

    VkGraphicsPipelineCreateInfo pipelineInfo = {};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = static_cast<u32>(_shaderStages.size());
    pipelineInfo.pStages = _shaderStages.data();

    // Vertex input state
    VkPipelineVertexInputStateCreateInfo vertexInputInfo = {};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount = static_cast<u32>(vertexBindingDescArray.size());
    vertexInputInfo.pVertexBindingDescriptions = vertexBindingDescArray.data();
    vertexInputInfo.vertexAttributeDescriptionCount = attributeDescriptionCount;
    vertexInputInfo.pVertexAttributeDescriptions = vertexAttributeDescArray.data();

    // Input assembly state
    VkPipelineInputAssemblyStateCreateInfo inputAssembly = {};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    // Viewport and scissor
    VkViewport viewport = {};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<f32>(extent.width);
    viewport.height = static_cast<f32>(extent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    VkRect2D scissor = {};
    scissor.offset = {0, 0};
    scissor.extent = extent;

    // Dynamic state
    VkPipelineDynamicStateCreateInfo dynamicState = {};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = static_cast<u32>(_dynamicStates.size());
    dynamicState.pDynamicStates = _dynamicStates.data();

    // Viewport state
    VkPipelineViewportStateCreateInfo viewportState = {};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.pViewports = &viewport; // Will be dynamic
    viewportState.scissorCount = 1;
    viewportState.pScissors = &scissor; // Will be dynamic

    // Rasterization state
    VkPipelineRasterizationStateCreateInfo rasterizer = {};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    //! Screen-space overlay quads have no meaningful facing, so culling them
    //! would drop whichever winding the batch happened to emit.
    rasterizer.cullMode = options.cullBackFaces ? VK_CULL_MODE_BACK_BIT : VK_CULL_MODE_NONE;
    /*
     * The dynamic viewport this pipeline is bound with (cmdIndexedDraw() /
     * cmdDraw()) uses a negative height to flip Y into GLM's convention (see
     * the comment there). That flip mirrors every triangle's 2D footprint in
     * framebuffer space, which reverses the winding the rasterizer measures:
     * geometry authored clockwise now arrives counter-clockwise on screen.
     * VK_FRONT_FACE_COUNTER_CLOCKWISE compensates so cullBackFaces still
     * culls the actual back faces instead of the front ones -- without it, a
     * single-sided mesh like a ground plane is entirely invisible (its one
     * visible face is exactly the one now getting culled), while a closed
     * mesh like a cube just silently shows its inside instead of its outside.
     */
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_FALSE;
    rasterizer.depthBiasSlopeFactor = 0.0f;
    rasterizer.depthBiasConstantFactor = 0.0f;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.depthBiasClamp = 0.0f;

    // Multisample state
    VkPipelineMultisampleStateCreateInfo multisampling = {};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = options.sampleCount;
    multisampling.minSampleShading = 1.0f; // Optional
    multisampling.pSampleMask = nullptr; // Optional
    multisampling.alphaToCoverageEnable = VK_FALSE; // Optional
    multisampling.alphaToOneEnable = VK_FALSE; // Optional

    // Color blend attachment
    VkPipelineColorBlendAttachmentState colorBlendAttachment = {};
    colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT |
                                          VK_COLOR_COMPONENT_G_BIT |
                                          VK_COLOR_COMPONENT_B_BIT |
                                          VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachment.blendEnable = options.alphaBlend ? VK_TRUE : VK_FALSE;
    colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
    colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;

    if (options.alphaBlend)
    {
        /*
         * Straight (non-premultiplied) source-over:
         *   rgb = src.rgb * src.a + dst.rgb * (1 - src.a)
         *   a   = src.a         + dst.a   * (1 - src.a)
         * The alpha channel accumulates rather than replacing, so stacking two
         * translucent overlays leaves a sensible coverage value behind.
         */
        colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    }
    else
    {
        //! Ignored while blendEnable is false, but must still be valid values.
        colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
        colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ZERO;
        colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    }

    // Color blend state
    VkPipelineColorBlendStateCreateInfo colorBlending = {};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.logicOp = VK_LOGIC_OP_COPY;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;
    colorBlending.blendConstants[0] = 0.0f; // Optional
    colorBlending.blendConstants[1] = 0.0f; // Optional
    colorBlending.blendConstants[2] = 0.0f; // Optional
    colorBlending.blendConstants[3] = 0.0f; // Optional

    // Depth stencil state (only meaningful when render pass has a depth attachment)
    VkPipelineDepthStencilStateCreateInfo depthStencil = {};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = options.depthTest ? VK_TRUE : VK_FALSE;
    depthStencil.depthWriteEnable = options.depthTest ? VK_TRUE : VK_FALSE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;
    depthStencil.depthBoundsTestEnable = VK_FALSE;
    depthStencil.stencilTestEnable = VK_FALSE;

    // Set all states in pipeline info
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = _pipelineLayout;
    pipelineInfo.renderPass = renderPass;
    pipelineInfo.subpass = 0;
    pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;
    pipelineInfo.basePipelineIndex = -1;

    // Create the graphics pipeline
    VK_RESULT_CHECK(vkCreateGraphicsPipelines(*_device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &_pipeline));

    INK_DEBUG << "Graphics pipeline created";
}

void VkGraphicsPipelineManager::cmdBindPipeline(VkCommandBuffer commandBuffer, VkPipelineBindPoint bindPoint)
{
    vkCmdBindPipeline(commandBuffer, bindPoint, _pipeline);
}

void VkGraphicsPipelineManager::cmdBindDescriptorSets(VkCommandBuffer commandBuffer,
                                                      VkPipelineBindPoint bindPoint,
                                                      u32 firstSet,
                                                      u32 descriptorSetCount,
                                                      const VkDescriptorSet* pDescriptorSets,
                                                      u32 dynamicOffsetCount,
                                                      const u32* pDynamicOffsets)
{
    vkCmdBindDescriptorSets(commandBuffer,
                            bindPoint,
                            _pipelineLayout,
                            firstSet,
                            descriptorSetCount,
                            pDescriptorSets,
                            dynamicOffsetCount,
                            pDynamicOffsets);
}

void VkGraphicsPipelineManager::cmdIndexedDraw(VkCommandBuffer commandBuffer,
                                               VkExtent2D extent,
                                               u32 indexCount,
                                               u32 instanceCount,
                                               u32 firstIndex,
                                               i32 vertexOffset,
                                               u32 firstInstance)
{
    // Set dynamic viewport and scissor
    VkViewport viewport = {};
    viewport.x = 0.0f;
    /*
     * Negative-height viewport (core since Vulkan 1.1 / VK_KHR_maintenance1):
     * flips the NDC-to-framebuffer Y mapping so that NDC -1 lands at the
     * *top* of the image, matching OpenGL/GLM's Y-up convention instead of
     * Vulkan's native Y-down NDC. Without this, every mesh drawn with a
     * GLM-built (Y-up) projection -- which is what Camera::perspective()/
     * ortho() produce -- renders vertically mirrored: geometry below the
     * camera (e.g. a floor) appears at the top of the screen instead of the
     * bottom. Mirroring the image also mirrors the 2D winding the rasterizer
     * measures for every triangle, which is why createPipeline() sets
     * VK_FRONT_FACE_COUNTER_CLOCKWISE to compensate -- see the comment there.
     */
    viewport.y = static_cast<f32>(extent.height);
    viewport.width = static_cast<f32>(extent.width);
    viewport.height = -static_cast<f32>(extent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(commandBuffer, 0, 1, &viewport);

    VkRect2D scissor = {};
    scissor.offset = {0, 0};
    scissor.extent = extent;
    vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

    // Draw vertices
    vkCmdDrawIndexed(commandBuffer, indexCount, instanceCount, firstIndex, vertexOffset, firstInstance);
}

void VkGraphicsPipelineManager::cmdDraw(VkCommandBuffer commandBuffer,
                                        VkExtent2D extent,
                                        u32 vertexCount,
                                        u32 instanceCount,
                                        u32 firstVertex,
                                        u32 firstInstance)
{
    // Set dynamic viewport and scissor
    VkViewport viewport = {};
    viewport.x = 0.0f;
    //! Same Y-flip as cmdIndexedDraw(); see the comment there.
    viewport.y = static_cast<f32>(extent.height);
    viewport.width = static_cast<f32>(extent.width);
    viewport.height = -static_cast<f32>(extent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(commandBuffer, 0, 1, &viewport);

    VkRect2D scissor = {};
    scissor.offset = {0, 0};
    scissor.extent = extent;
    vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

    // Draw vertices
    vkCmdDraw(commandBuffer, vertexCount, instanceCount, firstVertex, firstInstance);
}

}
}
