#include "VkBufferMemoryAllocator.h"

#include "AuraException/AuraException.h"
#include <cassert>
#include <plog/Log.h>

namespace aura3d {

VkBufferMemoryAllocator::VkBufferMemoryAllocator(VkDevice* device, VkPhysicalDevice physicalDevice, VkDeviceSize blockSize)
    : _device(device), _physicalDevice(physicalDevice), _blockSize(blockSize)
{
}

VkBufferMemoryAllocator::~VkBufferMemoryAllocator()
{
    cleanup();
}

AllocationInfo VkBufferMemoryAllocator::allocate(VkBuffer buffer, VkMemoryPropertyFlags properties)
{
    // Get memory requirements for this buffer
    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(*_device, buffer, &memRequirements);

    // Find appropriate memory type
    uint32_t memoryTypeIndex = VkBufferMemoryAllocator::findMemoryType(_physicalDevice, memRequirements.memoryTypeBits, properties);

    // Lock to prevent concurrent allocations
    std::lock_guard<std::mutex> lock(_allocationMutex);

    // Try to find existing chunk
    AllocationInfo allocation = findChunk(memRequirements, memoryTypeIndex);

    // If no suitable chunk found, allocate a new block
    if (allocation.memory == VK_NULL_HANDLE) {
        MemoryBlock* block = allocateBlock(memoryTypeIndex, properties);

        // Create initial chunk using entire block
        MemoryChunk chunk;
        chunk.offset = 0;
        chunk.size = _blockSize;
        chunk.free = true;
        chunk.alignment = 1; // Default alignment
        block->chunks.push_back(chunk);

        // Try allocation again
        allocation = findChunk(memRequirements, memoryTypeIndex);
        if (allocation.memory == VK_NULL_HANDLE) {
            throw AuraException("Failed to allocate memory even after creating a new block!");
        }
    }

    // Bind buffer to the allocated memory
    VkResult result = vkBindBufferMemory(*_device, buffer, allocation.memory, allocation.offset);
    if (result != VK_SUCCESS) {
        throw AuraException("Failed to bind buffer memory!");
    }

    // Track this buffer allocation
    _bufferAllocations[buffer] = allocation;

    return allocation;
}

AllocationInfo VkBufferMemoryAllocator::allocateForImage(VkImage image, VkMemoryPropertyFlags properties)
{
    // Get memory requirements for this image
    VkMemoryRequirements memRequirements;
    vkGetImageMemoryRequirements(*_device, image, &memRequirements);

    // Find appropriate memory type
    uint32_t memoryTypeIndex = VkBufferMemoryAllocator::findMemoryType(_physicalDevice, memRequirements.memoryTypeBits, properties);

    // Lock to prevent concurrent allocations
    std::lock_guard<std::mutex> lock(_allocationMutex);

    // Try to find existing chunk
    AllocationInfo allocation = findChunk(memRequirements, memoryTypeIndex);

    // If no suitable chunk found, allocate a new block
    if (allocation.memory == VK_NULL_HANDLE) {
        MemoryBlock* block = allocateBlock(memoryTypeIndex, properties);

        // Create initial chunk using entire block
        MemoryChunk chunk;
        chunk.offset = 0;
        chunk.size = _blockSize;
        chunk.free = true;
        chunk.alignment = 1; // Default alignment
        block->chunks.push_back(chunk);

        // Try allocation again
        allocation = findChunk(memRequirements, memoryTypeIndex);
        if (allocation.memory == VK_NULL_HANDLE) {
            throw AuraException("Failed to allocate memory even after creating a new block for image!");
        }
    }

    // Bind image to the allocated memory
    VkResult result = vkBindImageMemory(*_device, image, allocation.memory, allocation.offset);
    if (result != VK_SUCCESS) {
        throw AuraException("Failed to bind image memory!");
    }

    // Track this image allocation
    _imageAllocations[image] = allocation;

    return allocation;
}

AllocationInfo VkBufferMemoryAllocator::getAllocationInfo(VkDeviceMemory memory, VkDeviceSize offset)
{
    std::lock_guard<std::mutex> lock(_allocationMutex);
    AllocationInfo result{};
    result.memory = memory;
    result.offset = offset;
    result.mappedData = nullptr;

    // Find the block containing this allocation
    for (auto& block : _blocks) {
        if (block.memory == memory) {
            // Find the chunk at this offset
            for (auto& chunk : block.chunks) {
                if (chunk.offset == offset && !chunk.free) {
                    result.size = chunk.size;
                    // If memory is mapped, calculate the pointer
                    if (block.mappedData) {
                        result.mappedData = static_cast<char*>(block.mappedData) + offset;
                    }
                    return result;
                }
            }
        }
    }

    // If we couldn't find the allocation in our records, it might
    // be a memory allocation we didn't manage
    return result;
}

void VkBufferMemoryAllocator::unbindBuffer(VkBuffer buffer)
{
    std::lock_guard<std::mutex> lock(_allocationMutex);
    auto it = _bufferAllocations.find(buffer);
    if (it != _bufferAllocations.end()) {
        // Free the memory chunk
        free(it->second.memory, it->second.offset);
        // Remove from tracked buffers
        _bufferAllocations.erase(it);
    }
}

void VkBufferMemoryAllocator::unbindImage(VkImage image)
{
    std::lock_guard<std::mutex> lock(_allocationMutex);
    auto it = _imageAllocations.find(image);
    if (it != _imageAllocations.end()) {
        // Free the memory chunk
        free(it->second.memory, it->second.offset);
        // Remove from tracked images
        _imageAllocations.erase(it);
    }
}

void VkBufferMemoryAllocator::free(VkDeviceMemory memory, VkDeviceSize offset)
{
    std::lock_guard<std::mutex> lock(_allocationMutex);

    // Find the block containing this allocation
    for (auto& block : _blocks) {
        if (block.memory == memory) {
            // Find the chunk at this offset
            for (auto& chunk : block.chunks) {
                if (chunk.offset == offset) {
                    // Mark it as free
                    chunk.free = true;

                    // Coalesce adjacent free chunks
                    bool mergeOccurred;
                    do {
                        mergeOccurred = false;
                        for (size_t i = 0; i < block.chunks.size(); i++) {
                            for (size_t j = 0; j < block.chunks.size(); j++) {
                                if (i != j && block.chunks[i].free && block.chunks[j].free) {
                                    // Check if chunks are adjacent
                                    if (block.chunks[i].offset + block.chunks[i].size == block.chunks[j].offset) {
                                        // Merge chunks
                                        block.chunks[i].size += block.chunks[j].size;
                                        block.chunks.erase(block.chunks.begin() + j);
                                        mergeOccurred = true;
                                        break;
                                    }
                                }
                            }
                            if (mergeOccurred) break;
                        }
                    } while (mergeOccurred);

                    return;
                }
            }
        }
    }
}

void VkBufferMemoryAllocator::cleanup()
{
    // First, get copies of all allocations since we'll be modifying the maps
    auto bufferAllocs = _bufferAllocations;
    auto imageAllocs = _imageAllocations;

    // Log warning about still-bound resources
    if (!bufferAllocs.empty()) {
        PLOG_WARNING << "There are " << bufferAllocs.size() << " still-bound buffers!";
    }
    if (!imageAllocs.empty()) {
        PLOG_WARNING << "There are " << imageAllocs.size() << " still-bound images!";
    }

    // Clear tracking maps
    _bufferAllocations.clear();
    _imageAllocations.clear();

    // Free all blocks
    for (auto& block : _blocks) {
        if (block.mappedData) {
            vkUnmapMemory(*_device, block.memory);
            block.mappedData = nullptr;
        }
        vkFreeMemory(*_device, block.memory, nullptr);
    }
    _blocks.clear();
}
uint32_t VkBufferMemoryAllocator::findMemoryType(VkPhysicalDevice physicalDevice, uint32_t typeFilter, VkMemoryPropertyFlags properties)
{
    VkPhysicalDeviceMemoryProperties memProperties;
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProperties);

