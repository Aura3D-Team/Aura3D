#include "MemoryPool.h"

#include <algorithm>
#include <ink/Inkogger.h>

namespace aura3d {
namespace vk {

MemoryPool::MemoryPool(size_t blockSize, size_t initialBlocks)
    : blockSize(blockSize), freeList(nullptr)
{
    addBlocks(initialBlocks);
}

MemoryPool::~MemoryPool()
{
    for (void* block : blocks) {
#if defined(_WIN32)
        _aligned_free(block);
#else
        free(block);
#endif
    }
}

void MemoryPool::addBlocks(size_t count)
{
    std::lock_guard<std::mutex> lock(expansionMutex);

    size_t actualBlockSize = std::max(blockSize, sizeof(Block*));
    size_t totalSize = actualBlockSize * count;

#if defined(_WIN32)
    void* memory = _aligned_malloc(totalSize, std::max(size_t(16), sizeof(void*)));
#else
    void* memory = nullptr;
    if (posix_memalign(&memory, std::max(size_t(16), sizeof(void*)), totalSize) != 0)
        memory = nullptr;
#endif

    if (!memory)
    {
        INK_ERROR << "Failed to allocate memory for pool of block size " << blockSize;
        return;
    }

    blocks.push_back(memory);

    char* curr = static_cast<char*>(memory);
    Block* oldHead = freeList.load(std::memory_order_relaxed);

    for (size_t i = 0; i < count; ++i)
    {
        Block* block = reinterpret_cast<Block*>(curr);
        if (i == count - 1)
        {
            block->next.store(oldHead, std::memory_order_relaxed);
            Block* firstBlock = reinterpret_cast<Block*>(static_cast<char*>(memory));
            freeList.store(firstBlock, std::memory_order_release);
        }
        else
        {
            Block* nextBlock = reinterpret_cast<Block*>(curr + actualBlockSize);
            block->next.store(nextBlock, std::memory_order_relaxed);
        }
        curr += actualBlockSize;
    }

    totalBlocks.fetch_add(count, std::memory_order_relaxed);
}

void* MemoryPool::allocate()
{

    Block* oldHead = freeList.load(std::memory_order_acquire);
    Block* newHead;

    do {
        if (!oldHead)
        {
            if (!(oldHead = freeList.load(std::memory_order_acquire)))
            {
                size_t newBlocks = std::max(size_t(64), totalBlocks.load(std::memory_order_relaxed));
                addBlocks(newBlocks);
                oldHead = freeList.load(std::memory_order_acquire);

                if (!oldHead)
                {
                    INK_ERROR << "Failed to allocate memory block even after expansion";
                    return nullptr;
                }
            }
        }

        newHead = oldHead->next.load(std::memory_order_relaxed);
    } while (!freeList.compare_exchange_weak(oldHead, newHead,
                                             std::memory_order_release,
                                             std::memory_order_acquire));

    allocatedCount.fetch_add(1, std::memory_order_relaxed);
    return oldHead;
}

void MemoryPool::free(void* ptr)
{

    Block* block = static_cast<Block*>(ptr);
    Block* oldHead = freeList.load(std::memory_order_relaxed);

    do {
        block->next.store(oldHead, std::memory_order_relaxed);
    } while (!freeList.compare_exchange_weak(oldHead, block,
                                             std::memory_order_release,
                                             std::memory_order_acquire));

    allocatedCount.fetch_sub(1, std::memory_order_relaxed);
}

}
}
