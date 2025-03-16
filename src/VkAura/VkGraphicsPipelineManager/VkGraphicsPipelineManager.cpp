#include "VkGraphicsPipelineManager.h"

#include <aura.hpp>
#include <AuraException/AuraException.h>
#include <plog/Log.h>

namespace aura3d {

VkGraphicsPipelineManager::VkGraphicsPipelineManager(VkHostAllocator* vkHostAllocator,
                                                     std::string shader_vert_spv,
                                                     std::string shader_frag_spv,
                                                     VkDevice* device)
    : vkHostAllocator(vkHostAllocator), VkPipelineManager(vkHostAllocator, device), _shaderManager(VkShaderManager(vkHostAllocator, device))
{
    // Load shader modules
    _shaderManager.createVertShaderModule(shader_vert_spv);
    _shaderManager.createFragShaderModule(shader_frag_spv);

    // Initialize shader stages
    // Vertex Shader Stage Info
    VkPipelineShaderStageCreateInfo vertShaderStageInfo{};
    vertShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertShaderStageInfo.module = _shaderManager.getVertShaderModule();
    vertShaderStageInfo.pName = "main";

    // Fragment Shader Stage Info
    VkPipelineShaderStageCreateInfo fragShaderStageInfo{};
    fragShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    fragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragShaderStageInfo.module = _shaderManager.getFragShaderModule();
    fragShaderStageInfo.pName = "main";

    // Store in array
    _shaderStages = { vertShaderStageInfo, fragShaderStageInfo };

    // Initialize dynamic states
    initializeDynamicStates();

    // Add default UBO descriptor for vertex shader (set 0, binding 0)
    DescriptorBindingInfo uboBinding;
    uboBinding.binding = 0;
    uboBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    uboBinding.descriptorCount = 1;
    uboBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    addDescriptorBinding(0, uboBinding);

    // Add default sampler descriptor for fragment shader (set 1, binding 0)
    DescriptorBindingInfo samplerBinding;
    samplerBinding.binding = 0;
    samplerBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    samplerBinding.descriptorCount = 1;
    samplerBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    addDescriptorBinding(1, samplerBinding);
}

VkGraphicsPipelineManager::~VkGraphicsPipelineManager()
{
    // Clean up descriptor set layouts
    for (auto& pair : _descriptorSetLayouts) {
        vkDestroyDescriptorSetLayout(*_device, pair.second, vkHostAllocator->getCallbacks());
    }

    // Base class destructor will handle pipeline and pipeline layout
    PLOG_DEBUG << "Graphics Pipeline destroyed.";
}

void VkGraphicsPipelineManager::initializeDynamicStates()
{
    _dynamicStates = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
}

void VkGraphicsPipelineManager::addDescriptorBinding(uint32_t setIndex, const DescriptorBindingInfo& bindingInfo)
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

void VkGraphicsPipelineManager::createDescriptorSetLayouts()
{
    // Clean up any existing layouts
    for (auto& pair : _descriptorSetLayouts) {
        vkDestroyDescriptorSetLayout(*_device, pair.second, nullptr);
    }
    _descriptorSetLayouts.clear();

    // Create a layout for each descriptor set
    for (const auto& pair : _descriptorSetLayoutInfos) {
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
        layoutInfo.bindingCount = static_cast<uint32_t>(layoutBindings.size());
        layoutInfo.pBindings = layoutBindings.data();

        VkDescriptorSetLayout layout;
        VK_RESULT_CHECK(vkCreateDescriptorSetLayout(*_device, &layoutInfo, vkHostAllocator->getCallbacks(), &layout));

        // Store the layout
        _descriptorSetLayouts[setInfo.setIndex] = layout;

        PLOG_DEBUG << "Created descriptor set layout for set " << setInfo.setIndex
                   << " with " << layoutBindings.size() << " bindings";
    }

    // Create pipeline layout with all descriptor set layouts
    std::vector<VkDescriptorSetLayout> layouts;
    uint32_t maxSetIndex = 0;

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

    // Create pipeline layout
    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = static_cast<uint32_t>(layouts.size());
    pipelineLayoutInfo.pSetLayouts = layouts.data();

    // Create the pipeline layout
    VK_RESULT_CHECK(vkCreatePipelineLayout(*_device, &pipelineLayoutInfo, vkHostAllocator->getCallbacks(), &_pipelineLayout));

    PLOG_DEBUG << "Created pipeline layout with " << layouts.size() << " descriptor set layouts";
}

VkDescriptorSetLayout VkGraphicsPipelineManager::getDescriptorSetLayout(uint32_t setIndex) const
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
                                               const AttributeDescriptionArray<VkVertexInputAttributeDescription>& vertexAttributeDescArray)
{
    // Make sure descriptor set layouts are created
    if (_descriptorSetLayouts.empty()) {
        createDescriptorSetLayouts();
    }

    VkGraphicsPipelineCreateInfo pipelineInfo = {};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = static_cast<uint32_t>(_shaderStages.size());
    pipelineInfo.pStages = _shaderStages.data();

    // Vertex input state
    VkPipelineVertexInputStateCreateInfo vertexInputInfo = {};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount = static_cast<uint32_t>(vertexBindingDescArray.size());
    vertexInputInfo.pVertexBindingDescriptions = vertexBindingDescArray.data();
    vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(vertexAttributeDescArray.size());
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
    viewport.width = static_cast<float>(extent.width);
    viewport.height = static_cast<float>(extent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    VkRect2D scissor = {};
    scissor.offset = {0, 0};
    scissor.extent = extent;

    // Dynamic state
    VkPipelineDynamicStateCreateInfo dynamicState = {};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = static_cast<uint32_t>(_dynamicStates.size());
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
    rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
    rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_FALSE;
    rasterizer.depthBiasSlopeFactor = 0.0f;
    rasterizer.depthBiasConstantFactor = 0.0f;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.depthBiasClamp = 0.0f;

    // Multisample state
    VkPipelineMultisampleStateCreateInfo multisampling = {};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
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
    colorBlendAttachment.blendEnable = VK_FALSE;
    colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_ONE; // Optional
    colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ZERO; // Optional
    colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD; // Optional
    colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE; // Optional
    colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO; // Optional
    colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD; // Optional

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

    // Set all states in pipeline info
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = _pipelineLayout;
    pipelineInfo.renderPass = renderPass;
    pipelineInfo.subpass = 0;
    pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;
    pipelineInfo.basePipelineIndex = -1;

    // Create the graphics pipeline
    VK_RESULT_CHECK(vkCreateGraphicsPipelines(*_device, VK_NULL_HANDLE, 1, &pipelineInfo, vkHostAllocator->getCallbacks(), &_pipeline));

    PLOG_DEBUG << "Graphics pipeline created";
}

void VkGraphicsPipelineManager::cmdBindPipeline(VkCommandBuffer commandBuffer, VkPipelineBindPoint bindPoint)
{
    vkCmdBindPipeline(commandBuffer, bindPoint, _pipeline);
}

void VkGraphicsPipelineManager::cmdBindDescriptorSets(VkCommandBuffer commandBuffer,
                                                      VkPipelineBindPoint bindPoint,
                                                      uint32_t firstSet,
                                                      uint32_t descriptorSetCount,
                                                      const VkDescriptorSet* pDescriptorSets,
                                                      uint32_t dynamicOffsetCount,
                                                      const uint32_t* pDynamicOffsets)
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

void VkGraphicsPipelineManager::cmdDraw(VkCommandBuffer commandBuffer,
                                        VkExtent2D extent,
                                        uint32_t vertexCount,
                                        uint32_t instanceCount,
                                        uint32_t firstVertex,
                                        uint32_t firstInstance)
{
    // Set dynamic viewport and scissor
    VkViewport viewport = {};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(extent.width);
    viewport.height = static_cast<float>(extent.height);
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
