#include "VkDeviceAllocator.h"

#include <algorithm>
#include <iostream>
#include <sstream>
#include <cassert>
#include <ink/ink.hpp>

namespace aura3d {

// Constructor
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
    // Get memory properties for later use
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memoryProperties);

    INK_DEBUG << "VkDeviceAllocator created with block size: "
              << (defaultBlockSize / (1024 * 1024)) << "MB, "
              << "defragmentation: " << (defragmentationEnabled ? "enabled" : "disabled") << ", "
              << "thread safety: " << (threadSafetyMode == ThreadSafetyMode::NONE ? "none" :
                                           (threadSafetyMode == ThreadSafetyMode::COARSE_GRAINED ? "coarse" : "fine"));
}

// Destructor
VkDeviceAllocator::~VkDeviceAllocator()
{
    if (threadSafetyMode != ThreadSafetyMode::NONE) {
        std::lock_guard<std::mutex> lock(globalMutex);
    }

    // Check for leaks before cleanup if tracking is enabled
    if (trackLeaks && !allocationMap.empty() && !inShutdown) {
        INK_WARN << "VkDeviceAllocator destructed with " << allocationMap.size()
        << " allocations still active!";
        // Print details of leaked allocations
        for (const auto& pair : allocationMap) {
            const auto& alloc = pair.second;
            INK_WARN << "Leaked allocation: id=" << alloc.allocationId
                         << ", size=" << alloc.size << " bytes"
                         << ", mapped=" << (alloc.mappedData != nullptr ? "yes" : "no");
        }
    }

    // Call cleanup to properly free everything
    if (!inShutdown) {
        cleanup();
    }

    INK_DEBUG << "VkDeviceAllocator destroyed";
}

// Helper for thread safety
void VkDeviceAllocator::acquireLock(u32 memoryTypeIndex, bool forWrite) {
    if (threadSafetyMode == ThreadSafetyMode::NONE) {
        return;
    } else if (threadSafetyMode == ThreadSafetyMode::COARSE_GRAINED) {
        globalMutex.lock();
    } else { // ThreadSafetyMode::FINE_GRAINED
        // Ensure we have enough mutexes
        if (memoryTypeIndex >= poolMutexes.size()) {
            std::lock_guard<std::mutex> lock(globalMutex);
            while (memoryTypeIndex >= poolMutexes.size()) {
                poolMutexes.push_back(std::make_unique<std::mutex>());
            }
        }

        // Always lock (we simplified to just use regular mutexes instead of shared_mutex)
        poolMutexes[memoryTypeIndex]->lock();
    }
}

// Helper for thread safety
void VkDeviceAllocator::releaseLock(u32 memoryTypeIndex, bool forWrite) {
    if (threadSafetyMode == ThreadSafetyMode::NONE) {
        return;
    } else if (threadSafetyMode == ThreadSafetyMode::COARSE_GRAINED) {
        globalMutex.unlock();
    } else { // ThreadSafetyMode::FINE_GRAINED
        poolMutexes[memoryTypeIndex]->unlock();
    }
}

