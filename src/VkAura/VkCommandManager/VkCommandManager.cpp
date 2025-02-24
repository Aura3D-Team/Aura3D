#include "VkCommandManager.h"

#include "VkAura/VkException/VkException.h"

#include <plog/Log.h>

namespace aura3d {

std::unordered_map<std::thread::id, VkCommandPool> VkCommandManager::_threadCommandPools;
std::mutex VkCommandManager::_poolMutex;

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

void VkCommandManager::resetCommandPool()
{
    VkCommandPool commandPool = getThreadCommandPool();
    VK_RESULT_CHECK(vkResetCommandPool(*_device, commandPool, 0));
}

VkCommandPool VkCommandManager::getThreadCommandPool() {
    std::lock_guard<std::mutex> lock(_poolMutex);
    std::thread::id threadId = std::this_thread::get_id();

    // Check if a command pool exists for this thread
    auto it = _threadCommandPools.find(threadId);
    if (it == _threadCommandPools.end()) {
        VkCommandPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        poolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
        poolInfo.queueFamilyIndex = _queueFamilyIndex;

        VkCommandPool commandPool;
        VK_RESULT_CHECK(vkCreateCommandPool(*_device, &poolInfo, nullptr, &commandPool));

        _threadCommandPools[threadId] = commandPool;
        return commandPool;
    } else {
        return it->second;
    }
}

VkFixedArray<VkCommandBuffer> VkCommandManager::createCommandBuffer()
{
    VkCommandPool commandPool = getThreadCommandPool();

    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandPool = commandPool;
    allocInfo.commandBufferCount = (uint32_t) MAX_FRAMES_IN_FLIGHT;

    VkFixedArray<VkCommandBuffer> commandBuffers = {};
    VK_RESULT_CHECK(vkAllocateCommandBuffers(*_device, &allocInfo, commandBuffers.data()));

    return commandBuffers;
}

void VkCommandManager::resetCommandBuffer(VkCommandBuffer commandBuffer)
{
    VK_RESULT_CHECK(vkResetCommandBuffer(commandBuffer, 0));
}

void VkCommandManager::beginCommandBuffer(VkCommandBuffer commandBuffer)
{
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    beginInfo.pInheritanceInfo = nullptr;

    VK_RESULT_CHECK(vkBeginCommandBuffer(commandBuffer, &beginInfo));
}

void VkCommandManager::endCommandBuffer(VkCommandBuffer commandBuffer) {
    VK_RESULT_CHECK(vkEndCommandBuffer(commandBuffer));
}

void VkCommandManager::freeCmdBuffer(VkCommandBuffer* commandBuffer)
{
    VkCommandPool commandPool = getThreadCommandPool();
    vkFreeCommandBuffers(*_device, commandPool, 1, commandBuffer);
}

}
