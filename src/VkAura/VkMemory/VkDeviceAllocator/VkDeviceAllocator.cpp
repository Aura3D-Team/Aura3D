#include "VkDeviceAllocator.h"

#include <algorithm>
#include <iostream>
#include <sstream>
#include <cassert>

namespace aura3d {
namespace vk {

VkDeviceAllocator::VkDeviceAllocator(VkHostAllocator* vkHostAllocator, const VkDeviceAllocatorCreateInfo& createInfo)
    : vkHostAllocator(vkHostAllocator),
    physicalDevice(createInfo.physicalDevice),
    device(createInfo.device),
    defaultBlockSize(createInfo.blockSize),
    smallBlockSize(createInfo.smallBlockSize),
    defragmentationEnabled(createInfo.enableDefragmentation),
    trackLeaks(createInfo.trackLeaks),
    threadSafetyMode(createInfo.threadSafetyMode),
    allocationStrategy(createInfo.strategy),
    dedicatedAllocationThreshold(createInfo.dedicatedAllocationThreshold),
    useBuddyAllocatorForBuffers(createInfo.useBuddyAllocatorForBuffers),
    deferFrees(createInfo.deferFrees),
    deferredFreeLimit(createInfo.deferredFreeLimit),
    nextAllocationId(1),
    inShutdown(false)
{

    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memoryProperties);

    INK_TRACE << (defaultBlockSize / (1024 * 1024)) << "MB, "
            << "defragmentation: " << (defragmentationEnabled ? "enabled" : "disabled") << ", "
            << "thread safety: " << (threadSafetyMode == ThreadSafetyMode::NONE ? "none" :
                                       (threadSafetyMode == ThreadSafetyMode::COARSE_GRAINED ? "coarse" : "fine"));
}

VkDeviceAllocator::~VkDeviceAllocator()
{
    if (threadSafetyMode != ThreadSafetyMode::NONE)
        std::lock_guard<std::mutex> lock(globalMutex);

    if (trackLeaks && !allocationMap.empty() && !inShutdown)
    {
        INK_WARN << "VkDeviceAllocator destructed with " << allocationMap.size()
        << " allocations still active!";

        for (const auto& pair : allocationMap) {
            const auto& alloc = pair.second;
            INK_WARN << "Leaked allocation: id=" << alloc.allocationId
                         << ", size=" << alloc.size << " bytes"
                         << ", mapped=" << (alloc.mappedData != nullptr ? "yes" : "no");
        }
    }

    if (!inShutdown)
        cleanup();
}

void VkDeviceAllocator::acquireLock(u32 memoryTypeIndex, bool forWrite)
{
    if (threadSafetyMode == ThreadSafetyMode::NONE)
    {
        return;
    }
    else if (threadSafetyMode == ThreadSafetyMode::COARSE_GRAINED)
    {
        globalMutex.lock();
    }
    else
    {
        if (memoryTypeIndex >= poolMutexes.size()) {
            std::lock_guard<std::mutex> lock(globalMutex);

            while (memoryTypeIndex >= poolMutexes.size()) {
                poolMutexes.push_back(std::make_unique<std::mutex>());
            }
        }

        poolMutexes[memoryTypeIndex]->lock();
    }


}

void VkDeviceAllocator::releaseLock(u32 memoryTypeIndex, bool forWrite)
{
    if (threadSafetyMode == ThreadSafetyMode::NONE)
        return;
    else if (threadSafetyMode == ThreadSafetyMode::COARSE_GRAINED)
        globalMutex.unlock();
    else
        poolMutexes[memoryTypeIndex]->unlock();
}

VkResult VkDeviceAllocator::allocateMemory(
    const VkMemoryRequirements& memRequirements,
    VkMemoryPropertyFlags properties,
    VkDeviceAllocation& allocation)
{
    if (inShutdown)
    {
        INK_ERROR << "Attempting to allocate memory during shutdown";
        return VK_ERROR_DEVICE_LOST;
    }

    allocation.reset();

    u32 memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits, properties);
    if (memoryTypeIndex == UINT32_MAX)
    {
        INK_ERROR << "Failed to find suitable memory type";
        return VK_ERROR_FEATURE_NOT_PRESENT;
    }



    if (deferFrees)
    {
        processDeferredFrees(false);
    }

    acquireLock(memoryTypeIndex, true);

    VkResult result = VK_ERROR_OUT_OF_DEVICE_MEMORY;

