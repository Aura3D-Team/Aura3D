#include "aura/Renderer/Vulkan/VkAura/VkMemory/VkDeviceAllocator/BuddyAllocator.h"

namespace aura3d {
namespace vk {

/**
 * @brief Constructs a new Buddy Allocator
 *
 * Initializes a buddy memory allocator with the specified total size and minimum block size.
 * The allocator divides memory into blocks of power-of-2 sizes for efficient allocation.
 *
 * @param totalSize The total size of memory to manage
 * @param minBlockSize The minimum size of an allocatable block
 */
BuddyAllocator::BuddyAllocator(VkDeviceSize totalSize, VkDeviceSize minBlockSize)
    : totalSize(totalSize), minBlockSize(minBlockSize) {
    // Calculate number of levels
    maxLevels = 0;
    VkDeviceSize size = totalSize;
    while (size >= minBlockSize) {
        size /= 2;
        maxLevels++;
    }

    levels.resize(maxLevels);

    // Add the root block (entire memory range)
    levels[0].emplace_back(0, 0, false);
}

/**
 * @brief Allocates a memory block of the specified size
 *
 * Finds and allocates a free block of memory that can fit the requested size.
 * The size is rounded up to the next power of 2.
 *
 * @param size The requested allocation size
 * @param outOffset Reference to store the allocated block's offset
 * @return true if allocation succeeded, false if no suitable block was found
 */
bool BuddyAllocator::allocate(VkDeviceSize size, VkDeviceSize& outOffset) {
    // Find the level that can fit this size
    u32 level = 0;
    VkDeviceSize levelSize = totalSize;

    while (levelSize / 2 >= size && level + 1 < maxLevels) {
        levelSize /= 2;
        level++;
    }

    // Try to find a free block at this level
    for (u32 l = level; l < maxLevels; l++) {
        for (size_t i = 0; i < levels[l].size(); i++) {
            if (!levels[l][i].allocated) {
                // Found a free block
                if (l > level) {
                    // Need to split blocks from higher levels
                    splitUntilLevel(l, i, level);
                }

                // Mark the block as allocated
                levels[level][findBlockIndexAtLevel(levels[l][i].offset, l, level)].allocated = true;
                outOffset = levels[level][findBlockIndexAtLevel(levels[l][i].offset, l, level)].offset;
                return true;
            }
        }
    }

    return false;  // No free blocks
}

/**
 * @brief Frees a previously allocated memory block
 *
 * Releases a memory block at the given offset and attempts to merge with
 * its buddy if also free, potentially recursively merging up the tree.
 *
 * @param offset The offset of the block to free
 */
void BuddyAllocator::free(VkDeviceSize offset) {
    // Find the allocated block
    for (u32 l = 0; l < maxLevels; l++) {
        for (size_t i = 0; i < levels[l].size(); i++) {
            if (levels[l][i].offset == offset && levels[l][i].allocated) {
                // Mark as free
                levels[l][i].allocated = false;

                // Try to merge with buddy
                mergeBuddies(l, i);
                return;
            }
        }
    }
}

/**
 * @brief Splits a block recursively until reaching the target level
 *
 * Takes a free block at a higher level and splits it into smaller blocks
 * until a block at the target level is created.
 *
 * @param fromLevel The level of the block to split
 * @param blockIndex The index of the block within its level
 * @param targetLevel The desired level to split down to
 */
void BuddyAllocator::splitUntilLevel(u32 fromLevel, size_t blockIndex, u32 targetLevel) {
    if (fromLevel <= targetLevel) return;

    BuddyBlock& block = levels[fromLevel][blockIndex];
    VkDeviceSize offset = block.offset;
    VkDeviceSize size = totalSize / (1 << fromLevel);

    // Mark this block as split (not directly allocatable)
    block.allocated = true;

    // Create two child blocks at the next level down
    u32 nextLevel = fromLevel - 1;
    BuddyBlock left(offset, nextLevel, false);
    BuddyBlock right(offset + (size/2), nextLevel, false);

    levels[nextLevel].push_back(left);
    levels[nextLevel].push_back(right);

    // If we need to split further, split the left child
    if (nextLevel > targetLevel) {
        splitUntilLevel(nextLevel, levels[nextLevel].size() - 2, targetLevel);
    }
}

/**
 * @brief Finds the index of a block at a specific level
 *
 * Locates a block within a target level that contains the given offset.
 *
 * @param offset The memory offset to search for
 * @param fromLevel The original level containing the offset
 * @param targetLevel The level to find the corresponding block in
 * @return The index of the found block or 0 if not found
 */
size_t BuddyAllocator::findBlockIndexAtLevel(VkDeviceSize offset, u32 fromLevel, u32 targetLevel) {
    if (fromLevel == targetLevel) {
        // Find the block at this level
        for (size_t i = 0; i < levels[targetLevel].size(); i++) {
            if (levels[targetLevel][i].offset == offset) {
                return i;
            }
        }
        return 0;  // Not found (should not happen)
    }

    // Adjust the offset based on the level difference
    VkDeviceSize levelSize = totalSize / (1 << fromLevel);
    VkDeviceSize targetLevelSize = totalSize / (1 << targetLevel);

    // Find the containing block at the target level
    for (size_t i = 0; i < levels[targetLevel].size(); i++) {
        VkDeviceSize blockOffset = levels[targetLevel][i].offset;
        if (offset >= blockOffset && offset < blockOffset + targetLevelSize) {
            return i;
        }
    }

    return 0;  // Not found (should not happen)
}

/**
 * @brief Attempts to merge a block with its buddy
 *
 * If a block and its buddy are both free, combines them into a larger block
 * and recursively attempts to merge the parent with its buddy.
 *
 * @param level The level of the block to merge
 * @param blockIndex The index of the block within its level
 */
void BuddyAllocator::mergeBuddies(u32 level, size_t blockIndex) {
    if (level == 0) return;  // Can't merge the root

    BuddyBlock& block = levels[level][blockIndex];
    VkDeviceSize buddyOffset;
    VkDeviceSize size = totalSize / (1 << level);

    // Calculate buddy offset
    if (block.offset % (size * 2) == 0) {
        buddyOffset = block.offset + size;  // Right buddy
    } else {
        buddyOffset = block.offset - size;  // Left buddy
    }

    // Find the buddy
    for (size_t i = 0; i < levels[level].size(); i++) {
        if (levels[level][i].offset == buddyOffset) {
            // If buddy is also free, merge
            if (!levels[level][i].allocated) {
                // Find the parent block at level-1
                VkDeviceSize parentOffset = std::min(block.offset, buddyOffset);
                bool foundParent = false;

                for (size_t j = 0; j < levels[level-1].size(); j++) {
                    if (levels[level-1][j].offset == parentOffset) {
                        // Mark parent as free
                        levels[level-1][j].allocated = false;
                        foundParent = true;

                        // Remove the two child blocks
                        auto it1 = levels[level].begin() + std::min(blockIndex, i);
                        auto it2 = levels[level].begin() + std::max(blockIndex, i);
                        levels[level].erase(it2);
                        levels[level].erase(it1);

                        // Try to merge at the parent level
                        mergeBuddies(level-1, j);
                        break;
                    }
                }

                if (!foundParent) {
                    // Parent not found, just mark these blocks as free
                    levels[level][blockIndex].allocated = false;
                    levels[level][i].allocated = false;
                }
            }
            break;
        }
    }
}

}
}
