#ifndef ALLOCATOR_H
#define ALLOCATOR_H

#pragma once

#include <cstddef>

namespace aura3d {

/**
 * @class Allocator
 * @brief Abstract base class defining a generic memory allocation interface.
 *
 * This interface allows for implementation of different memory allocation strategies
 * while providing a common interface for clients.
 */
class Allocator {
public:
    /**
     * @brief Virtual destructor to ensure proper cleanup in derived classes.
     */
    virtual ~Allocator() = default;

    /**
     * @brief Allocate memory of specified size with given alignment requirements.
     *
     * @param size Size of the allocation in bytes.
     * @param alignment Alignment requirement for the allocation.
     * @return Pointer to the allocated memory, or nullptr if allocation failed.
     */
    virtual void* allocate(size_t size, size_t alignment) = 0;

    /**
     * @brief Free previously allocated memory.
     *
     * @param ptr Pointer to the memory to be freed.
     */
    virtual void free(void* ptr) = 0;

    /**
     * @brief Get the actual size of an allocation.
     *
     * This may return a value larger than what was requested due to alignment
     * or other implementation details.
     *
     * @param ptr Pointer to the allocated memory.
     * @return The actual size of the allocation in bytes.
     */
    virtual size_t getAllocationSize(void* ptr) = 0;

    /**
     * @brief Get the total memory managed by this allocator.
     *
     * @return The total memory size in bytes.
     */
    virtual size_t getTotalMemory() = 0;

    /**
     * @brief Get the currently used memory amount.
     *
     * @return The used memory size in bytes.
     */
    virtual size_t getUsedMemory() = 0;

    /**
     * @brief Reset the allocator, freeing all allocations.
     */
    virtual void reset() = 0;
};

} // namespace aura3d
#endif // ALLOCATOR_H
