#include "VkDeviceAllocator.h"

#include <AuraException/AuraException.h>

#include <iostream>
#include <algorithm>
#include <sstream>
#include <plog/Log.h>

namespace aura3d {

// Initialize static members
std::unique_ptr<VkDeviceAllocator> VkDeviceAllocator::instance = nullptr;
std::once_flag VkDeviceAllocator::initInstanceFlag;

// Initialize the singleton
void VkDeviceAllocator::initialize(const VkDeviceAllocatorCreateInfo& createInfo) {
    std::call_once(initInstanceFlag, [&createInfo]() {
        instance = std::unique_ptr<VkDeviceAllocator>(new VkDeviceAllocator(createInfo));
    });
}

// Get the singleton instance
VkDeviceAllocator& VkDeviceAllocator::getInstance() {
    if (!instance) {
        throw AuraException("VkDeviceAllocator has not been initialized. Call initialize() first.");
    }
    return *instance;
}

// Destroy the singleton instance
void VkDeviceAllocator::destroy() {
    if (instance) {
        PLOG_INFO << "Destroying VkDeviceAllocator singleton";
        instance.reset();
    }
}

// Constructor
VkDeviceAllocator::VkDeviceAllocator(const VkDeviceAllocatorCreateInfo& createInfo)
    : physicalDevice(createInfo.physicalDevice),
    device(createInfo.device),
    defaultBlockSize(createInfo.blockSize),
    defragmentationEnabled(createInfo.enableDefragmentation),
    nextAllocationId(1) {

    // Get memory properties for later use
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memoryProperties);

    PLOG_INFO << "VkDeviceAllocator created with block size: "
              << (defaultBlockSize / (1024 * 1024)) << "MB, "
              << "defragmentation: " << (defragmentationEnabled ? "enabled" : "disabled");
}

// Destructor
VkDeviceAllocator::~VkDeviceAllocator() {
    std::lock_guard<std::mutex> lock(allocationMutex);

    if (!allocationMap.empty()) {
        PLOG_WARNING << "VkDeviceAllocator destructed with " << allocationMap.size()
        << " allocations still active!";
    }

    // Free all memory blocks
    for (auto& pool : memoryTypePools) {
        for (auto& block : pool.blocks) {
            if (block.mappedAddress != nullptr) {
                vkUnmapMemory(device, block.memory);
            }
            vkFreeMemory(device, block.memory, nullptr);
        }
    }

    PLOG_INFO << "VkDeviceAllocator destroyed";
}

// Core allocation method
VkResult VkDeviceAllocator::allocateMemory(
    const VkMemoryRequirements& memRequirements,
    VkMemoryPropertyFlags properties,
    VkDeviceAllocation& allocation) {

    std::lock_guard<std::mutex> lock(allocationMutex);

    uint32_t memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits, properties);
    if (memoryTypeIndex == UINT32_MAX) {
        PLOG_ERROR << "Failed to find suitable memory type";
        return VK_ERROR_FEATURE_NOT_PRESENT;
    }

    // Try to allocate from existing blocks
    VkResult result = findAndAllocateInBlock(
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
            // For large allocations, create a dedicated block
            blockSize = memRequirements.size;
            PLOG_INFO << "Creating dedicated block of size " << (blockSize / (1024 * 1024))
                      << "MB for large allocation";
        }

        result = allocateNewBlock(memoryTypeIndex, blockSize);
        if (result != VK_SUCCESS) {
            PLOG_ERROR << "Failed to allocate new block, result: " << result;
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

    if (result == VK_SUCCESS) {
        // Generate and assign ID
        allocation.allocationId = nextAllocationId++;

        // Track the allocation
        allocationMap[allocation.allocationId] = allocation;

        PLOG_DEBUG << "Allocated memory: id=" << allocation.allocationId
                   << ", size=" << allocation.size
                   << ", type=" << allocation.memoryTypeIndex
                   << ", offset=" << allocation.offset;
    }

    return result;
}

// Allocate memory for a buffer
VkResult VkDeviceAllocator::allocateMemoryForBuffer(
    VkBuffer buffer,
    VkMemoryPropertyFlags properties,
    VkDeviceAllocation& allocation)
{

    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(device, buffer, &memRequirements);

    return allocateMemory(memRequirements, properties, allocation);
}

// Allocate memory for an image
VkResult VkDeviceAllocator::allocateMemoryForImage(
    VkImage image,
    VkMemoryPropertyFlags properties,
    VkDeviceAllocation& allocation) {

    VkMemoryRequirements memRequirements;
    vkGetImageMemoryRequirements(device, image, &memRequirements);

    return allocateMemory(memRequirements, properties, allocation);
}

