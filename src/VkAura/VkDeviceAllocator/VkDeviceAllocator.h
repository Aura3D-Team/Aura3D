#ifndef VK_DEVICE_ALLOCATOR_H
#define VK_DEVICE_ALLOCATOR_H

#include "VkDeviceAllocatorTypes.h"
#include <vulkan/vulkan.h>
#include <unordered_map>
#include <vector>
#include <mutex>
// #include <shared_mutex>
#include <atomic>
#include <memory>
// #include <queue>

namespace aura3d {

/**
 * @class VkDeviceAllocator
 * @brief A high-performance device memory allocator for Vulkan.
 *
 * This class provides a memory allocator that efficiently manages Vulkan device memory allocations
 * using a sub-allocation strategy to reduce the number of actual Vulkan memory allocations.
 * It handles mapping and unmapping memory, binding resources, and tracking allocations with
 * configurable thread safety and allocation strategies.
 */
class VkDeviceAllocator {
public:
    /**
     * @brief Constructor
     * @param createInfo The configuration for the allocator
     */
    explicit VkDeviceAllocator(const VkDeviceAllocatorCreateInfo& createInfo);

    /**
     * @brief Destructor - frees all allocated memory
     */
    ~VkDeviceAllocator();

    /**
     * @brief Allocate memory based on memory requirements
     * @param memRequirements The memory requirements (from Vulkan)
     * @param properties The desired memory properties
     * @param allocation Output parameter to receive the allocation details
     * @return VK_SUCCESS on success, other VkResult values on failure
     */
    VkResult allocateMemory(const VkMemoryRequirements& memRequirements,
                            VkMemoryPropertyFlags properties,
                            VkDeviceAllocation& allocation);

    /**
     * @brief Allocate memory for a buffer
     * @param buffer The Vulkan buffer
     * @param properties The desired memory properties
     * @param allocation Output parameter to receive the allocation details
     * @return VK_SUCCESS on success, other VkResult values on failure
     */
    VkResult allocateMemoryForBuffer(VkBuffer buffer,
                                     VkMemoryPropertyFlags properties,
                                     VkDeviceAllocation& allocation);

    /**
     * @brief Allocate memory for an image
     * @param image The Vulkan image
     * @param properties The desired memory properties
     * @param allocation Output parameter to receive the allocation details
     * @return VK_SUCCESS on success, other VkResult values on failure
     */
    VkResult allocateMemoryForImage(VkImage image,
                                    VkMemoryPropertyFlags properties,
                                    VkDeviceAllocation& allocation);

    /**
     * @brief Free memory that was previously allocated
     * @param allocation The allocation to free
     */
    void freeMemory(VkDeviceAllocation& allocation);

    /**
     * @brief Map memory for CPU access
     * @param allocation The allocation to map
     * @param offset The offset within the allocation
     * @param size The size to map, or VK_WHOLE_SIZE for the entire allocation
     * @param ppData Output parameter to receive the mapped pointer
     * @return VK_SUCCESS on success, other VkResult values on failure
     */
    VkResult mapMemory(VkDeviceAllocation& allocation, VkDeviceSize offset, VkDeviceSize size, void** ppData);

    /**
     * @brief Unmap memory that was previously mapped
     * @param allocation The allocation to unmap
     */
    void unmapMemory(VkDeviceAllocation& allocation);

    /**
     * @brief Bind memory to a buffer
     * @param buffer The Vulkan buffer
     * @param allocation The allocation to bind
     * @param offsetInAllocation Optional offset within the allocation
     * @return VK_SUCCESS on success, other VkResult values on failure
     */
    VkResult bindBufferMemory(VkBuffer buffer, const VkDeviceAllocation& allocation, VkDeviceSize offsetInAllocation = 0);

    /**
     * @brief Bind memory to an image
     * @param image The Vulkan image
     * @param allocation The allocation to bind
     * @param offsetInAllocation Optional offset within the allocation
     * @return VK_SUCCESS on success, other VkResult values on failure
     */
    VkResult bindImageMemory(VkImage image, const VkDeviceAllocation& allocation, VkDeviceSize offsetInAllocation = 0);

    /**
     * @brief Get memory statistics
     * @return MemoryStats structure with current statistics
     */
    MemoryStats getMemoryStats() const;

    /**
     * @brief Print memory statistics to the console
     */
    void printMemoryStats() const;

    /**
     * @brief Force unmap all mapped memory - useful during shutdown
     */
    void unmapAllMemory();

    /**
     * @brief Get the Vulkan device
     * @return The Vulkan device
     */
    VkDevice getDevice() const { return device; }

    /**
     * @brief Get the Vulkan physical device
     * @return The Vulkan physical device
     */
    VkPhysicalDevice getPhysicalDevice() const { return physicalDevice; }

    /**
     * @brief Get allocation by ID
     * @param allocationId The ID of the allocation to get
     * @return Reference to the allocation, or a default allocation if not found
     */
    VkDeviceAllocation& getAllocation(uint32_t allocationId);

    /**
     * @brief Process any deferred free operations
     * @param processAll If true, process all pending frees regardless of the limit
     */
    void processDeferredFrees(bool processAll = false);