    for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
        if ((typeFilter & (1 << i)) &&
            (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }

    throw AuraException("Failed to find suitable memory type!");
}

MemoryBlock* VkBufferMemoryAllocator::allocateBlock(uint32_t memoryTypeIndex, VkMemoryPropertyFlags properties)
{
    MemoryBlock block;
    block.size = _blockSize;
    block.memoryTypeIndex = memoryTypeIndex;
    block.mappedData = nullptr;

    // Allocate the memory
    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = _blockSize;
    allocInfo.memoryTypeIndex = memoryTypeIndex;

    VkResult result = vkAllocateMemory(*_device, &allocInfo, nullptr, &block.memory);
    if (result != VK_SUCCESS) {
        throw AuraException("Failed to allocate memory block!");
    }

    // Map memory if it's host-visible
    if (properties & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) {
        result = vkMapMemory(*_device, block.memory, 0, _blockSize, 0, &block.mappedData);
        if (result != VK_SUCCESS) {
            vkFreeMemory(*_device, block.memory, nullptr);
            throw AuraException("Failed to map memory!");
        }
    }

    _blocks.push_back(block);
    return &_blocks.back();
}

AllocationInfo VkBufferMemoryAllocator::findChunk(VkMemoryRequirements memRequirements, uint32_t memoryTypeIndex)
{
    AllocationInfo result{};
    result.memory = VK_NULL_HANDLE;

    // Find smallest chunk that fits our requirements
    VkDeviceSize bestFitSize = VK_WHOLE_SIZE;

    for (auto& block : _blocks) {
        if (block.memoryTypeIndex == memoryTypeIndex) {
            for (auto& chunk : block.chunks) {
                if (chunk.free && chunk.size >= memRequirements.size) {
                    // Check alignment
                    VkDeviceSize alignedOffset = (chunk.offset + memRequirements.alignment - 1) & ~(memRequirements.alignment - 1);
                    VkDeviceSize alignmentPadding = alignedOffset - chunk.offset;

                    if (chunk.size >= memRequirements.size + alignmentPadding) {
                        // This chunk fits, check if it's the best fit so far
                        if (chunk.size < bestFitSize) {
                            bestFitSize = chunk.size;
                            result.memory = block.memory;
                            result.offset = alignedOffset;
                            result.size = memRequirements.size;

                            if (block.mappedData) {
                                result.mappedData = static_cast<char*>(block.mappedData) + alignedOffset;
                            } else {
                                result.mappedData = nullptr;
                            }

                            // If this chunk is a perfect fit, break early
                            if (chunk.size == memRequirements.size + alignmentPadding) {
                                break;
                            }
                        }
                    }
                }
            }
        }
    }

    // If we found a suitable chunk, split it
    if (result.memory != VK_NULL_HANDLE) {
        for (auto& block : _blocks) {
            if (block.memory == result.memory) {
                for (auto it = block.chunks.begin(); it != block.chunks.end(); ++it) {
                    if (it->free && it->offset <= result.offset &&
                        it->offset + it->size >= result.offset + memRequirements.size) {

                        // Calculate alignment padding
                        VkDeviceSize alignedOffset = (it->offset + memRequirements.alignment - 1) & ~(memRequirements.alignment - 1);
                        VkDeviceSize alignmentPadding = alignedOffset - it->offset;

                        // Handle front padding if necessary
                        if (alignmentPadding > 0) {
                            MemoryChunk paddingChunk;
                            paddingChunk.offset = it->offset;
                            paddingChunk.size = alignmentPadding;
                            paddingChunk.free = true;
                            paddingChunk.alignment = 1;

                            // Insert padding chunk
                            block.chunks.insert(it, paddingChunk);

                            // Update the main chunk
                            it = block.chunks.begin() + (it - block.chunks.begin() + 1);
                            it->offset += alignmentPadding;
                            it->size -= alignmentPadding;
                        }

                        // Split the chunk if it's larger than needed
                        if (it->size > memRequirements.size) {
                            MemoryChunk newChunk;
                            newChunk.offset = it->offset + memRequirements.size;
                            newChunk.size = it->size - memRequirements.size;
                            newChunk.free = true;
                            newChunk.alignment = 1;

                            // Update the allocated chunk
                            it->size = memRequirements.size;
                            it->free = false;
                            it->alignment = memRequirements.alignment;

                            // Insert the remainder chunk
                            block.chunks.insert(it + 1, newChunk);
                        } else {
                            // Use the entire chunk
                            it->free = false;
                            it->alignment = memRequirements.alignment;
                        }

                        break;
                    }
                }
                break;
            }
        }
    }

    return result;
}

} // namespace aura3d
