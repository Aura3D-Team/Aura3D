#ifndef VKAURACORE_H
#define VKAURACORE_H

#pragma once

#include <vulkan/vulkan.h>
#include <array>
#include <vector>

#include "aura/Core/AuraCore.h"
#include "aura/Renderer/Vulkan/VkAura/VkMemory/VkDeviceAllocator/VkDeviceAllocator.h"

#define MAX_FRAMES_IN_FLIGHT 2
#define MAX_ATTRIBUTE_DESCRIPTION_2D 3
#define MAX_ATTRIBUTE_DESCRIPTION_3D 4
#define MAX_ATTRIBUTE_DESCRIPTION MAX_ATTRIBUTE_DESCRIPTION_3D
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


/**
 * @brief Cached swapchain support information queried from the physical device.
 *
 * Populated once per surface and used to select the optimal swapchain format,
 * present mode, and extent during swapchain creation.
 */
struct SwapChainSupportDetails {
    VkSurfaceCapabilitiesKHR        capabilities; ///< Surface capabilities (min/max image count, extent, etc.).
    std::vector<VkSurfaceFormatKHR> formats;      ///< Supported surface formats.
    std::vector<VkPresentModeKHR>   presentModes; ///< Supported presentation modes.
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


/**
 * @brief Describes a single descriptor binding within a descriptor set layout.
 *
 * Used when building VkDescriptorSetLayoutBinding entries for pipeline creation.
 */
struct DescriptorBindingInfo {
    u32                binding;             ///< Binding point in the shader.
    VkDescriptorType   descriptorType;      ///< Type of descriptor (uniform buffer, sampler, etc.).
    u32                descriptorCount;     ///< Number of descriptors at this binding.
    VkShaderStageFlags stageFlags;          ///< Shader stages that access this binding.
    const VkSampler*   pImmutableSamplers;  ///< Optional immutable samplers (may be nullptr).

    DescriptorBindingInfo() : binding(0), descriptorType(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER),
        descriptorCount(1), stageFlags(VK_SHADER_STAGE_VERTEX_BIT),
        pImmutableSamplers(nullptr) {}
};

/**
 * @brief Groups all bindings for a single descriptor set.
 *
 * Passed to VkDescriptorManager to create a VkDescriptorSetLayout for the
 * set identified by @c setIndex.
 */
struct DescriptorSetLayoutInfo {
    u32                                setIndex; ///< Set number used in the shader (layout(set = N)).
    std::vector<DescriptorBindingInfo> bindings; ///< All bindings belonging to this set.

    DescriptorSetLayoutInfo() : setIndex(0) {}
};


/**
 * @brief Subresource range parameters used when creating a VkImageView.
 *
 * Passed to VkImageViewsManager::createImageViews() to configure the
 * aspect mask, mip levels, and array layers for each swapchain image view.
 */
struct ImageViewData {
    VkImageAspectFlags aspectMask;    ///< Aspect to expose (e.g. VK_IMAGE_ASPECT_COLOR_BIT).
    u32 baseMipLevel;                 ///< First mip level accessible to the view.
    u32 levelCount;                   ///< Number of mip levels accessible.
    u32 baseArrayLayer;               ///< First array layer accessible.
    u32 layerCount;                   ///< Number of array layers accessible.
};

/**
 * @brief Base metadata shared by all GPU buffer types.
 *
 * Stores the raw VkBuffer handle, its allocation ID inside VkDeviceAllocator,
 * and whether the underlying memory is persistently mapped for CPU writes.
 */
struct AuraBufferInfo {
    VkBuffer buffer      = VK_NULL_HANDLE; ///< Underlying Vulkan buffer handle.
    u32 allocationId     = 0;              ///< Allocation ID registered in VkDeviceAllocator.
    bool persistent      = false;          ///< True if the buffer memory is kept permanently mapped.
    void* mappedPointer  = nullptr;        ///< CPU-visible mapped region (persistent uploads).
};

/**
 * @brief Metadata for a vertex buffer, extending AuraBufferInfo.
 */
struct VertexBufferInfo : public AuraBufferInfo {
    size_t vertexCount = 0;  ///< Number of vertices stored in the buffer.
    bool   is2d        = true; ///< True for Vertex2d layout; false for Vertex3d layout.
};

/**
 * @brief Metadata for an index buffer, extending AuraBufferInfo.
 */
struct IndexBufferInfo : public AuraBufferInfo {
    u32 indexCount = 0; ///< Number of indices stored in the buffer.
};

/**
 * @brief Aggregates the depth buffer image and its GPU memory allocation.
 *
 * Keeps the VkImage handle and the VkDeviceAllocator allocation together so
 * they are always created, destroyed, and tested as a unit. The associated
 * VkImageView is owned separately by VkImageViewsManager.
 *
 * Defaults to VK_FORMAT_D32_SFLOAT; only valid in 3D rendering mode.
 */
struct DepthResources {
    VkImage            image      = VK_NULL_HANDLE;    ///< Depth image handle.
    VkDeviceAllocation allocation = {};                ///< Device memory backing the image.
    VkFormat           format     = VK_FORMAT_D32_SFLOAT; ///< Depth format used for image and view creation.

    /** @brief Returns true when the depth image has been allocated. */
    bool isValid() const { return image != VK_NULL_HANDLE; }

    /** @brief Resets all fields to their null/empty defaults without freeing resources. */
    void reset() { image = VK_NULL_HANDLE; allocation = {}; }
};

}

}

#endif