    /**
     * @brief Cleanup and release all resources - call before destruction
     */
    void cleanup();

    /**
     * @brief Defragment memory to reduce fragmentation
     * @param maxBytesToMove Maximum number of bytes to move during defragmentation
     * @return VK_SUCCESS on success, other VkResult values on failure
     */
    VkResult defragment(VkDeviceSize maxBytesToMove = VK_WHOLE_SIZE);

private:
    /**
     * @brief Find a suitable memory type
     * @param typeFilter The type filter from memory requirements
     * @param properties The desired memory properties
     * @return The memory type index, or UINT32_MAX if not found
     */
    uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties);

    /**
     * @brief Allocate a new memory block
     * @param memoryTypeIndex The memory type index
     * @param blockSize The size of the block to allocate
     * @return VK_SUCCESS on success, other VkResult values on failure
     */
    VkResult allocateNewBlock(uint32_t memoryTypeIndex, VkDeviceSize blockSize);

    /**
     * @brief Find space in a block and allocate from it
     * @param memoryTypeIndex The memory type index
     * @param size The size to allocate
     * @param alignment The required alignment
     * @param allocation Output parameter to receive the allocation details
     * @return VK_SUCCESS on success, other VkResult values on failure
     */
    VkResult findAndAllocateInBlock(uint32_t memoryTypeIndex,
                                    VkDeviceSize size,
                                    VkDeviceSize alignment,
                                    VkDeviceAllocation& allocation);

    /**
     * @brief Allocate using the buddy allocator if available
     * @param memoryTypeIndex The memory type index
     * @param size The size to allocate
     * @param alignment The required alignment
     * @param allocation Output parameter to receive the allocation details
     * @return VK_SUCCESS on success, other VkResult values on failure
     */
    VkResult allocateUsingBuddyAllocator(uint32_t memoryTypeIndex,
                                         VkDeviceSize size,
                                         VkDeviceSize alignment,
                                         VkDeviceAllocation& allocation);

    /**
     * @brief Get or create a memory type pool
     * @param memoryTypeIndex The memory type index
     * @return Reference to the memory type pool
     */
    MemoryTypePool& getOrCreateMemoryTypePool(uint32_t memoryTypeIndex);

    /**
     * @brief Find a block by memory handle
     * @param memory The Vulkan memory handle
     * @param outBlock Output parameter to receive the block pointer
     * @return true if found, false otherwise
     */
    bool findBlockByMemory(VkDeviceMemory memory, MemoryBlock** outBlock);

    /**
     * @brief Initialize a buddy allocator for a memory type
     * @param memoryTypeIndex The memory type index
     * @param blockSize The size of the block for the buddy allocator
     */
    void initializeBuddyAllocator(uint32_t memoryTypeIndex, VkDeviceSize blockSize);

    /**
     * @brief Find a free range using the specified allocation strategy
     * @param block The memory block to search
     * @param size The size required
     * @param alignment The alignment required
     * @param outOffset Output parameter to receive the offset
     * @param outPadding Output parameter to receive the padding required for alignment
     * @return true if a suitable range was found, false otherwise
     */
    bool findFreeRange(MemoryBlock& block,
                       VkDeviceSize size,
                       VkDeviceSize alignment,
                       VkDeviceSize& outOffset,
                       VkDeviceSize& outPadding);

    /**
     * @brief Acquire the proper lock for a memory operation
     * @param memoryTypeIndex The memory type index involved in the operation
     * @param forWrite True if the operation will modify the pool
     */
    void acquireLock(uint32_t memoryTypeIndex, bool forWrite = true);

    /**
     * @brief Release the proper lock
     * @param memoryTypeIndex The memory type index for which the lock was acquired
     * @param forWrite True if the lock was acquired for writing
     */
    void releaseLock(uint32_t memoryTypeIndex, bool forWrite = true);

    // Member variables
    VkPhysicalDevice physicalDevice;
    VkDevice device;
    VkDeviceSize defaultBlockSize;
    VkDeviceSize smallBlockSize;
    bool defragmentationEnabled;
    bool trackLeaks;
    ThreadSafetyMode threadSafetyMode;
    AllocationStrategy allocationStrategy;
    uint32_t dedicatedAllocationThreshold;
    bool useBuddyAllocatorForBuffers;
    bool deferFrees;
    uint32_t deferredFreeLimit;

    // Memory pools by memory type
    std::vector<MemoryTypePool> memoryTypePools;

    // Physical device memory properties
    VkPhysicalDeviceMemoryProperties memoryProperties;

    // Tracking info
    std::atomic<uint32_t> nextAllocationId;
    std::unordered_map<uint32_t, VkDeviceAllocation> allocationMap;

    // Thread safety
    mutable std::mutex globalMutex;                                           // Global mutex for coarse-grained locking
    mutable std::vector<std::unique_ptr<std::mutex>> poolMutexes;             // Per-pool mutexes for fine-grained locking

    // Flag to track if we're in shutdown to avoid errors
    bool inShutdown;
};

} // namespace aura3d

#endif // VK_DEVICE_ALLOCATOR_H
