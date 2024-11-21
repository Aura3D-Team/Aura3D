#include "VkCommandManager.h"

#include <plog/Log.h>
#include "VkAura/VkException/VkException.h"

VkCommandManager::VkCommandManager(VkDevice* device, uint32_t queueFamilyIndex)
    : _device(device), _queueFamilyIndex(queueFamilyIndex) {
    // Command pools will now be created on demand for each thread
}

VkCommandManager::~VkCommandManager() {
    std::lock_guard<std::mutex> lock(_poolMutex);

    // Destroy all command pools created by each thread
    for (auto& it : _threadCommandPools) {
        vkDestroyCommandPool(*_device, it.second, nullptr);
        PLOG_DEBUG << "CommandPool for thread " << it.first << " was deleted.";
    }
    _device = nullptr;
}

VkCommandPool VkCommandManager::createThreadCommandPool() {
    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.queueFamilyIndex = _queueFamilyIndex;
    poolInfo.flags = 0;

    VkCommandPool commandPool;
    VkResult result = vkCreateCommandPool(*_device, &poolInfo, nullptr, &commandPool);
    if (result != VK_SUCCESS) {
        throw VkException(result);
    }

    return commandPool;
}

VkCommandPool VkCommandManager::getThreadCommandPool() {
    std::lock_guard<std::mutex> lock(_poolMutex);
    std::thread::id threadId = std::this_thread::get_id();

    // Check if a command pool exists for this thread
    auto it = _threadCommandPools.find(threadId);
    if (it == _threadCommandPools.end()) {
        VkCommandPool commandPool = createThreadCommandPool();
        _threadCommandPools[threadId] = commandPool;
        return commandPool;
    } else {
        return it->second;
    }
}

VkCommandBuffer VkCommandManager::beginCommandBuffer() {
    VkCommandPool commandPool = getThreadCommandPool();

    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandPool = commandPool;
    allocInfo.commandBufferCount = 1;

    VkCommandBuffer commandBuffer;
    VkResult result = vkAllocateCommandBuffers(*_device, &allocInfo, &commandBuffer);
    if (result != VK_SUCCESS) {
        throw VkException(result);
    }

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    // Begin recording on the command buffer
    result = vkBeginCommandBuffer(commandBuffer, &beginInfo);
    if (result != VK_SUCCESS) {
        throw VkException(result);
    }

    return commandBuffer;
}

void VkCommandManager::endCommandBuffer(VkCommandBuffer commandBuffer, VkQueue queue) {
    VkResult result = vkEndCommandBuffer(commandBuffer);
    if (result != VK_SUCCESS) {
        throw VkException(result);
    }

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;

    result = vkQueueSubmit(queue, 1, &submitInfo, VK_NULL_HANDLE);
    if (result != VK_SUCCESS) {
        throw VkException(result);
    }

    result = vkQueueWaitIdle(queue);
    if (result != VK_SUCCESS) {
        throw VkException(result);
    }

    VkCommandPool commandPool = getThreadCommandPool();
    vkFreeCommandBuffers(*_device, commandPool, 1, &commandBuffer);
}