// Free an allocation
void VkDeviceAllocator::freeMemory(VkDeviceAllocation& allocation) {
    std::lock_guard<std::mutex> lock(allocationMutex);

    auto it = allocationMap.find(allocation.allocationId);
    if (it == allocationMap.end()) {
        PLOG_WARNING << "Attempting to free unknown allocation: " << allocation.allocationId;
        return;
    }

    // Find the pool for this memory type
    for (auto& pool : memoryTypePools) {
        if (pool.memoryTypeIndex == allocation.memoryTypeIndex) {
            // Find the block containing this allocation
            for (auto& block : pool.blocks) {
                if (block.memory == allocation.memory) {
                    // Add the freed range back to the free list
                    block.freeList.push_back(MemoryChunk(allocation.offset, allocation.size));

                    // Decrease used size in the pool
                    pool.usedSize -= allocation.size;

                    PLOG_DEBUG << "Freed memory: id=" << allocation.allocationId
                               << ", size=" << allocation.size
                               << ", type=" << allocation.memoryTypeIndex
                               << ", offset=" << allocation.offset;

                    // Coalesce adjacent free chunks to reduce fragmentation
                    if (block.freeList.size() > 1) {
                        // Sort chunks by offset
                        std::sort(block.freeList.begin(), block.freeList.end());

                        // Merge adjacent chunks
                        for (size_t i = 0; i < block.freeList.size() - 1;) {
                            MemoryChunk& current = block.freeList[i];
                            MemoryChunk& next = block.freeList[i + 1];

                            // If current chunk extends to the start of next chunk
                            if (current.offset + current.size == next.offset) {
                                // Merge them
                                current.size += next.size;
                                // Remove the next chunk
                                block.freeList.erase(block.freeList.begin() + i + 1);
                                // Don't increment i, check the new "next" chunk
                            } else {
                                // Move to next pair
                                i++;
                            }
                        }

                        PLOG_DEBUG << "After coalescing, block has " << block.freeList.size()
                                   << " free chunks";
                    }

                    // Remove from tracking
                    allocationMap.erase(it);

                    // Clear the allocation
                    allocation.memory = VK_NULL_HANDLE;
                    allocation.mappedData = nullptr;

                    return;
                }
            }
        }
    }

    PLOG_ERROR << "Failed to find memory block for allocation: " << allocation.allocationId;
}

// Map memory
VkResult VkDeviceAllocator::mapMemory(
    VkDeviceAllocation& allocation,
    VkDeviceSize offset,
    VkDeviceSize size,
    void** ppData) {

    // Check if memory is host visible
    VkMemoryPropertyFlags memFlags = memoryProperties.memoryTypes[allocation.memoryTypeIndex].propertyFlags;
    if ((memFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) == 0) {
        PLOG_ERROR << "Attempting to map memory that is not host visible";
        return VK_ERROR_MEMORY_MAP_FAILED;
    }

    // If size is VK_WHOLE_SIZE, map the entire allocation
    if (size == VK_WHOLE_SIZE) {
        size = allocation.size - offset;
    }

    // Calculate the actual offset in the device memory
    VkDeviceSize actualOffset = allocation.offset + offset;

    // Map the memory
    void* mappedData = nullptr;
    VkResult result = vkMapMemory(device, allocation.memory, actualOffset, size, 0, &mappedData);

    if (result == VK_SUCCESS) {
        allocation.mappedData = mappedData;
        *ppData = mappedData;

        // If memory is not host coherent, we'll need to flush/invalidate explicitly
        if ((memFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) == 0) {
            PLOG_DEBUG << "Mapped non-coherent memory, explicit flushes required";
        }
    }

    return result;
}

// Unmap memory
void VkDeviceAllocator::unmapMemory(VkDeviceAllocation& allocation) {
    if (allocation.mappedData != nullptr) {
        vkUnmapMemory(device, allocation.memory);
        allocation.mappedData = nullptr;

        PLOG_DEBUG << "Unmapped memory: id=" << allocation.allocationId;
    }
}

