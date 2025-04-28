#ifndef MEMORYPOOL_H
#define MEMORYPOOL_H

#pragma once

#include <mutex>
#include <atomic>
#include <vector>

namespace aura3d {

class MemoryPool {
public:
    /**
     * @brief Constructs a memory pool with specified block size and initial block count
     *
     * @param blockSize Size of each memory block in bytes
     * @param initialBlocks Number of blocks to pre-allocate
     */
    MemoryPool(size_t blockSize, size_t initialBlocks = 64);

    /**
     * @brief Destroys the memory pool and frees all allocated memory
     */
    ~MemoryPool();

    /**
     * @brief Allocates a block from the memory pool
     *
     * Uses lock-free algorithms to allocate from the free list.
     * If free list is empty, more blocks are allocated.
     *
     * @return Pointer to the allocated memory block or nullptr if allocation failed
     */
    void* allocate();

    /**
     * @brief Returns a block to the memory pool
     *
     * Uses lock-free algorithms to return the block to the free list.
     *
     * @param ptr Pointer to the memory block to free
     */
    void free(void* ptr);

    size_t getBlockSize() const { return blockSize; }
    size_t getAllocatedCount() const { return allocatedCount.load(); }
    size_t getTotalCount() const { return totalBlocks.load(); }

private:
    struct Block {
        std::atomic<Block*> next;
    };

    const size_t blockSize;
    std::atomic<Block*> freeList;
    std::vector<void*> blocks;
    std::atomic<size_t> allocatedCount{0};
    std::atomic<size_t> totalBlocks{0};
    std::mutex expansionMutex;

    /**
     * @brief Adds new blocks to the memory pool
     *
     * Allocates a new chunk of memory and adds it to the free list.
     * Thread-safe through the use of a mutex for expansion operations.
     *
     * @param count Number of blocks to add
     */
    void addBlocks(size_t count);
};

}

#endif // MEMORYPOOL_H