    if (memRequirements.size >= dedicatedAllocationThreshold)
    {
        VkMemoryAllocateInfo allocInfo = {};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = memRequirements.size;
        allocInfo.memoryTypeIndex = memoryTypeIndex;

        VkDeviceMemory memory = VK_NULL_HANDLE;
        result = vkAllocateMemory(device, &allocInfo, vkHostAllocator->getCallbacks(), &memory);

        if (result == VK_SUCCESS)
        {
            MemoryTypePool& pool = getOrCreateMemoryTypePool(memoryTypeIndex);
            MemoryBlock block = {};
            block.memory = memory;
            block.size = memRequirements.size;
            block.canBeMapped = (properties & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) != 0;

            allocation.memory = memory;
            allocation.offset = 0;
            allocation.size = memRequirements.size;
            allocation.memoryTypeIndex = memoryTypeIndex;
            allocation.allocationId = nextAllocationId++;

            pool.blocks.push_back(block);
            pool.totalSize += memRequirements.size;
            pool.usedSize += memRequirements.size;

            allocationMap[allocation.allocationId] = allocation;

        }
        else
        {
            INK_ERROR << "Dedicated allocation failed: " << result;
        }
    }
    else
    {
        if (useBuddyAllocatorForBuffers &&
            (properties & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) &&
            (properties & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) == 0)
        {
            result = allocateUsingBuddyAllocator(
                memoryTypeIndex,
                memRequirements.size,
                memRequirements.alignment,
                allocation
            );
        }

        if (result != VK_SUCCESS)
        {
            result = findAndAllocateInBlock(
                memoryTypeIndex,
                memRequirements.size,
                memRequirements.alignment,
                allocation
            );

            if (result != VK_SUCCESS)
            {
                VkDeviceSize blockSize = defaultBlockSize;

                if (memRequirements.size > defaultBlockSize / 2)
                    blockSize = memRequirements.size * 2;
                else if (memRequirements.size < smallBlockSize / 2)
                    blockSize = smallBlockSize;

                result = allocateNewBlock(memoryTypeIndex, blockSize);

                if (result != VK_SUCCESS)
                {
                    INK_ERROR << "Failed to allocate new block, result: " << result;
                    releaseLock(memoryTypeIndex, true);
                    return result;
                }


                result = findAndAllocateInBlock(
                    memoryTypeIndex,
                    memRequirements.size,
                    memRequirements.alignment,
                    allocation
                );

                if (result != VK_SUCCESS)
                    INK_ERROR << "Allocation failed even with new block: " << result;
            }
        }

        if (result == VK_SUCCESS)
        {
            if (allocation.allocationId == 0)
                allocation.allocationId = nextAllocationId++;

            allocationMap[allocation.allocationId] = allocation;
        }
    }

    releaseLock(memoryTypeIndex, true);
    return result;
}