// Core allocation method
VkResult VkDeviceAllocator::allocateMemory(
    const VkMemoryRequirements& memRequirements,
    VkMemoryPropertyFlags properties,
    VkDeviceAllocation& allocation)
{
    if (inShutdown) {
        INK_ERROR << "Attempting to allocate memory during shutdown";
        return VK_ERROR_DEVICE_LOST;
    }

    // Reset the allocation
    allocation.reset();

    u32 memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits, properties);
    if (memoryTypeIndex == UINT32_MAX) {
        INK_ERROR << "Failed to find suitable memory type";
        return VK_ERROR_FEATURE_NOT_PRESENT;
    }

    // Process any deferred frees before allocating
    if (deferFrees) {
        processDeferredFrees(false);
    }

    acquireLock(memoryTypeIndex, true);

    VkResult result = VK_ERROR_OUT_OF_DEVICE_MEMORY;

    // For large allocations, create a dedicated block directly
    if (memRequirements.size >= dedicatedAllocationThreshold) {
        INK_DEBUG << "Using dedicated allocation for large memory request: "
                  << (memRequirements.size / (1024 * 1024)) << "MB";

        // Allocate a dedicated block
        VkMemoryAllocateInfo allocInfo = {};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = memRequirements.size;
        allocInfo.memoryTypeIndex = memoryTypeIndex;

        VkDeviceMemory memory = VK_NULL_HANDLE;
        result = vkAllocateMemory(device, &allocInfo, vkHostAllocator->getCallbacks(), &memory);

        if (result == VK_SUCCESS) {
            // Create a new block to track this memory
            MemoryTypePool& pool = getOrCreateMemoryTypePool(memoryTypeIndex);
            MemoryBlock block = {};
            block.memory = memory;
            block.size = memRequirements.size;
            block.canBeMapped = (properties & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) != 0;

            // The entire block is used by this allocation
            allocation.memory = memory;
            allocation.offset = 0;
            allocation.size = memRequirements.size;
            allocation.memoryTypeIndex = memoryTypeIndex;
            allocation.allocationId = nextAllocationId++;

            // Update pool statistics
            pool.blocks.push_back(block);
            pool.totalSize += memRequirements.size;
            pool.usedSize += memRequirements.size;

            // Track the allocation
            allocationMap[allocation.allocationId] = allocation;
        }
    } else {
        INK_DEBUG << "Using buddy allocator for small memory request: "
                  << (memRequirements.size / (1024 * 1024)) << "MB";

        // Try buddy allocator for buffer memory if enabled
        if (useBuddyAllocatorForBuffers &&
            (properties & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) &&
            (properties & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) == 0) {

            result = allocateUsingBuddyAllocator(
                memoryTypeIndex,
                memRequirements.size,
                memRequirements.alignment,
                allocation
            );
        }

        // If buddy allocation failed or not applicable, use standard allocation
        if (result != VK_SUCCESS) {
            // Try to allocate from existing blocks
            result = findAndAllocateInBlock(
                memoryTypeIndex,
                memRequirements.size,
                memRequirements.alignment,
                allocation
            );

            // If no space in existing blocks, allocate a new block
            if (result != VK_SUCCESS) {
                // Calculate block size based on allocation size
                VkDeviceSize blockSize = defaultBlockSize;

                if (memRequirements.size > defaultBlockSize / 2) {
                    // For larger allocations, create a custom-sized block
                    blockSize = memRequirements.size * 2; // Some extra space for future allocations
                    INK_DEBUG << "Creating larger block of size " << (blockSize / (1024 * 1024))
                              << "MB for allocation of " << (memRequirements.size / (1024 * 1024)) << "MB";
                } else if (memRequirements.size < smallBlockSize / 2) {
                    // For smaller allocations, use smaller block size
                    blockSize = smallBlockSize;
                }

                result = allocateNewBlock(memoryTypeIndex, blockSize);

                if (result != VK_SUCCESS) {
                    INK_ERROR << "Failed to allocate new block, result: " << result;
                    releaseLock(memoryTypeIndex, true);
                    return result;
                }

                // Try allocation again with the new block
                result = findAndAllocateInBlock(
                    memoryTypeIndex,
                    memRequirements.size,
                    memRequirements.alignment,
                    allocation
                );
            }
        }

        if (result == VK_SUCCESS) {
            // Generate and assign ID (if not already assigned by buddy allocator)
            if (allocation.allocationId == 0) {
                allocation.allocationId = nextAllocationId++;
            }

            // Track the allocation
            allocationMap[allocation.allocationId] = allocation;
        }
    }

    releaseLock(memoryTypeIndex, true);
    return result;
}

