#ifndef ALIGNEDALLOCATOR_H
#define ALIGNEDALLOCATOR_H

#pragma once

#include <vector>
#include <algorithm>
#include <cstdint>

namespace aura3d {

// Custom aligned allocator for high-performance memory access
template<typename T, std::size_t Alignment = 32> // 32-byte alignment for AVX instructions
class AlignedAllocator {
public:
    using value_type = T;
    using pointer = T*;
    using const_pointer = const T*;
    using reference = T&;
    using const_reference = const T&;
    using size_type = std::size_t;
    using difference_type = std::ptrdiff_t;

    template<typename U>
    struct rebind {
        using other = AlignedAllocator<U, Alignment>;
    };

    AlignedAllocator() noexcept = default;
    template<typename U>
    AlignedAllocator(const AlignedAllocator<U, Alignment>&) noexcept {}

    pointer allocate(size_type n) {
        void* ptr = nullptr;
#if defined(_MSC_VER)
        ptr = _aligned_malloc(n * sizeof(T), Alignment);
#else
        if (posix_memalign(&ptr, Alignment, n * sizeof(T))) {
            ptr = nullptr;
        }
#endif

        if (!ptr) {
            throw std::bad_alloc();
        }

        return static_cast<pointer>(ptr);
    }

    void deallocate(pointer p, size_type) noexcept {
#if defined(_MSC_VER)
        _aligned_free(p);
#else
        free(p);
#endif
    }

    // Required for C++11 allocator compatibility
    bool operator==(const AlignedAllocator&) const noexcept { return true; }
    bool operator!=(const AlignedAllocator&) const noexcept { return false; }
};

// Use the aligned allocator with vectors for SIMD-friendly memory access
template<typename T, std::size_t Alignment = 32>  // 32-byte alignment for AVX
using AlignedVector = std::vector<T, AlignedAllocator<T, Alignment>>;

}

#endif // ALIGNEDALLOCATOR_H