VkResult VkDeviceAllocator::allocateUsingBuddyAllocator(
    u32 memoryTypeIndex,
    VkDeviceSize size,
    VkDeviceSize alignment,
    VkDeviceAllocation& allocation)
{
    MemoryTypePool* pool = nullptr;
    for (auto& p : memoryTypePools)
    {
        if (p.memoryTypeIndex == memoryTypeIndex)
        {
            pool = &p;
            break;
        }
    }

    if (!pool || !pool->buddyAllocator) {

        return VK_ERROR_INITIALIZATION_FAILED;
    }

    VkDeviceSize paddedSize = size;
    if ((paddedSize & (paddedSize - 1)) != 0)
    {
        paddedSize = 1;
        while (paddedSize < size)
            paddedSize <<= 1;
    }

    VkDeviceSize offset = 0;
    if (!pool->buddyAllocator->allocate(paddedSize, offset))
        return VK_ERROR_OUT_OF_DEVICE_MEMORY;

    MemoryBlock* block = nullptr;
    for (auto& b : pool->blocks)
    {
        if (b.memory != VK_NULL_HANDLE)
        {
            block = &b;
            break;
        }
    }

    if (!block)
    {
        INK_ERROR << "No memory block found for buddy allocation";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    allocation.memory = block->memory;
    allocation.offset = offset;
    allocation.size = size;
    allocation.memoryTypeIndex = memoryTypeIndex;
    allocation.allocationId = nextAllocationId++;

    pool->usedSize += size;

    return VK_SUCCESS;
}

VkResult VkDeviceAllocator::allocateMemoryForBuffer(
    VkBuffer buffer,
    VkMemoryPropertyFlags properties,
    VkDeviceAllocation& allocation)
{
    if (buffer == VK_NULL_HANDLE)
    {
        INK_ERROR << "Cannot allocate memory for null buffer";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(device, buffer, &memRequirements);

    return allocateMemory(memRequirements, properties, allocation);
}

VkResult VkDeviceAllocator::allocateMemoryForImage(
    VkImage image,
    VkMemoryPropertyFlags properties,
    VkDeviceAllocation& allocation)
{
    if (image == VK_NULL_HANDLE)
    {
        INK_ERROR << "Cannot allocate memory for null image";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    VkMemoryRequirements memRequirements;
    vkGetImageMemoryRequirements(device, image, &memRequirements);

    return allocateMemory(memRequirements, properties, allocation);
}

void VkDeviceAllocator::freeMemory(VkDeviceAllocation& allocation)
{
    if (inShutdown) {

        return;
    }

    auto allocationId = allocation.allocationId;
    if (allocationId == 0)
    {
        INK_WARN << "Attempting to free invalid allocation with ID 0";
        return;
    }

    if (deferFrees && !inShutdown)
    {
        u32 memoryTypeIndex = allocation.memoryTypeIndex;
        acquireLock(memoryTypeIndex, true);

        for (auto& pool : memoryTypePools)
        {
            if (pool.memoryTypeIndex == memoryTypeIndex)
            {
                pool.deferredFrees.push_back(allocationId);

                if (pool.deferredFrees.size() >= deferredFreeLimit)
                {
                    processDeferredFrees(false);
                }
                break;
            }
        }

        releaseLock(memoryTypeIndex, true);
        return;
    }

    u32 memoryTypeIndex = allocation.memoryTypeIndex;
    acquireLock(memoryTypeIndex, true);

    auto it = allocationMap.find(allocationId);
    if (it == allocationMap.end())
    {
        INK_WARN << "Attempting to free unknown allocation: " << allocationId;
        releaseLock(memoryTypeIndex, true);
        return;
    }

    if (allocation.mappingState != AllocationMappingState::UNMAPPED)
    {
        if (!inShutdown) {
            unmapMemory(allocation);
        }
    }

    for (auto& pool : memoryTypePools)
    {
        if (pool.memoryTypeIndex == allocation.memoryTypeIndex)
        {
            if (pool.buddyAllocator)
            {
                for (auto& block : pool.blocks)
                {
                    if (block.memory == allocation.memory)
                    {

                        pool.buddyAllocator->free(allocation.offset);

                        pool.usedSize -= allocation.size;

                        allocationMap.erase(it);

                        allocation.reset();

                        releaseLock(memoryTypeIndex, true);
                        return;
                    }
                }
            }

            for (auto& block : pool.blocks)
            {
                if (block.memory == allocation.memory)
                {
                    if (allocation.offset == 0 && allocation.size == block.size)
                    {
                        if (block.isMapped)
                            vkUnmapMemory(device, block.memory);

                        vkFreeMemory(device, block.memory, nullptr);

                        pool.totalSize -= block.size;
                        pool.usedSize -= allocation.size;

                        auto blockIt = std::find_if(pool.blocks.begin(), pool.blocks.end(),
                                                    [&block](const MemoryBlock& b) {
                                                        return b.memory == block.memory;
                                                    });

                        if (blockIt != pool.blocks.end())
                            pool.blocks.erase(blockIt);
                    }
                    else
                    {
                        block.freeRanges.insert(allocation.offset, allocation.size);

                        if (block.mappedRegions.count(allocation.offset) > 0)
                            block.mappedRegions.erase(allocation.offset);

                        pool.usedSize -= allocation.size;

                        block.freeRanges.coalesce();
                    }

                    allocationMap.erase(it);
                    allocation.reset();

                    releaseLock(memoryTypeIndex, true);
                    return;
                }
            }
        }
    }

    INK_ERROR << "Failed to find memory block for allocation: " << allocation.allocationId;
    releaseLock(memoryTypeIndex, true);
}

VkResult VkDeviceAllocator::mapMemory(
    VkDeviceAllocation& allocation,
    VkDeviceSize offset,
    VkDeviceSize size,
    void** ppData)
{
    if (inShutdown)
    {
        INK_WARN << "Attempting to map memory during shutdown";
        return VK_ERROR_DEVICE_LOST;
    }

    if (allocation.memory == VK_NULL_HANDLE)
    {
        INK_ERROR << "Attempting to map invalid allocation";
        return VK_ERROR_MEMORY_MAP_FAILED;
    }

    u32 memoryTypeIndex = allocation.memoryTypeIndex;
    acquireLock(memoryTypeIndex, true);

    if (allocation.mappingState != AllocationMappingState::UNMAPPED)
    {
        *ppData = allocation.mappedData;
        releaseLock(memoryTypeIndex, true);
        return VK_SUCCESS;
    }

    VkMemoryPropertyFlags memFlags = memoryProperties.memoryTypes[allocation.memoryTypeIndex].propertyFlags;
    if ((memFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) == 0)
    {
        INK_ERROR << "Attempting to map memory that is not host visible";
        releaseLock(memoryTypeIndex, true);
        return VK_ERROR_MEMORY_MAP_FAILED;
    }

    if (size == VK_WHOLE_SIZE)
        size = allocation.size - offset;

    VkDeviceSize actualOffset = allocation.offset + offset;

    MemoryBlock* block = nullptr;
    if (!findBlockByMemory(allocation.memory, &block))
    {
        INK_ERROR << "Failed to find memory block for mapping";
        releaseLock(memoryTypeIndex, true);
        return VK_ERROR_MEMORY_MAP_FAILED;
    }

    void* mappedData = nullptr;
    if (block->isMapped)
        mappedData = static_cast<char*>(block->mappedAddress) + actualOffset;
    else
    {
        VkResult result = vkMapMemory(device, allocation.memory, 0, block->size, 0, &block->mappedAddress);
        if (result != VK_SUCCESS)
        {
            INK_ERROR << "Failed to map memory: id=" << allocation.allocationId << ", error=" << result;
            releaseLock(memoryTypeIndex, true);
            return result;
        }

        block->isMapped = true;
        mappedData = static_cast<char*>(block->mappedAddress) + actualOffset;
    }

    if ((memFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) == 0)
    {
        VkMappedMemoryRange range = {};
        range.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
        range.memory = allocation.memory;
        range.offset = actualOffset;
        range.size = size;
        vkInvalidateMappedMemoryRanges(device, 1, &range);
    }

    block->mappedRegions[allocation.offset] = allocation.allocationId;

    allocation.mappedData = mappedData;
    allocation.mappingState = AllocationMappingState::MAPPED;
    *ppData = mappedData;

    allocationMap[allocation.allocationId] = allocation;

    releaseLock(memoryTypeIndex, true);
    return VK_SUCCESS;
}

void VkDeviceAllocator::unmapMemory(VkDeviceAllocation& allocation)
{
    if (allocation.mappingState == AllocationMappingState::UNMAPPED)
        return;

    u32 memoryTypeIndex = allocation.memoryTypeIndex;
    acquireLock(memoryTypeIndex, true);

    if (!inShutdown && allocation.mappingState == AllocationMappingState::PERSISTENTLY_MAPPED)
    {
        releaseLock(memoryTypeIndex, true);
        return;
    }

    MemoryBlock* block = nullptr;
    if (!findBlockByMemory(allocation.memory, &block))
    {
        INK_ERROR << "Failed to find memory block for unmapping";
        releaseLock(memoryTypeIndex, true);
        return;
    }

    VkMemoryPropertyFlags memFlags = memoryProperties.memoryTypes[allocation.memoryTypeIndex].propertyFlags;
    if ((memFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) == 0)
    {
        VkMappedMemoryRange range = {};
        range.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
        range.memory = allocation.memory;
        range.offset = allocation.offset;
        range.size = allocation.size;
        vkFlushMappedMemoryRanges(device, 1, &range);
    }

    allocation.mappedData = nullptr;
    allocation.mappingState = AllocationMappingState::UNMAPPED;

    if (block->mappedRegions.count(allocation.offset) > 0)
        block->mappedRegions.erase(allocation.offset);

    if (block->mappedRegions.empty() && block->isMapped)
    {
        vkUnmapMemory(device, allocation.memory);
        block->isMapped = false;
        block->mappedAddress = nullptr;
    }

    if (!inShutdown && allocationMap.count(allocation.allocationId) > 0)
    {
        allocationMap[allocation.allocationId] = allocation;
    }

    releaseLock(memoryTypeIndex, true);
}

void VkDeviceAllocator::unmapAllMemory()
{
    if (threadSafetyMode != ThreadSafetyMode::NONE)
        std::lock_guard<std::mutex> lock(globalMutex);

    size_t unmappedCount = 0;
    for (auto& pair : allocationMap)
    {
        auto& alloc = pair.second;
        if (alloc.mappingState != AllocationMappingState::UNMAPPED)
        {
            alloc.mappedData = nullptr;
            alloc.mappingState = AllocationMappingState::UNMAPPED;
            unmappedCount++;
        }
    }

    size_t unmappedBlocks = 0;
    for (auto& pool : memoryTypePools)
    {
        for (auto& block : pool.blocks)
        {
            if (block.isMapped)
            {
                block.mappedRegions.clear();
                vkUnmapMemory(device, block.memory);
                block.isMapped = false;
                block.mappedAddress = nullptr;
                unmappedBlocks++;
            }
        }
    }
}

VkResult VkDeviceAllocator::bindBufferMemory(
    VkBuffer buffer,
    const VkDeviceAllocation& allocation,
    VkDeviceSize offsetInAllocation)
{
    if (allocation.memory == VK_NULL_HANDLE)
    {
        INK_ERROR << "Attempting to bind to null allocation";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    VkDeviceSize bindOffset = allocation.offset + offsetInAllocation;
    VkResult result = vkBindBufferMemory(device, buffer, allocation.memory, bindOffset);

    if (result != VK_SUCCESS)
        INK_ERROR << "Buffer binding failed: " << result;

    return result;
}

VkResult VkDeviceAllocator::bindImageMemory(
    VkImage image,
    const VkDeviceAllocation& allocation,
    VkDeviceSize offsetInAllocation)
{
    if (allocation.memory == VK_NULL_HANDLE)
    {
        INK_ERROR << "Attempting to bind to null allocation";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    VkDeviceSize bindOffset = allocation.offset + offsetInAllocation;
    VkResult result = vkBindImageMemory(device, image, allocation.memory, bindOffset);

    if (result != VK_SUCCESS)
        INK_ERROR << "Image binding failed: " << result;

    return result;
}

MemoryStats VkDeviceAllocator::getMemoryStats() const
{
    if (threadSafetyMode != ThreadSafetyMode::NONE)
        std::lock_guard<std::mutex> lock(globalMutex);

    MemoryStats stats = {};
    stats.totalSize = 0;
    stats.usedSize = 0;
    stats.allocationCount = static_cast<u32>(allocationMap.size());
    stats.blockCount = 0;
    stats.fragmentationIndex = 0.0f;
    stats.largestFreeBlock = 0;
    stats.totalFreeChunks = 0;

    for (const auto& pool : memoryTypePools){
        stats.totalSize += pool.totalSize;
        stats.usedSize += pool.usedSize;
        stats.blockCount += static_cast<u32>(pool.blocks.size());
        stats.sizeByMemoryType.push_back(std::make_pair(pool.memoryTypeIndex, pool.usedSize));

        u32 totalChunks = 0;
        VkDeviceSize largestChunk = 0;

        for (const auto& block : pool.blocks)
        {
            VkDeviceSize blockLargestFree = 0;

            for (const auto& pair : block.freeRanges.offsetToSize)
            {
                totalChunks++;
                if (pair.second > blockLargestFree)
                    blockLargestFree = pair.second;
            }

            if (blockLargestFree > largestChunk)
                largestChunk = blockLargestFree;
        }

        stats.totalFreeChunks += totalChunks;

        if (largestChunk > stats.largestFreeBlock)
            stats.largestFreeBlock = largestChunk;
    }

    if (stats.totalSize > 0)
    {
        VkDeviceSize freeSize = stats.totalSize - stats.usedSize;
        if (freeSize > 0)
            stats.fragmentationIndex = 1.0f - (static_cast<f32>(stats.largestFreeBlock) / freeSize);
    }

    return stats;
}

void VkDeviceAllocator::printMemoryStats() const
{
    MemoryStats stats = getMemoryStats();

    INK_LOG << "===== VkDeviceAllocator Memory Stats =====";
    INK_LOG << "Total allocated: " << (stats.totalSize / (1024 * 1024)) << " MB";
    INK_LOG << "Total used: " << (stats.usedSize / (1024 * 1024)) << " MB";
    INK_LOG << "Utilization: " << (stats.totalSize > 0 ? (100.0f * stats.usedSize / stats.totalSize) : 0.0f) << "%";
    INK_LOG << "Fragmentation: " << (stats.fragmentationIndex * 100.0f) << "%";
    INK_LOG << "Largest free block: " << (stats.largestFreeBlock / (1024 * 1024)) << " MB";
    INK_LOG << "Allocations: " << stats.allocationCount;
    INK_LOG << "Memory blocks: " << stats.blockCount;
    INK_LOG << "Free chunks: " << stats.totalFreeChunks;

    INK_LOG << "Memory by type:";
    for (const auto& typeStat : stats.sizeByMemoryType)
    {
        u32 memoryTypeIndex = typeStat.first;
        VkDeviceSize memorySize = typeStat.second;

        if (memoryTypeIndex < memoryProperties.memoryTypeCount)
        {
            VkMemoryPropertyFlags flags = memoryProperties.memoryTypes[memoryTypeIndex].propertyFlags;
            std::stringstream flagStr;

            if (flags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)     flagStr << "DEVICE_LOCAL ";
            if (flags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)     flagStr << "HOST_VISIBLE ";
            if (flags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)    flagStr << "HOST_COHERENT ";
            if (flags & VK_MEMORY_PROPERTY_HOST_CACHED_BIT)      flagStr << "HOST_CACHED ";
            if (flags & VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT) flagStr << "LAZILY_ALLOCATED ";

            INK_LOG << "  Type " << memoryTypeIndex << " (" << flagStr.str() << "): "
                      << (memorySize / (1024 * 1024)) << " MB";
        }
    }

    INK_LOG << "=========================================";
}

u32 VkDeviceAllocator::findMemoryType(u32 typeFilter, VkMemoryPropertyFlags properties)
{
    for (u32 i = 0; i < memoryProperties.memoryTypeCount; i++)
    {
        if ((typeFilter & (1 << i)) &&
            (memoryProperties.memoryTypes[i].propertyFlags & properties) == properties)
            return i;
    }

    for (u32 i = 0; i < memoryProperties.memoryTypeCount; i++)
    {
        if ((typeFilter & (1 << i)) &&
            (memoryProperties.memoryTypes[i].propertyFlags & properties) == properties)
            return i;
    }

    INK_ERROR << "Failed to find suitable memory type with filter 0x" << std::hex << typeFilter
              << " and properties 0x" << properties << std::dec;
    return UINT32_MAX;
}

void VkDeviceAllocator::initializeBuddyAllocator(u32 memoryTypeIndex, VkDeviceSize blockSize)
{
    MemoryTypePool& pool = getOrCreateMemoryTypePool(memoryTypeIndex);

    if (!pool.buddyAllocator)
    {
        VkMemoryAllocateInfo allocInfo = {};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = blockSize;
        allocInfo.memoryTypeIndex = memoryTypeIndex;

        VkDeviceMemory memory = VK_NULL_HANDLE;
        VkResult result = vkAllocateMemory(device, &allocInfo, vkHostAllocator->getCallbacks(), &memory);

        if (result == VK_SUCCESS)
        {
            MemoryBlock block = {};
            block.memory = memory;
            block.size = blockSize;

            VkMemoryPropertyFlags memFlags = memoryProperties.memoryTypes[memoryTypeIndex].propertyFlags;
            block.canBeMapped = (memFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) != 0;

            pool.blocks.push_back(block);
            pool.totalSize += blockSize;

            VkDeviceSize minBlockSize = 64 * 1024;
            pool.buddyAllocator = std::make_unique<BuddyAllocator>(blockSize, minBlockSize);
        } else {
            INK_ERROR << "Failed to allocate memory for buddy allocator, result: " << result;
        }
    } else {

    }
}

VkResult VkDeviceAllocator::allocateNewBlock(u32 memoryTypeIndex, VkDeviceSize blockSize)
{
    MemoryTypePool& pool = getOrCreateMemoryTypePool(memoryTypeIndex);

    if (memoryTypeIndex < memoryProperties.memoryTypeCount)
    {
        u32 heapIndex = memoryProperties.memoryTypes[memoryTypeIndex].heapIndex;
        VkDeviceSize heapSize = memoryProperties.memoryHeaps[heapIndex].size;

        if (pool.totalSize + blockSize > heapSize)
        {
            INK_WARN << "Memory allocation may exceed heap size: "
                     << "Current: " << (pool.totalSize / (1024 * 1024)) << "MB, "
                     << "Adding: " << (blockSize / (1024 * 1024)) << "MB, "
                     << "Heap: " << (heapSize / (1024 * 1024)) << "MB";
        }
    }

    VkMemoryAllocateInfo allocInfo = {};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = blockSize;
    allocInfo.memoryTypeIndex = memoryTypeIndex;

    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkResult result = vkAllocateMemory(device, &allocInfo, vkHostAllocator->getCallbacks(), &memory);

    if (result != VK_SUCCESS)
    {
        INK_ERROR << "Failed to allocate device memory block, result: " << result
                  << ", size: " << (blockSize / (1024 * 1024)) << "MB, type: " << memoryTypeIndex;
        return result;
    }

    MemoryBlock block = {};
    block.memory = memory;
    block.size = blockSize;
    block.mappedAddress = nullptr;
    block.isMapped = false;

    VkMemoryPropertyFlags memFlags = memoryProperties.memoryTypes[memoryTypeIndex].propertyFlags;
    block.canBeMapped = (memFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) != 0;

    block.freeRanges.insert(0, blockSize);

    pool.blocks.push_back(block);
    pool.totalSize += blockSize;

    return VK_SUCCESS;
}

VkResult VkDeviceAllocator::findAndAllocateInBlock(
    u32 memoryTypeIndex,
    VkDeviceSize size,
    VkDeviceSize alignment,
    VkDeviceAllocation& allocation)
{
    MemoryTypePool* pool = nullptr;
    for (auto& p : memoryTypePools)
    {
        if (p.memoryTypeIndex == memoryTypeIndex)
        {
            pool = &p;
            break;
        }
    }

    if (!pool) {

        return VK_ERROR_INITIALIZATION_FAILED;
    }

    size_t blockIndex = 0;
    for (auto& block : pool->blocks)
    {
        VkDeviceSize offset = 0;
        VkDeviceSize padding = 0;

        if (findFreeRange(block, size, alignment, offset, padding))
        {
            VkDeviceSize alignedOffset = offset + padding;

            block.freeRanges.remove(offset);

            if (padding > 0)
                block.freeRanges.insert(offset, padding);

            VkDeviceSize remainingSize = block.freeRanges.offsetToSize[offset] - size - padding;
            if (remainingSize > 0)
                block.freeRanges.insert(alignedOffset + size, remainingSize);

            allocation.memory = block.memory;
            allocation.offset = alignedOffset;
            allocation.size = size;
            allocation.memoryTypeIndex = memoryTypeIndex;
            allocation.mappedData = nullptr;
            allocation.mappingState = AllocationMappingState::UNMAPPED;

            pool->usedSize += size;

            return VK_SUCCESS;
        }

        blockIndex++;
    }

    return VK_ERROR_OUT_OF_DEVICE_MEMORY;
}

bool VkDeviceAllocator::findFreeRange(
    MemoryBlock& block,
    VkDeviceSize size,
    VkDeviceSize alignment,
    VkDeviceSize& outOffset,
    VkDeviceSize& outPadding)
{
    if (block.freeRanges.empty())
        return false;

    VkDeviceSize foundOffset = 0;
    VkDeviceSize foundSize = 0;

    bool found = false;

    switch (allocationStrategy)
    {
        case AllocationStrategy::BEST_FIT:
        {
            found = block.freeRanges.findBestFit(size, foundOffset, foundSize);
            if (found)
            break;
        }
        case AllocationStrategy::FIRST_FIT:
        {
            found = block.freeRanges.findFirstFit(size, foundOffset, foundSize);
            if (found)
            break;
        }
        case AllocationStrategy::WORST_FIT:
        {
            found = block.freeRanges.findWorstFit(size, foundOffset, foundSize);
            if (found)
            break;
        }
    }

    if (!found)
        return false;

    VkDeviceSize alignedOffset = (foundOffset + alignment - 1) & ~(alignment - 1);
    VkDeviceSize padding = alignedOffset - foundOffset;

    if (foundSize < size + padding)
        return false;

    outOffset = foundOffset;
    outPadding = padding;
    return true;
}

MemoryTypePool& VkDeviceAllocator::getOrCreateMemoryTypePool(u32 memoryTypeIndex)
{
    for (auto& pool : memoryTypePools)
    {
        if (pool.memoryTypeIndex == memoryTypeIndex)
            return pool;
    }

    memoryTypePools.emplace_back();
    auto& newPool = memoryTypePools.back();

    newPool.memoryTypeIndex = memoryTypeIndex;
    newPool.properties = memoryProperties.memoryTypes[memoryTypeIndex].propertyFlags;
    newPool.totalSize = 0;
    newPool.usedSize = 0;

    if (useBuddyAllocatorForBuffers &&
        (newPool.properties & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) &&
        (newPool.properties & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) == 0)
    {
        initializeBuddyAllocator(memoryTypeIndex, defaultBlockSize);
    }

    return newPool;
}

bool VkDeviceAllocator::findBlockByMemory(VkDeviceMemory memory, MemoryBlock** outBlock)
{
    for (auto& pool : memoryTypePools)
    {
        for (auto& block : pool.blocks)
        {
            if (block.memory == memory)
            {
                *outBlock = &block;
                return true;
            }
        }
    }

    return false;
}

VkDeviceAllocation& VkDeviceAllocator::getAllocation(u32 allocationId)
{
    static VkDeviceAllocation invalidAllocation;

    if (threadSafetyMode != ThreadSafetyMode::NONE)
        std::lock_guard<std::mutex> lock(globalMutex);

    auto it = allocationMap.find(allocationId);
    if (it == allocationMap.end())
    {
        INK_ERROR << "Attempting to access invalid allocation ID: " << allocationId;
        return invalidAllocation;
    }

    return it->second;
}

void VkDeviceAllocator::processDeferredFrees(bool processAll)
{
    if (!deferFrees) {

        return;
    }

    if (threadSafetyMode != ThreadSafetyMode::NONE)
        std::lock_guard<std::mutex> lock(globalMutex);

    for (auto& pool : memoryTypePools)
    {
        if (pool.deferredFrees.empty())
            continue;

        if (processAll || pool.deferredFrees.size() >= deferredFreeLimit)
        {
            for (u32 allocationId : pool.deferredFrees)
            {
                auto it = allocationMap.find(allocationId);
                if (it != allocationMap.end()) {
                    VkDeviceAllocation allocation = it->second;

                    if (allocation.mappingState != AllocationMappingState::UNMAPPED) {

                        unmapMemory(allocation);
                    }

                    for (auto& block : pool.blocks) {
                        if (block.memory == allocation.memory) {
                            block.freeRanges.insert(allocation.offset, allocation.size);

                            if (block.mappedRegions.count(allocation.offset) > 0) {
                                block.mappedRegions.erase(allocation.offset);
                            }

                            pool.usedSize -= allocation.size;

                            allocationMap.erase(allocationId);

                            break;
                        }
                    }
                }
                else {
                    INK_WARN << "Deferred free for unknown allocation: " << allocationId;
                }
            }

            for (auto& block : pool.blocks) {
                block.freeRanges.coalesce();
            }
            pool.deferredFrees.clear();
        }
    }
}

VkResult VkDeviceAllocator::defragment(VkDeviceSize maxBytesToMove)
{
    if (!defragmentationEnabled)
    {
        INK_WARN << "Defragmentation is disabled";
        return VK_ERROR_FEATURE_NOT_PRESENT;
    }

    if (threadSafetyMode != ThreadSafetyMode::NONE)
        std::lock_guard<std::mutex> lock(globalMutex);

    processDeferredFrees(true);

    VkDeviceSize bytesMoved = 0;

    for (auto& pool : memoryTypePools)
    {
        if (pool.buddyAllocator)
            continue;

        size_t blockIndex = 0;
        for (auto& block : pool.blocks)
        {
            if (block.freeRanges.size() <= 1)
            {
                blockIndex++;
                continue;
            }

            std::vector<std::pair<VkDeviceSize, u32>> blockAllocations;
            for (const auto& pair : allocationMap)
            {
                const auto& alloc = pair.second;
                if (alloc.memory == block.memory)
                    blockAllocations.push_back({alloc.offset, alloc.allocationId});
            }

            std::sort(blockAllocations.begin(), blockAllocations.end());

            VkDeviceSize currentOffset = 0;

            for (const auto& pair : blockAllocations)
            {
                u32 allocationId = pair.second;
                auto& alloc = allocationMap[allocationId];

                if (alloc.offset > currentOffset)
                {
                    VkDeviceSize moveSize = alloc.size;
                    if (maxBytesToMove != VK_WHOLE_SIZE && bytesMoved + moveSize > maxBytesToMove)
                        break;

                    VkDeviceAllocation newAlloc;
                    newAlloc.memory = alloc.memory;
                    newAlloc.offset = currentOffset;
                    newAlloc.size = alloc.size;
                    newAlloc.memoryTypeIndex = alloc.memoryTypeIndex;
                    newAlloc.allocationId = alloc.allocationId;

                    void* srcData = nullptr;
                    // void* dstData = nullptr;

                    VkResult mapResult = mapMemory(alloc, 0, VK_WHOLE_SIZE, &srcData);
                    if (mapResult != VK_SUCCESS) {
                        INK_ERROR << "Failed to map source allocation for defragmentation";
                        continue;
                    }

                    block.freeRanges.remove(currentOffset);
                    block.freeRanges.insert(alloc.offset, alloc.size);

                    alloc.offset = currentOffset;
                    allocationMap[allocationId] = alloc;

                    currentOffset += alloc.size;
                    bytesMoved += moveSize;

                }
                else {
                    currentOffset = alloc.offset + alloc.size;
                }
            }


            block.freeRanges.coalesce();

            blockIndex++;
        }
    }

    return VK_SUCCESS;
}

void VkDeviceAllocator::cleanup()
{
    if (inShutdown)
        return;

    inShutdown = true;

    if (threadSafetyMode != ThreadSafetyMode::NONE) {

        std::lock_guard<std::mutex> lock(globalMutex);
    }

    processDeferredFrees(true);

    unmapAllMemory();

    std::vector<u32> allocationIds;
    for (const auto& pair : allocationMap)
        allocationIds.push_back(pair.first);

    for (u32 id : allocationIds)
    {
        if (allocationMap.count(id) > 0)
        {
            auto allocation = allocationMap[id];

            allocation.mappingState = AllocationMappingState::UNMAPPED;
            allocation.mappedData = nullptr;

            allocationMap.erase(id);
        }
    }

    for (auto& pool : memoryTypePools)
    {
        for (auto& block : pool.blocks)
        {
            if (block.memory != VK_NULL_HANDLE)
            {
                vkFreeMemory(device, block.memory, nullptr);
                block.memory = VK_NULL_HANDLE;
            }
        }

        if (pool.buddyAllocator)
            pool.buddyAllocator.reset();
    }

    memoryTypePools.clear();
    allocationMap.clear();
}

}
} // namespace aura3d