// Allocate using buddy allocator
VkResult VkDeviceAllocator::allocateUsingBuddyAllocator(
    u32 memoryTypeIndex,
    VkDeviceSize size,
    VkDeviceSize alignment,
    VkDeviceAllocation& allocation)
{
    MemoryTypePool* pool = nullptr;
    for (auto& p : memoryTypePools) {
        if (p.memoryTypeIndex == memoryTypeIndex) {
            pool = &p;
            break;
        }
    }

    if (!pool || !pool->buddyAllocator) {
        // No pool or buddy allocator for this type
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    // Round up to the next power of 2 for buddy allocator
    VkDeviceSize paddedSize = size;
    if ((paddedSize & (paddedSize - 1)) != 0) {
        // Not a power of 2, round up
        paddedSize = 1;
        while (paddedSize < size) {
            paddedSize <<= 1;
        }
    }

    // Try to allocate from buddy allocator
    VkDeviceSize offset = 0;
    if (!pool->buddyAllocator->allocate(paddedSize, offset)) {
        // Failed to allocate from buddy allocator
        return VK_ERROR_OUT_OF_DEVICE_MEMORY;
    }

    // Find the block that contains this buddy allocation
    MemoryBlock* block = nullptr;
    for (auto& b : pool->blocks) {
        if (b.memory != VK_NULL_HANDLE) {
            block = &b;
            break;
        }
    }

    if (!block) {
        INK_ERROR << "No memory block found for buddy allocation";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    // Set up the allocation
    allocation.memory = block->memory;
    allocation.offset = offset;
    allocation.size = size; // Use the original size, not the padded size
    allocation.memoryTypeIndex = memoryTypeIndex;
    allocation.allocationId = nextAllocationId++;

    // Update pool statistics
    pool->usedSize += size;

    return VK_SUCCESS;
}

// Allocate memory for a buffer
VkResult VkDeviceAllocator::allocateMemoryForBuffer(
    VkBuffer buffer,
    VkMemoryPropertyFlags properties,
    VkDeviceAllocation& allocation)
{
    if (buffer == VK_NULL_HANDLE) {
        INK_ERROR << "Cannot allocate memory for null buffer";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(device, buffer, &memRequirements);
    return allocateMemory(memRequirements, properties, allocation);
}

// Allocate memory for an image
VkResult VkDeviceAllocator::allocateMemoryForImage(
    VkImage image,
    VkMemoryPropertyFlags properties,
    VkDeviceAllocation& allocation)
{
    if (image == VK_NULL_HANDLE) {
        INK_ERROR << "Cannot allocate memory for null image";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    VkMemoryRequirements memRequirements;
    vkGetImageMemoryRequirements(device, image, &memRequirements);
    return allocateMemory(memRequirements, properties, allocation);
}

// Free an allocation
void VkDeviceAllocator::freeMemory(VkDeviceAllocation& allocation)
{
    if (inShutdown) {
        return;  // Skip during shutdown
    }

    auto allocationId = allocation.allocationId;
    if (allocationId == 0) {
        INK_WARN << "Attempting to free invalid allocation with ID 0";
        return;
    }

    // If deferred frees are enabled, just add to the queue
    if (deferFrees && !inShutdown) {
        u32 memoryTypeIndex = allocation.memoryTypeIndex;
        acquireLock(memoryTypeIndex, true);

        for (auto& pool : memoryTypePools) {
            if (pool.memoryTypeIndex == memoryTypeIndex) {
                pool.deferredFrees.push_back(allocationId);

                // If we've reached the limit, process frees
                if (pool.deferredFrees.size() >= deferredFreeLimit) {
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
    if (it == allocationMap.end()) {
        INK_WARN << "Attempting to free unknown allocation: " << allocationId;
        releaseLock(memoryTypeIndex, true);
        return;
    }

    // Unmap memory first if it's mapped
    if (allocation.mappingState != AllocationMappingState::UNMAPPED) {
        if (!inShutdown) {  // Skip during shutdown as we'll unmap everything
            unmapMemory(allocation);
        }
    }

    // Find the pool for this memory type
    for (auto& pool : memoryTypePools) {
        if (pool.memoryTypeIndex == allocation.memoryTypeIndex) {
            // Check if this is a buddy-allocated block
            if (pool.buddyAllocator) {
                for (auto& block : pool.blocks) {
                    if (block.memory == allocation.memory) {
                        // Try to free from buddy allocator
                        pool.buddyAllocator->free(allocation.offset);

                        // Update used size
                        pool.usedSize -= allocation.size;

                        // Remove from tracking
                        allocationMap.erase(it);

                        // Clear the allocation
                        allocation.reset();

                        releaseLock(memoryTypeIndex, true);
                        return;
                    }
                }
            }

            // Find the block containing this allocation
            for (auto& block : pool.blocks) {
                if (block.memory == allocation.memory) {
                    // Check if this is a dedicated allocation (occupies the entire block)
                    if (allocation.offset == 0 && allocation.size == block.size) {
                        // Just free the entire block if it's dedicated
                        if (block.isMapped) {
                            vkUnmapMemory(device, block.memory);
                        }

                        vkFreeMemory(device, block.memory, nullptr);

                        // Update pool stats
                        pool.totalSize -= block.size;
                        pool.usedSize -= allocation.size;

                        // Remove the block
                        auto blockIt = std::find_if(pool.blocks.begin(), pool.blocks.end(),
                                                    [&block](const MemoryBlock& b) {
                                                        return b.memory == block.memory;
                                                    });

                        if (blockIt != pool.blocks.end()) {
                            pool.blocks.erase(blockIt);
                        }
                    } else {
                        // Add the freed range back to the free list
                        block.freeRanges.insert(allocation.offset, allocation.size);

                        // Remove from mapping tracking if needed
                        if (block.mappedRegions.count(allocation.offset) > 0) {
                            block.mappedRegions.erase(allocation.offset);
                        }

                        // Decrease used size in the pool
                        pool.usedSize -= allocation.size;

                        // Coalesce adjacent free chunks to reduce fragmentation
                        block.freeRanges.coalesce();
                    }

                    // Remove from tracking
                    allocationMap.erase(it);

                    // Clear the allocation
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

// Map memory
VkResult VkDeviceAllocator::mapMemory(
    VkDeviceAllocation& allocation,
    VkDeviceSize offset,
    VkDeviceSize size,
    void** ppData)
{
    if (inShutdown) {
        INK_WARN << "Attempting to map memory during shutdown";
        return VK_ERROR_DEVICE_LOST;
    }

    if (allocation.memory == VK_NULL_HANDLE) {
        INK_ERROR << "Attempting to map invalid allocation";
        return VK_ERROR_MEMORY_MAP_FAILED;
    }

    u32 memoryTypeIndex = allocation.memoryTypeIndex;
    acquireLock(memoryTypeIndex, true);

    // Check if this specific allocation is already mapped
    if (allocation.mappingState != AllocationMappingState::UNMAPPED) {
        // If this specific allocation is already mapped, return the existing pointer
        *ppData = allocation.mappedData;
        releaseLock(memoryTypeIndex, true);
        return VK_SUCCESS;
    }

    // Check if memory is host visible
    VkMemoryPropertyFlags memFlags = memoryProperties.memoryTypes[allocation.memoryTypeIndex].propertyFlags;
    if ((memFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) == 0) {
        INK_ERROR << "Attempting to map memory that is not host visible";
        releaseLock(memoryTypeIndex, true);
        return VK_ERROR_MEMORY_MAP_FAILED;
    }

    // If size is VK_WHOLE_SIZE, map the entire allocation
    if (size == VK_WHOLE_SIZE) {
        size = allocation.size - offset;
    }

    // Calculate the actual offset in the device memory
    VkDeviceSize actualOffset = allocation.offset + offset;

    // Find the memory block
    MemoryBlock* block = nullptr;
    if (!findBlockByMemory(allocation.memory, &block)) {
        INK_ERROR << "Failed to find memory block for mapping";
        releaseLock(memoryTypeIndex, true);
        return VK_ERROR_MEMORY_MAP_FAILED;
    }

    // Check if block is already mapped
    void* mappedData = nullptr;
    if (block->isMapped) {
        // Block is already mapped, just calculate the right pointer
        mappedData = static_cast<char*>(block->mappedAddress) + actualOffset;
    } else {
        // Map the memory if not already mapped
        VkResult result = vkMapMemory(device, allocation.memory, 0, block->size, 0, &block->mappedAddress);
        if (result != VK_SUCCESS) {
            INK_ERROR << "Failed to map memory: id=" << allocation.allocationId << ", error=" << result;
            releaseLock(memoryTypeIndex, true);
            return result;
        }

        block->isMapped = true;
        mappedData = static_cast<char*>(block->mappedAddress) + actualOffset;
    }

    // Ensure coherency if needed - fast path for coherent memory
    if ((memFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) == 0) {
        VkMappedMemoryRange range = {};
        range.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
        range.memory = allocation.memory;
        range.offset = actualOffset;
        range.size = size;
        vkInvalidateMappedMemoryRanges(device, 1, &range);
    }

    // Track this mapped region in the block
    block->mappedRegions[allocation.offset] = allocation.allocationId;

    // Update allocation state
    allocation.mappedData = mappedData;
    allocation.mappingState = AllocationMappingState::MAPPED;
    *ppData = mappedData;

    // Update the allocation in our map
    allocationMap[allocation.allocationId] = allocation;

    releaseLock(memoryTypeIndex, true);
    return VK_SUCCESS;
}

void VkDeviceAllocator::unmapMemory(VkDeviceAllocation& allocation)
{
    if (allocation.mappingState == AllocationMappingState::UNMAPPED) {
        return;
    }

    u32 memoryTypeIndex = allocation.memoryTypeIndex;
    acquireLock(memoryTypeIndex, true);

    // For persistently mapped memory during normal operation, don't unmap
    if (!inShutdown && allocation.mappingState == AllocationMappingState::PERSISTENTLY_MAPPED) {
        releaseLock(memoryTypeIndex, true);
        return;
    }

    // Find the block for this memory
    MemoryBlock* block = nullptr;
    if (!findBlockByMemory(allocation.memory, &block)) {
        INK_ERROR << "Failed to find memory block for unmapping";
        releaseLock(memoryTypeIndex, true);
        return;
    }

    // Ensure coherency if needed - flush the memory range
    VkMemoryPropertyFlags memFlags = memoryProperties.memoryTypes[allocation.memoryTypeIndex].propertyFlags;
    if ((memFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) == 0) {
        VkMappedMemoryRange range = {};
        range.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
        range.memory = allocation.memory;
        range.offset = allocation.offset;
        range.size = allocation.size;
        vkFlushMappedMemoryRanges(device, 1, &range);
    }

    // Update allocation state first
    allocation.mappedData = nullptr;
    allocation.mappingState = AllocationMappingState::UNMAPPED;

    // Remove from mapped regions
    if (block->mappedRegions.count(allocation.offset) > 0) {
        block->mappedRegions.erase(allocation.offset);
    }

    // Only unmap the block if there are no more mapped regions
    if (block->mappedRegions.empty() && block->isMapped) {
        vkUnmapMemory(device, allocation.memory);
        block->isMapped = false;
        block->mappedAddress = nullptr;
    }

    // Update the allocation in our map if not during shutdown
    if (!inShutdown && allocationMap.count(allocation.allocationId) > 0) {
        allocationMap[allocation.allocationId] = allocation;
    }

    releaseLock(memoryTypeIndex, true);
}

// Force unmap all memory - useful during shutdown
void VkDeviceAllocator::unmapAllMemory()
{
    INK_DEBUG << "Unmapping all memory";

    if (threadSafetyMode != ThreadSafetyMode::NONE) {
        std::lock_guard<std::mutex> lock(globalMutex);
    }

    // First, mark all allocations as unmapped
    for (auto& pair : allocationMap) {
        auto& alloc = pair.second;
        alloc.mappedData = nullptr;
        alloc.mappingState = AllocationMappingState::UNMAPPED;
    }

    // Then unmap all blocks that are mapped
    for (auto& pool : memoryTypePools) {
        for (auto& block : pool.blocks) {
            if (block.isMapped) {
                // Clear mapped regions first
                block.mappedRegions.clear();
                // Only unmap if actually mapped
                vkUnmapMemory(device, block.memory);
                block.isMapped = false;
                block.mappedAddress = nullptr;
            }
        }
    }
}

// Bind buffer to allocation
VkResult VkDeviceAllocator::bindBufferMemory(
    VkBuffer buffer,
    const VkDeviceAllocation& allocation,
    VkDeviceSize offsetInAllocation)
{
    if (allocation.memory == VK_NULL_HANDLE) {
        INK_ERROR << "Attempting to bind to null allocation";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    VkDeviceSize bindOffset = allocation.offset + offsetInAllocation;
    return vkBindBufferMemory(device, buffer, allocation.memory, bindOffset);
}

// Bind image to allocation
VkResult VkDeviceAllocator::bindImageMemory(
    VkImage image,
    const VkDeviceAllocation& allocation,
    VkDeviceSize offsetInAllocation)
{
    if (allocation.memory == VK_NULL_HANDLE) {
        INK_ERROR << "Attempting to bind to null allocation";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    VkDeviceSize bindOffset = allocation.offset + offsetInAllocation;
    return vkBindImageMemory(device, image, allocation.memory, bindOffset);
}

// Get memory stats
MemoryStats VkDeviceAllocator::getMemoryStats() const
{
    if (threadSafetyMode != ThreadSafetyMode::NONE) {
        std::lock_guard<std::mutex> lock(globalMutex);
    }

    MemoryStats stats = {};
    stats.totalSize = 0;
    stats.usedSize = 0;
    stats.allocationCount = static_cast<u32>(allocationMap.size());
    stats.blockCount = 0;
    stats.fragmentationIndex = 0.0f;
    stats.largestFreeBlock = 0;
    stats.totalFreeChunks = 0;

    // Calculate stats for each memory type
    for (const auto& pool : memoryTypePools) {
        stats.totalSize += pool.totalSize;
        stats.usedSize += pool.usedSize;
        stats.blockCount += static_cast<u32>(pool.blocks.size());
        stats.sizeByMemoryType.push_back(std::make_pair(pool.memoryTypeIndex, pool.usedSize));

        // Calculate fragmentation stats
        u32 totalChunks = 0;
        VkDeviceSize largestChunk = 0;

        for (const auto& block : pool.blocks) {
            VkDeviceSize blockLargestFree = 0;

            // Find largest free chunk and count total chunks
            for (const auto& pair : block.freeRanges.offsetToSize) {
                totalChunks++;
                if (pair.second > blockLargestFree) {
                    blockLargestFree = pair.second;
                }
            }

            if (blockLargestFree > largestChunk) {
                largestChunk = blockLargestFree;
            }
        }

        stats.totalFreeChunks += totalChunks;

        if (largestChunk > stats.largestFreeBlock) {
            stats.largestFreeBlock = largestChunk;
        }
    }

    // Calculate fragmentation index
    if (stats.totalSize > 0) {
        VkDeviceSize freeSize = stats.totalSize - stats.usedSize;
        if (freeSize > 0) {
            // Ratio of largest free block to total free space (0.0-1.0)
            // 1.0 means no fragmentation (one large free block)
            // 0.0 means complete fragmentation (lots of tiny blocks)
            stats.fragmentationIndex = 1.0f - (static_cast<f32>(stats.largestFreeBlock) / freeSize);
        }
    }

    return stats;
}

// Print memory stats
void VkDeviceAllocator::printMemoryStats() const
{
    MemoryStats stats = getMemoryStats();

    std::cout << "===== VkDeviceAllocator Memory Stats =====" << std::endl;
    std::cout << "Total allocated: " << (stats.totalSize / (1024 * 1024)) << " MB" << std::endl;
    std::cout << "Total used: " << (stats.usedSize / (1024 * 1024)) << " MB" << std::endl;
    std::cout << "Utilization: " << (stats.totalSize > 0 ? (100.0f * stats.usedSize / stats.totalSize) : 0.0f) << "%" << std::endl;
    std::cout << "Fragmentation: " << (stats.fragmentationIndex * 100.0f) << "%" << std::endl;
    std::cout << "Largest free block: " << (stats.largestFreeBlock / (1024 * 1024)) << " MB" << std::endl;
    std::cout << "Allocations: " << stats.allocationCount << std::endl;
    std::cout << "Memory blocks: " << stats.blockCount << std::endl;
    std::cout << "Free chunks: " << stats.totalFreeChunks << std::endl;

    // Memory type breakdown
    std::cout << "Memory by type:" << std::endl;
    for (const auto& typeStat : stats.sizeByMemoryType) {
        u32 memoryTypeIndex = typeStat.first;
        VkDeviceSize memorySize = typeStat.second;

        if (memoryTypeIndex < memoryProperties.memoryTypeCount) {
            VkMemoryPropertyFlags flags = memoryProperties.memoryTypes[memoryTypeIndex].propertyFlags;
            std::stringstream flagStr;

            if (flags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)     flagStr << "DEVICE_LOCAL ";
            if (flags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)     flagStr << "HOST_VISIBLE ";
            if (flags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)    flagStr << "HOST_COHERENT ";
            if (flags & VK_MEMORY_PROPERTY_HOST_CACHED_BIT)      flagStr << "HOST_CACHED ";
            if (flags & VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT) flagStr << "LAZILY_ALLOCATED ";

            std::cout << "  Type " << memoryTypeIndex << " (" << flagStr.str() << "): "
                      << (memorySize / (1024 * 1024)) << " MB" << std::endl;
        }
    }

    std::cout << "=========================================" << std::endl;
}

// Find memory type
u32 VkDeviceAllocator::findMemoryType(u32 typeFilter, VkMemoryPropertyFlags properties)
{
    // First, try to find an exact match
    for (u32 i = 0; i < memoryProperties.memoryTypeCount; i++) {
        if ((typeFilter & (1 << i)) &&
            (memoryProperties.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }

    // If no exact match, try to find a superset (memory with more capabilities)
    for (u32 i = 0; i < memoryProperties.memoryTypeCount; i++) {
        if ((typeFilter & (1 << i)) &&
            (memoryProperties.memoryTypes[i].propertyFlags & properties) == properties) {
            INK_DEBUG << "Using memory type " << i << " as fallback";
            return i;
        }
    }

    INK_ERROR << "Failed to find suitable memory type with filter " << typeFilter
               << " and properties " << properties;
    return UINT32_MAX;
}

// Initialize buddy allocator for a memory type
void VkDeviceAllocator::initializeBuddyAllocator(u32 memoryTypeIndex, VkDeviceSize blockSize)
{
    MemoryTypePool& pool = getOrCreateMemoryTypePool(memoryTypeIndex);

    // Only create if it doesn't exist yet
    if (!pool.buddyAllocator) {
        // Create a block for the buddy allocator
        VkMemoryAllocateInfo allocInfo = {};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = blockSize;
        allocInfo.memoryTypeIndex = memoryTypeIndex;

        VkDeviceMemory memory = VK_NULL_HANDLE;
        VkResult result = vkAllocateMemory(device, &allocInfo, vkHostAllocator->getCallbacks(), &memory);

        if (result == VK_SUCCESS) {
            // Create a new block
            MemoryBlock block = {};
            block.memory = memory;
            block.size = blockSize;

            // Check if memory can be mapped
            VkMemoryPropertyFlags memFlags = memoryProperties.memoryTypes[memoryTypeIndex].propertyFlags;
            block.canBeMapped = (memFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) != 0;

            // Add the block to the pool
            pool.blocks.push_back(block);
            pool.totalSize += blockSize;

            // Create the buddy allocator
            VkDeviceSize minBlockSize = 64 * 1024; // 64KB minimum block size
            pool.buddyAllocator = std::make_unique<BuddyAllocator>(blockSize, minBlockSize);

            INK_DEBUG << "Created buddy allocator for memory type " << memoryTypeIndex
                      << " with block size " << (blockSize / (1024 * 1024)) << "MB";
        } else {
            INK_ERROR << "Failed to allocate memory for buddy allocator, result: " << result;
        }
    }
}

// Allocate a new memory block
VkResult VkDeviceAllocator::allocateNewBlock(u32 memoryTypeIndex, VkDeviceSize blockSize)
{
    MemoryTypePool& pool = getOrCreateMemoryTypePool(memoryTypeIndex);

    // Check device memory limits
    if (memoryTypeIndex < memoryProperties.memoryTypeCount) {
        u32 heapIndex = memoryProperties.memoryTypes[memoryTypeIndex].heapIndex;
        VkDeviceSize heapSize = memoryProperties.memoryHeaps[heapIndex].size;

        if (pool.totalSize + blockSize > heapSize) {
            INK_WARN << "Memory allocation may exceed heap size: "
                         << "Current: " << (pool.totalSize / (1024 * 1024)) << "MB, "
                         << "Adding: " << (blockSize / (1024 * 1024)) << "MB, "
                         << "Heap: " << (heapSize / (1024 * 1024)) << "MB";
            // Continue anyway - the allocation might still succeed depending on global memory usage
        }
    }

    // Create the allocation
    VkMemoryAllocateInfo allocInfo = {};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = blockSize;
    allocInfo.memoryTypeIndex = memoryTypeIndex;

    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkResult result = vkAllocateMemory(device, &allocInfo, vkHostAllocator->getCallbacks(), &memory);

    if (result != VK_SUCCESS) {
        INK_ERROR << "Failed to allocate device memory block, result: " << result
                   << ", size: " << (blockSize / (1024 * 1024)) << "MB, type: " << memoryTypeIndex;
        return result;
    }

    // Create a new block
    MemoryBlock block = {};
    block.memory = memory;
    block.size = blockSize;
    block.mappedAddress = nullptr;
    block.isMapped = false;

    // Check if memory can be mapped
    VkMemoryPropertyFlags memFlags = memoryProperties.memoryTypes[memoryTypeIndex].propertyFlags;
    block.canBeMapped = (memFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) != 0;

    // Add a single free range representing the entire block
    block.freeRanges.insert(0, blockSize);

    // Add the block to the pool
    pool.blocks.push_back(block);
    pool.totalSize += blockSize;

    return VK_SUCCESS;
}

// Find space in a block and allocate from it
VkResult VkDeviceAllocator::findAndAllocateInBlock(
    u32 memoryTypeIndex,
    VkDeviceSize size,
    VkDeviceSize alignment,
    VkDeviceAllocation& allocation)
{
    // Get the memory pool for this type
    MemoryTypePool* pool = nullptr;
    for (auto& p : memoryTypePools) {
        if (p.memoryTypeIndex == memoryTypeIndex) {
            pool = &p;
            break;
        }
    }

    if (!pool) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    // For each block in the pool
    for (auto& block : pool->blocks) {
        VkDeviceSize offset = 0;
        VkDeviceSize padding = 0;

        // Find a free range that fits our requirements
        if (findFreeRange(block, size, alignment, offset, padding)) {
            // Allocate from this range
            VkDeviceSize alignedOffset = offset + padding;

            // Remove the allocated range from the free list
            block.freeRanges.remove(offset);

            // If there's padding at the beginning, add it as a free range
            if (padding > 0) {
                block.freeRanges.insert(offset, padding);
            }

            // If there's space after the allocation, add it as a free range
            VkDeviceSize remainingSize = block.freeRanges.offsetToSize[offset] - size - padding;
            if (remainingSize > 0) {
                block.freeRanges.insert(alignedOffset + size, remainingSize);
            }

            // Update the allocation info
            allocation.memory = block.memory;
            allocation.offset = alignedOffset;
            allocation.size = size;
            allocation.memoryTypeIndex = memoryTypeIndex;
            allocation.mappedData = nullptr;
            allocation.mappingState = AllocationMappingState::UNMAPPED;

            // Update pool stats
            pool->usedSize += size;

            return VK_SUCCESS;
        }
    }

    // No suitable space found
    return VK_ERROR_OUT_OF_DEVICE_MEMORY;
}

// Find a free range using the configured allocation strategy
bool VkDeviceAllocator::findFreeRange(
    MemoryBlock& block,
    VkDeviceSize size,
    VkDeviceSize alignment,
    VkDeviceSize& outOffset,
    VkDeviceSize& outPadding)
{
    if (block.freeRanges.empty()) {
        return false;
    }

    VkDeviceSize foundOffset = 0;
    VkDeviceSize foundSize = 0;

    // Choose allocation strategy
    bool found = false;

    switch (allocationStrategy) {
    case AllocationStrategy::BEST_FIT: {
        found = block.freeRanges.findBestFit(size, foundOffset, foundSize);
        break;
    }
    case AllocationStrategy::FIRST_FIT: {
        found = block.freeRanges.findFirstFit(size, foundOffset, foundSize);
        break;
    }
    case AllocationStrategy::WORST_FIT: {
        found = block.freeRanges.findWorstFit(size, foundOffset, foundSize);
        break;
    }
    }

    if (!found) {
        return false;
    }

    // Calculate alignment
    VkDeviceSize alignedOffset = (foundOffset + alignment - 1) & ~(alignment - 1);
    VkDeviceSize padding = alignedOffset - foundOffset;

    // Check if the aligned range still fits
    if (foundSize < size + padding) {
        return false;
    }

    outOffset = foundOffset;
    outPadding = padding;
    return true;
}

// Get or create a memory type pool
MemoryTypePool& VkDeviceAllocator::getOrCreateMemoryTypePool(u32 memoryTypeIndex)
{
    // Look for existing pool
    for (auto& pool : memoryTypePools) {
        if (pool.memoryTypeIndex == memoryTypeIndex) {
            return pool;
        }
    }

    // Create new pool - use emplace_back with in-place construction to avoid copy/move
    memoryTypePools.emplace_back();
    auto& newPool = memoryTypePools.back();

    // Initialize the new pool
    newPool.memoryTypeIndex = memoryTypeIndex;
    newPool.properties = memoryProperties.memoryTypes[memoryTypeIndex].propertyFlags;
    newPool.totalSize = 0;
    newPool.usedSize = 0;

    // Add buddy allocator if this is a device local buffer memory type
    if (useBuddyAllocatorForBuffers &&
        (newPool.properties & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) &&
        (newPool.properties & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) == 0) {
        initializeBuddyAllocator(memoryTypeIndex, defaultBlockSize);
    }

    return newPool;
}

// Find block by memory handle
bool VkDeviceAllocator::findBlockByMemory(VkDeviceMemory memory, MemoryBlock** outBlock)
{
    for (auto& pool : memoryTypePools) {
        for (auto& block : pool.blocks) {
            if (block.memory == memory) {
                *outBlock = &block;
                return true;
            }
        }
    }
    return false;
}

// Get allocation by ID
VkDeviceAllocation& VkDeviceAllocator::getAllocation(u32 allocationId)
{
    static VkDeviceAllocation invalidAllocation;

    if (threadSafetyMode != ThreadSafetyMode::NONE) {
        std::lock_guard<std::mutex> lock(globalMutex);
    }

    auto it = allocationMap.find(allocationId);
    if (it == allocationMap.end()) {
        INK_ERROR << "Attempting to access invalid allocation ID: " << allocationId;
        return invalidAllocation;
    }

    return it->second;
}

// Process deferred frees
void VkDeviceAllocator::processDeferredFrees(bool processAll)
{
    if (!deferFrees) {
        return;
    }

    if (threadSafetyMode != ThreadSafetyMode::NONE) {
        std::lock_guard<std::mutex> lock(globalMutex);
    }

    for (auto& pool : memoryTypePools) {
        if (pool.deferredFrees.empty()) {
            continue;
        }

        if (processAll || pool.deferredFrees.size() >= deferredFreeLimit) {
            // Process all deferred frees for this pool
            for (u32 allocationId : pool.deferredFrees) {
                auto it = allocationMap.find(allocationId);
                if (it != allocationMap.end()) {
                    VkDeviceAllocation allocation = it->second;

                    // Unmap if mapped
                    if (allocation.mappingState != AllocationMappingState::UNMAPPED) {
                        unmapMemory(allocation);
                    }

                    // Process the actual free
                    for (auto& block : pool.blocks) {
                        if (block.memory == allocation.memory) {
                            // Add the freed range back to the free list
                            block.freeRanges.insert(allocation.offset, allocation.size);

                            // Remove from mapping tracking if needed
                            if (block.mappedRegions.count(allocation.offset) > 0) {
                                block.mappedRegions.erase(allocation.offset);
                            }

                            // Decrease used size in the pool
                            pool.usedSize -= allocation.size;

                            // Remove from tracking
                            allocationMap.erase(allocationId);

                            break;
                        }
                    }
                }
            }

            // Coalesce free blocks in each memory block to reduce fragmentation
            for (auto& block : pool.blocks) {
                block.freeRanges.coalesce();
            }

            pool.deferredFrees.clear();
        }
    }
}

// Defragment memory to reduce fragmentation
VkResult VkDeviceAllocator::defragment(VkDeviceSize maxBytesToMove)
{
    if (!defragmentationEnabled) {
        INK_WARN << "Defragmentation is disabled";
        return VK_ERROR_FEATURE_NOT_PRESENT;
    }

    if (threadSafetyMode != ThreadSafetyMode::NONE) {
        std::lock_guard<std::mutex> lock(globalMutex);
    }

    // Process any deferred frees first
    processDeferredFrees(true);

    INK_DEBUG << "Starting memory defragmentation, max bytes to move: "
              << (maxBytesToMove == VK_WHOLE_SIZE ? "unlimited" : std::to_string(maxBytesToMove / (1024 * 1024)) + "MB");

    VkDeviceSize bytesMoved = 0;

    // Analyze fragmentation in each pool
    for (auto& pool : memoryTypePools) {
        // Skip pools with buddy allocators
        if (pool.buddyAllocator) {
            continue;
        }

        for (auto& block : pool.blocks) {
            // Skip if no fragmentation (block has at most one free chunk)
            if (block.freeRanges.size() <= 1) {
                continue;
            }

            // Get all allocations in this block
            std::vector<std::pair<VkDeviceSize, u32>> blockAllocations;
            for (const auto& pair : allocationMap) {
                const auto& alloc = pair.second;
                if (alloc.memory == block.memory) {
                    blockAllocations.push_back({alloc.offset, alloc.allocationId});
                }
            }

            // Sort allocations by offset
            std::sort(blockAllocations.begin(), blockAllocations.end());

            // Compact allocations by moving them to the beginning of the block
            VkDeviceSize currentOffset = 0;

            for (const auto& pair : blockAllocations) {
                u32 allocationId = pair.second;
                auto& alloc = allocationMap[allocationId];

                if (alloc.offset > currentOffset) {
                    // This allocation needs to be moved
                    VkDeviceSize moveSize = alloc.size;

                    // Check if we've hit the limit
                    if (maxBytesToMove != VK_WHOLE_SIZE && bytesMoved + moveSize > maxBytesToMove) {
                        INK_DEBUG << "Defragmentation reached byte limit, stopping";
                        break;
                    }

                    // Create a new allocation at the beginning
                    VkDeviceAllocation newAlloc;
                    newAlloc.memory = alloc.memory;
                    newAlloc.offset = currentOffset;
                    newAlloc.size = alloc.size;
                    newAlloc.memoryTypeIndex = alloc.memoryTypeIndex;
                    newAlloc.allocationId = alloc.allocationId;

                    // Copy the data
                    void* srcData = nullptr;
                    void* dstData = nullptr;

                    VkResult mapResult = mapMemory(alloc, 0, VK_WHOLE_SIZE, &srcData);
                    if (mapResult != VK_SUCCESS) {
                        INK_ERROR << "Failed to map source allocation for defragmentation";
                        continue;
                    }

                    // Update the free range info
                    block.freeRanges.remove(currentOffset);
                    block.freeRanges.insert(alloc.offset, alloc.size);

                    // Update the allocation in the map
                    alloc.offset = currentOffset;
                    allocationMap[allocationId] = alloc;

                    // Move to the next position
                    currentOffset += alloc.size;
                    bytesMoved += moveSize;
                } else {
                    // Already at the right position
                    currentOffset = alloc.offset + alloc.size;
                }
            }

            // Coalesce free chunks after defragmentation
            block.freeRanges.coalesce();
        }
    }

    INK_DEBUG << "Defragmentation complete, moved " << (bytesMoved / (1024 * 1024)) << "MB of data";
    return VK_SUCCESS;
}

void VkDeviceAllocator::cleanup()
{
    if (inShutdown) {
        return;  // Prevent recursive cleanup
    }

    inShutdown = true;
    INK_DEBUG << "Starting VkDeviceAllocator cleanup";

    if (threadSafetyMode != ThreadSafetyMode::NONE) {
        std::lock_guard<std::mutex> lock(globalMutex);
    }

    // Process any deferred frees
    processDeferredFrees(true);

    // First, unmap all memory to avoid Vulkan validation errors
    unmapAllMemory();

    // Create a copy of all allocations to avoid iterator invalidation
    std::vector<u32> allocationIds;
    for (const auto& pair : allocationMap) {
        allocationIds.push_back(pair.first);
    }

    // Free all allocations
    for (u32 id : allocationIds) {
        if (allocationMap.count(id) > 0) {
            auto allocation = allocationMap[id];

            // Skip actual unmapping since we did that in unmapAllMemory
            allocation.mappingState = AllocationMappingState::UNMAPPED;
            allocation.mappedData = nullptr;

            // Remove from tracking
            allocationMap.erase(id);
        }
    }

    // Now free all memory blocks
    for (auto& pool : memoryTypePools) {
        for (auto& block : pool.blocks) {
            if (block.memory != VK_NULL_HANDLE) {
                vkFreeMemory(device, block.memory, nullptr);
                block.memory = VK_NULL_HANDLE;
            }
        }

        // Clear buddy allocator if present
        if (pool.buddyAllocator) {
            pool.buddyAllocator.reset();
        }
    }

    // Clear all data structures
    memoryTypePools.clear();
    allocationMap.clear();

    INK_DEBUG << "VkDeviceAllocator cleanup completed";
}

} // namespace aura3d
