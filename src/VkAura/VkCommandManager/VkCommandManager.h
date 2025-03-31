#ifndef VKCOMMANDMANAGER_H
#define VKCOMMANDMANAGER_H

#pragma once

#include <vulkan/vulkan.h>
#include <unordered_map>
#include <mutex>
#include <thread>

#include "aura.hpp"
#include <VkAura/VkHostAllocator/VkHostAllocator.h>

namespace aura3d {

/**
 * @brief Manages Vulkan command pools and command buffers in a multi-threaded environment.
 *
 * The `VkCommandManager` class handles the creation and management of Vulkan command pools and
 * command buffers. It supports multi-threaded applications by providing thread-local command pools
 * and synchronized access to shared resources.
 */
class VkCommandManager
{
public:
    /**
     * @brief Constructs a `VkCommandManager` with a logical device and queue family index.
     *
     * This constructor initializes the `VkCommandManager` with a given Vulkan logical device (`VkDevice`)
     * and a specific queue family index. The manager will use these to allocate and manage command pools
     * and command buffers for Vulkan operations.
     *
     * @param device Pointer to a Vulkan logical device.
     * @param queueFamilyIndex Queue family index used for creating command pools.
     */
    VkCommandManager(VkHostAllocator* vkHostAllocator, VkDevice* device, u32 queueFamilyIndex);

    /**
     * @brief Destroys the `VkCommandManager` and releases resources.
     *
     * The destructor ensures that all command pools managed by the `VkCommandManager` are properly
     * released, freeing any Vulkan resources allocated.
     */
    ~VkCommandManager();

    /**
     * @brief Retrieves the command pool associated with the current thread.
     *
     * Ensures that each thread has its own command pool for safe multi-threaded command buffer allocation.
     *
     * @return VkCommandPool The thread-local command pool.
     */
    VkCommandPool getThreadCommandPool();

    VkFixedArray<VkCommandBuffer> createCommandBuffer();

    static void resetCommandBuffer(VkCommandBuffer commandBuffer);

    void resetCommandPool();

    /**
     * @brief Begins one or more command buffers for recording commands.
     *
     * This method allocates and begins recording on a specified number of command buffers.
     * The command buffers returned are ready for recording Vulkan commands.
     *
     * @param commandBuffer The command buffer to begin recording.
     */
    static void beginCommandBuffer(VkCommandBuffer commandBuffer);

    /**
     * @brief Ends recording of a command buffer array and submits it to a Vulkan queue.
     *
     * This method finalizes the recording of the specified command buffer array and submits it to the
     * provided Vulkan queue for execution.
     *
     * @param commandBuffer The command buffer to end.
     */
    static void endCommandBuffer(VkCommandBuffer commandBuffer);

    void freeCmdBuffer(VkCommandBuffer* commandBuffer);

private:
    VkHostAllocator* vkHostAllocator;

    VkDevice* _device;
    u32 _queueFamilyIndex; /**< The queue family index for command pool allocation. */

    // Map of command pools by thread ID
    static std::unordered_map<std::thread::id, VkCommandPool> _threadCommandPools;
    static std::mutex _poolMutex; /**< Mutex to protect access to the command pool map. */
};

}

#endif // VKCOMMANDMANAGER_H
