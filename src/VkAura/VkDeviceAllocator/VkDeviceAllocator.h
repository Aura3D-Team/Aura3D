#ifndef VK_DEVICE_ALLOCATOR_H
#define VK_DEVICE_ALLOCATOR_H

#include <vulkan/vulkan.h>
#include <unordered_map>
#include <vector>
#include <mutex>
// #include <memory>

namespace aura3d {

// Enum to track allocation mapping state
enum class AllocationMappingState {
    UNMAPPED,
    MAPPED,
    PERSISTENTLY_MAPPED
};

// Represents an allocation from the device allocator
struct VkDeviceAllocation {
    VkDeviceMemory memory = VK_NULL_HANDLE;    // The Vulkan memory object
    VkDeviceSize offset = 0;                   // Offset within the memory object
    VkDeviceSize size = 0;                     // Size of the allocation
    uint32_t memoryTypeIndex = 0;              // Memory type index
    void* mappedData = nullptr;                // Pointer to mapped memory (nullptr if not mapped)
    uint32_t allocationId = 0;                 // Unique ID for this allocation
    AllocationMappingState mappingState = AllocationMappingState::UNMAPPED; // Current mapping state

    // Reset the allocation to default state
    void reset() {
        memory = VK_NULL_HANDLE;
        offset = 0;
        size = 0;
        memoryTypeIndex = 0;
        mappedData = nullptr;
        // Don't reset allocationId to keep it unique
        mappingState = AllocationMappingState::UNMAPPED;
    }
};

// Configuration for the device allocator
struct VkDeviceAllocatorCreateInfo {
    VkPhysicalDevice physicalDevice;
    VkDevice device;
    VkDeviceSize blockSize = 64 * 1024 * 1024;  // 64MB default block size
    bool enableDefragmentation = false;         // Defragmentation support
    bool enableHostMapping = true;              // Support for memory mapping
    bool trackLeaks = true;                     // Track memory leaks in debug mode
};

// Free memory chunk within a memory block
struct MemoryChunk {
    VkDeviceSize offset;
    VkDeviceSize size;

    // Constructor for convenience
    MemoryChunk(VkDeviceSize offset = 0, VkDeviceSize size = 0)
        : offset(offset), size(size) {}

    // Comparison operators for sorting chunks by offset
    bool operator<(const MemoryChunk& other) const {
        return offset < other.offset;
    }
};

// Memory block within a memory type pool
struct MemoryBlock {
    VkDeviceMemory memory;
    VkDeviceSize size;
    std::vector<MemoryChunk> freeList;  // List of free ranges
    bool canBeMapped;
    bool isMapped;                      // Will be protected by allocationMutex
    void* mappedAddress;
    std::unordered_map<VkDeviceSize, uint32_t> mappedRegions;  // Track active mappings by offset->allocationId

    MemoryBlock()
        : memory(VK_NULL_HANDLE), size(0), canBeMapped(false), isMapped(false), mappedAddress(nullptr) {}

    // Define copy constructor explicitly
    MemoryBlock(const MemoryBlock& other)
        : memory(other.memory),
        size(other.size),
        freeList(other.freeList),
        canBeMapped(other.canBeMapped),
        isMapped(other.isMapped),
        mappedAddress(other.mappedAddress),
        mappedRegions(other.mappedRegions) {}

    // Add assignment operator
    MemoryBlock& operator=(const MemoryBlock& other) {
        if (this != &other) {
            memory = other.memory;
            size = other.size;
            freeList = other.freeList;
            canBeMapped = other.canBeMapped;
            isMapped = other.isMapped;
            mappedAddress = other.mappedAddress;
            mappedRegions = other.mappedRegions;
        }
        return *this;
    }
};

// Pool of memory blocks for a specific memory type
struct MemoryTypePool {
    uint32_t memoryTypeIndex;
    VkMemoryPropertyFlags properties;
    std::vector<MemoryBlock> blocks;
    VkDeviceSize totalSize;
    VkDeviceSize usedSize;

    MemoryTypePool()
        : memoryTypeIndex(0), properties(0), totalSize(0), usedSize(0) {}

    // Define copy constructor explicitly
    MemoryTypePool(const MemoryTypePool& other)
        : memoryTypeIndex(other.memoryTypeIndex),
        properties(other.properties),
        blocks(other.blocks),  // This will call MemoryBlock's copy constructor
        totalSize(other.totalSize),
        usedSize(other.usedSize) {}

    // Add assignment operator
    MemoryTypePool& operator=(const MemoryTypePool& other) {
        if (this != &other) {
            memoryTypeIndex = other.memoryTypeIndex;
            properties = other.properties;
            blocks = other.blocks;
            totalSize = other.totalSize;
            usedSize = other.usedSize;
        }
        return *this;
    }
};

/**
 * @class VkDeviceAllocator
 * @brief A device memory allocator for Vulkan that efficiently manages memory allocations.
 *
 * This class provides a singleton instance that manages Vulkan device memory allocations
 * using a sub-allocation strategy to reduce the number of actual Vulkan memory allocations.
 * It handles mapping and unmapping memory, binding resources, and tracking allocations.
 */
class VkDeviceAllocator {
public:
    /**
     * @brief Constructor - use initialize() instead
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
     * @brief Structure to hold memory statistics
     */
    struct MemoryStats {
        VkDeviceSize totalSize;
        VkDeviceSize usedSize;
        uint32_t allocationCount;
        uint32_t blockCount;
        std::vector<std::pair<uint32_t, VkDeviceSize>> sizeByMemoryType;
    };

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

    void cleanup();

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

    // Member variables
    VkPhysicalDevice physicalDevice;
    VkDevice device;
    VkDeviceSize defaultBlockSize;
    bool defragmentationEnabled;
    bool trackLeaks;

    // Memory pools by memory type
    std::vector<MemoryTypePool> memoryTypePools;

    // Physical device memory properties
    VkPhysicalDeviceMemoryProperties memoryProperties;

    // Tracking info
    uint32_t nextAllocationId;
    std::unordered_map<uint32_t, VkDeviceAllocation> allocationMap;

    // Thread safety
    mutable std::mutex allocationMutex;

    // Flag to track if we're in shutdown to avoid errors
    bool inShutdown;
};

} // namespace aura3d

#endif // VK_DEVICE_ALLOCATOR_H
