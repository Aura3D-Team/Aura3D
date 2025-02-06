#include "VkCommandManager.h"

#include "VkAura/VkException/VkException.h"

#include <plog/Log.h>

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

    PLOG_DEBUG << "CommandPools destroyed.";
}

VkCommandPool VkCommandManager::createThreadCommandPool() {
    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = _queueFamilyIndex;
    poolInfo.flags = 0;

    VkCommandPool commandPool;
    VK_RESULT_CHECK(vkCreateCommandPool(*_device, &poolInfo, nullptr, &commandPool));

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
    VK_RESULT_CHECK(vkAllocateCommandBuffers(*_device, &allocInfo, &commandBuffer));

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = 0;
    beginInfo.pInheritanceInfo = nullptr;

    VK_RESULT_CHECK(vkBeginCommandBuffer(commandBuffer, &beginInfo));

    return commandBuffer;
}

void VkCommandManager::endCommandBuffer(VkCommandBuffer commandBuffer) {
    VK_RESULT_CHECK(vkEndCommandBuffer(commandBuffer));

    VkCommandPool commandPool = getThreadCommandPool();
    vkFreeCommandBuffers(*_device, commandPool, 1, &commandBuffer);

    // Optional: Reset the command pool if all buffers in it are freed
    // Consider pool-wide reset only if pool won't be reused soon.
    // result = vkResetCommandPool(*_device, commandPool, 0);
    // VK_RESULT_CHECK(result);
}