// Bind buffer to allocation
VkResult VkDeviceAllocator::bindBufferMemory(
    VkBuffer buffer,
    const VkDeviceAllocation& allocation,
    VkDeviceSize offsetInAllocation) {

    if (allocation.memory == VK_NULL_HANDLE) {
        PLOG_ERROR << "Attempting to bind to null allocation";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    VkDeviceSize bindOffset = allocation.offset + offsetInAllocation;
    VkResult result = vkBindBufferMemory(device, buffer, allocation.memory, bindOffset);

    if (result == VK_SUCCESS) {
        PLOG_DEBUG << "Bound buffer to allocation: id=" << allocation.allocationId
                   << ", offset=" << bindOffset;
    }

    return result;
}

// Bind image to allocation
VkResult VkDeviceAllocator::bindImageMemory(
    VkImage image,
    const VkDeviceAllocation& allocation,
    VkDeviceSize offsetInAllocation) {

    if (allocation.memory == VK_NULL_HANDLE) {
        PLOG_ERROR << "Attempting to bind to null allocation";
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    VkDeviceSize bindOffset = allocation.offset + offsetInAllocation;
    VkResult result = vkBindImageMemory(device, image, allocation.memory, bindOffset);

    if (result == VK_SUCCESS) {
        PLOG_DEBUG << "Bound image to allocation: id=" << allocation.allocationId
                   << ", offset=" << bindOffset;
    }

    return result;
}

// Get memory stats
VkDeviceAllocator::MemoryStats VkDeviceAllocator::getMemoryStats() const {
    std::lock_guard<std::mutex> lock(allocationMutex);

    MemoryStats stats;
    stats.totalSize = 0;
    stats.usedSize = 0;
    stats.allocationCount = static_cast<uint32_t>(allocationMap.size());
    stats.blockCount = 0;

    // Calculate stats for each memory type
    for (const auto& pool : memoryTypePools) {
        stats.totalSize += pool.totalSize;
        stats.usedSize += pool.usedSize;
        stats.blockCount += static_cast<uint32_t>(pool.blocks.size());

        stats.sizeByMemoryType.push_back(std::make_pair(pool.memoryTypeIndex, pool.usedSize));
    }

    return stats;
}

// Print memory stats
void VkDeviceAllocator::printMemoryStats() const {
    MemoryStats stats = getMemoryStats();

    std::cout << "===== VkDeviceAllocator Memory Stats =====" << std::endl;
    std::cout << "Total allocated: " << (stats.totalSize / (1024 * 1024)) << " MB" << std::endl;
    std::cout << "Total used: " << (stats.usedSize / (1024 * 1024)) << " MB" << std::endl;
    std::cout << "Utilization: " << (stats.totalSize > 0 ? (100.0f * stats.usedSize / stats.totalSize) : 0.0f) << "%" << std::endl;
    std::cout << "Allocations: " << stats.allocationCount << std::endl;
    std::cout << "Memory blocks: " << stats.blockCount << std::endl;

    // Memory type breakdown
    std::cout << "Memory by type:" << std::endl;
    for (const auto& typeStat : stats.sizeByMemoryType) {
        uint32_t memoryTypeIndex = typeStat.first;
        VkDeviceSize memorySize = typeStat.second;

        if (memoryTypeIndex < memoryProperties.memoryTypeCount) {
            VkMemoryPropertyFlags flags = memoryProperties.memoryTypes[memoryTypeIndex].propertyFlags;

            std::stringstream flagStr;
            if (flags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) flagStr << "DEVICE_LOCAL ";
            if (flags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) flagStr << "HOST_VISIBLE ";
            if (flags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) flagStr << "HOST_COHERENT ";
            if (flags & VK_MEMORY_PROPERTY_HOST_CACHED_BIT) flagStr << "HOST_CACHED ";
            if (flags & VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT) flagStr << "LAZILY_ALLOCATED ";

            std::cout << "  Type " << memoryTypeIndex << " (" << flagStr.str() << "): "
                      << (memorySize / (1024 * 1024)) << " MB" << std::endl;
        }
    }

    // Fragmentation analysis
    std::cout << "Fragmentation analysis:" << std::endl;
    for (const auto& pool : memoryTypePools) {
        uint32_t totalChunks = 0;
        VkDeviceSize largestChunk = 0;

        for (const auto& block : pool.blocks) {
            totalChunks += static_cast<uint32_t>(block.freeList.size());

            for (const auto& chunk : block.freeList) {
                largestChunk = std::max(largestChunk, chunk.size);
            }
        }

        std::cout << "  Type " << pool.memoryTypeIndex << ": "
                  << totalChunks << " free chunks, largest: "
                  << (largestChunk / 1024) << " KB" << std::endl;
    }

    std::cout << "=========================================" << std::endl;
}

// Find memory type
uint32_t VkDeviceAllocator::findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) {
    for (uint32_t i = 0; i < memoryProperties.memoryTypeCount; i++) {
        if ((typeFilter & (1 << i)) &&
            (memoryProperties.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }

    PLOG_ERROR << "Failed to find suitable memory type with filter " << typeFilter
               << " and properties " << properties;
    return UINT32_MAX;
}

// Allocate a new memory block
VkResult VkDeviceAllocator::allocateNewBlock(uint32_t memoryTypeIndex, VkDeviceSize blockSize) {
    MemoryTypePool& pool = getOrCreateMemoryTypePool(memoryTypeIndex);

    // Check device memory limits
    VkPhysicalDeviceMemoryProperties memProps;
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProps);

    if (memoryTypeIndex < memProps.memoryTypeCount) {
        uint32_t heapIndex = memProps.memoryTypes[memoryTypeIndex].heapIndex;
        VkDeviceSize heapSize = memProps.memoryHeaps[heapIndex].size;

        if (pool.totalSize + blockSize > heapSize) {
            PLOG_WARNING << "Memory allocation may exceed heap size: "
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
    VkResult result = vkAllocateMemory(device, &allocInfo, nullptr, &memory);

    if (result != VK_SUCCESS) {
        PLOG_ERROR << "Failed to allocate device memory block, result: " << result
                   << ", size: " << (blockSize / (1024 * 1024)) << "MB, type: " << memoryTypeIndex;
        return result;
    }

    // Create a new block
    MemoryBlock block = {};
    block.memory = memory;
    block.size = blockSize;
    block.mappedAddress = nullptr;

    // Check if memory can be mapped
    VkMemoryPropertyFlags memFlags = memProps.memoryTypes[memoryTypeIndex].propertyFlags;
    block.canBeMapped = (memFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) != 0;

    // Add a single free chunk representing the entire block
    block.freeList.push_back(MemoryChunk(0, blockSize));

    // Add the block to the pool
    pool.blocks.push_back(block);
    pool.totalSize += blockSize;

    PLOG_INFO << "Allocated new memory block: size=" << (blockSize / (1024 * 1024))
              << "MB, type=" << memoryTypeIndex
              << ", is_mappable=" << (block.canBeMapped ? "yes" : "no");

    return VK_SUCCESS;
}

// Find space in a block and allocate from it
VkResult VkDeviceAllocator::findAndAllocateInBlock(
    uint32_t memoryTypeIndex,
    VkDeviceSize size,
    VkDeviceSize alignment,
    VkDeviceAllocation& allocation) {

    // Get the memory pool for this type
    MemoryTypePool* pool = nullptr;
    for (auto& p : memoryTypePools) {
        if (p.memoryTypeIndex == memoryTypeIndex) {
            pool = &p;
            break;
        }
    }

    if (!pool) {
        PLOG_ERROR << "No memory pool found for type " << memoryTypeIndex;
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    // For each block in the pool
    for (auto& block : pool->blocks) {
        // For each free chunk in the block
        for (auto it = block.freeList.begin(); it != block.freeList.end(); ++it) {
            MemoryChunk& chunk = *it;
            VkDeviceSize offset = chunk.offset;
            VkDeviceSize availableSize = chunk.size;

            // Calculate aligned offset
            VkDeviceSize alignedOffset = (offset + alignment - 1) & ~(alignment - 1);
            VkDeviceSize alignmentLoss = alignedOffset - offset;

            // Check if enough space
            if (availableSize >= size + alignmentLoss) {
                // Remove this chunk
                block.freeList.erase(it);

                // If there's space before the aligned offset, add it as a chunk
                if (alignmentLoss > 0) {
                    block.freeList.push_back(MemoryChunk(offset, alignmentLoss));
                }

                // If there's space after the allocation, add it as a chunk
                VkDeviceSize excessSize = availableSize - size - alignmentLoss;
                if (excessSize > 0) {
                    block.freeList.push_back(MemoryChunk(alignedOffset + size, excessSize));
                }

                // Update the allocation info
                allocation.memory = block.memory;
                allocation.offset = alignedOffset;
                allocation.size = size;
                allocation.memoryTypeIndex = memoryTypeIndex;
                allocation.mappedData = nullptr;

                // Update pool stats
                pool->usedSize += size;

                PLOG_DEBUG << "Sub-allocated memory: size=" << size
                           << ", aligned_offset=" << alignedOffset
                           << ", type=" << memoryTypeIndex;

                return VK_SUCCESS;
            }
        }
    }

    // No suitable space found
    return VK_ERROR_OUT_OF_DEVICE_MEMORY;
}

// Get or create a memory type pool
MemoryTypePool& VkDeviceAllocator::getOrCreateMemoryTypePool(uint32_t memoryTypeIndex) {
    // Look for existing pool
    for (auto& pool : memoryTypePools) {
        if (pool.memoryTypeIndex == memoryTypeIndex) {
            return pool;
        }
    }

    // Create new pool
    MemoryTypePool newPool = {};
    newPool.memoryTypeIndex = memoryTypeIndex;
    newPool.properties = memoryProperties.memoryTypes[memoryTypeIndex].propertyFlags;
    newPool.totalSize = 0;
    newPool.usedSize = 0;

    memoryTypePools.push_back(newPool);

    PLOG_INFO << "Created new memory type pool for type " << memoryTypeIndex;

    return memoryTypePools.back();
}

} // namespace aura3d
