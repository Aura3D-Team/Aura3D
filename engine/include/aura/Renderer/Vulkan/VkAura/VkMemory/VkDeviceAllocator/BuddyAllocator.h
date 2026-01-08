#ifndef BUDDYALLOCATOR_H
#define BUDDYALLOCATOR_H

#pragma once

#include <vulkan/vulkan.h>
#include <vector>

#include "aura/aura.h"

namespace aura3d {
namespace vk {

/**
 * @brief Efficient memory allocator for power-of-2 sized blocks
 *
 * Implements a buddy allocation system that manages memory by dividing
 * it into power-of-2 sized blocks. This is particularly useful for GPU
 * buffer allocations where alignment requirements are common.
 */
class BuddyAllocator {
private:
    /**
     * @brief Represents a single block in the buddy system
     */
    struct BuddyBlock {
        VkDeviceSize offset;   ///< Offset from the start of memory
        u32 level;             ///< Power of 2 size: blockSize / (2^level)
        bool allocated;        ///< Whether this block is in use

        BuddyBlock(VkDeviceSize offset, u32 level, bool allocated)
            : offset(offset), level(level), allocated(allocated) {}
    };

    std::vector<std::vector<BuddyBlock>> levels;  ///< Blocks at each level
    VkDeviceSize totalSize;                       ///< Total memory size
    VkDeviceSize minBlockSize;                    ///< Minimum block size
    u32 maxLevels;                                ///< Number of levels

public:
    /**
     * @brief Constructs a new Buddy Allocator
     *
     * @param totalSize The total size of memory to manage
     * @param minBlockSize The minimum size of an allocatable block
     */
    BuddyAllocator(VkDeviceSize totalSize, VkDeviceSize minBlockSize);

    /**
     * @brief Allocates a memory block of the specified size
     *
     * @param size The requested allocation size
     * @param outOffset Reference to store the allocated block's offset
     * @return true if allocation succeeded, false if no suitable block was found
     */
    bool allocate(VkDeviceSize size, VkDeviceSize& outOffset);

    /**
     * @brief Frees a previously allocated memory block
     *
     * @param offset The offset of the block to free
     */
    void free(VkDeviceSize offset);

private:
    /**
     * @brief Splits a block recursively until reaching the target level
     *
     * @param fromLevel The level of the block to split
     * @param blockIndex The index of the block within its level
     * @param targetLevel The desired level to split down to
     */
    void splitUntilLevel(u32 fromLevel, size_t blockIndex, u32 targetLevel);

    /**
     * @brief Finds the index of a block at a specific level
     *
     * @param offset The memory offset to search for
     * @param fromLevel The original level containing the offset
     * @param targetLevel The level to find the corresponding block in
     * @return The index of the found block
     */
    size_t findBlockIndexAtLevel(VkDeviceSize offset, u32 fromLevel, u32 targetLevel);

    /**
     * @brief Attempts to merge a block with its buddy
     *
     * @param level The level of the block to merge
     * @param blockIndex The index of the block within its level
     */
    void mergeBuddies(u32 level, size_t blockIndex);
};

}
}

#endif // BUDDYALLOCATOR_H
