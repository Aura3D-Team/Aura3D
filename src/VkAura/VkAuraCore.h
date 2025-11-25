#ifndef VKAURACORE_H
#define VKAURACORE_H

#pragma once

#include <vulkan/vulkan.h>
#include <array>
#include <vector>

#include "AuraCore.h"

#define MAX_FRAMES_IN_FLIGHT 2
#define MAX_ATTRIBUTE_DESCRIPTION 3
#define MAX_SHADER_MODULES 8
#define MAX_DESCRIPTOR_SETS 4
#define MAX_BINDING_COUNT 16

// Template array definitions
template <typename T>
using VkFixedArray = std::array<T, MAX_FRAMES_IN_FLIGHT>;

template <typename T>
using AttributeDescriptionArray = std::array<T, MAX_ATTRIBUTE_DESCRIPTION>;

template <typename T>
using ShaderModuleArray = std::array<T, MAX_SHADER_MODULES>;

template <typename T>
using DescriptorSetArray = std::array<T, MAX_DESCRIPTOR_SETS>;

template <typename T>
using BindingArray = std::array<T, MAX_BINDING_COUNT>;

namespace aura3d {

namespace vk {

/**
 * @brief This struct represents Important data for VkInstance creation
 *
 * Obs: for more details, it can have more parameters in the future
 */
struct VkInstanceData {
    const char* appName;
    const char* engineName;
    std::vector<int> appVersion;
    std::vector<const char*> vkInstanceExtensions;
    std::vector<const char*> vkValidationLayers;
};


/**
 * @brief This struct represents Important data for VkDevice creation
 *
 * Obs: for more details, it can have more parameters in the future
 */
struct VkDeviceData {
    std::vector<const char*> vkDeviceExtensions;
    std::vector<const char*> vkEnabledLayers;

    std::vector<VkQueueFlags> concurrentQueueFlags;
    VkQueueFlags exclusiveQueueFlags;
};

/**
 * @brief Holds information about a Vulkan queue.
 *
 * This struct stores data related to a Vulkan queue, including the queues themselves,
 * a map of queue priorities per queue family index, and the corresponding
 * VkDeviceQueueCreateInfo structure.
 */
struct QueueData {
    std::vector<VkQueue> queues;
    std::vector<f32> queuePriorities;
    VkDeviceQueueCreateInfo vkDeviceQueueCreateInfo{};
};


struct SwapChainSupportDetails {
    VkSurfaceCapabilitiesKHR capabilities;
    std::vector<VkSurfaceFormatKHR> formats;
    std::vector<VkPresentModeKHR> presentModes;
};


/**
 * @brief Stores command pool creation info and Vulkan command pool.
 *
 * This struct holds the necessary information and Vulkan command pool (`VkCommandPool`)
 * for managing command buffers in a Vulkan application.
 */
struct VkCommandPoolData {
    VkCommandPoolCreateInfo commandPoolInfo; /**< Information for creating a Vulkan command pool. */
    VkCommandPool commandPool; /**< Vulkan command pool for submitting command buffers. */
};


// Descriptor binding information for pipeline creation
struct DescriptorBindingInfo {
    u32 binding;                     // Binding point in shader
    VkDescriptorType descriptorType;      // Type of descriptor (uniform buffer, sampler, etc.)
    u32 descriptorCount;             // Number of descriptors in this binding
    VkShaderStageFlags stageFlags;        // Shader stages that use this binding
    const VkSampler* pImmutableSamplers;  // Optional immutable samplers

    DescriptorBindingInfo() : binding(0), descriptorType(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER),
        descriptorCount(1), stageFlags(VK_SHADER_STAGE_VERTEX_BIT),
        pImmutableSamplers(nullptr) {}
};

// Set of descriptor bindings for a single descriptor set
struct DescriptorSetLayoutInfo {
    u32 setIndex;                                // Set index used in the shader
    std::vector<DescriptorBindingInfo> bindings;      // Bindings in this set

    DescriptorSetLayoutInfo() : setIndex(0) {}
};


struct ImageViewData {
    VkImageAspectFlags aspectMask;
    u32 baseMipLevel;
    u32 levelCount;
    u32 baseArrayLayer;
    u32 layerCount;
};

// Base class for buffer information
struct AuraBufferInfo {
    VkBuffer buffer = VK_NULL_HANDLE;
    u32 allocationId = 0;      // ID for tracking in VkDeviceAllocator
    bool persistent = false;   // Whether the buffer is persistently mapped
};

// Vertex buffer information
struct VertexBufferInfo : public AuraBufferInfo {
    size_t vertexCount = 0;
    bool is2d = true;          // Whether the buffer contains 2D or 3D vertices
};

struct IndexBufferInfo : public AuraBufferInfo {
    u32 indexCount = 0;
};

}

}

#endif
