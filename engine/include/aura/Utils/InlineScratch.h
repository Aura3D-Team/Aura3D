#pragma once

#include <array>
#include <memory_resource>
#include <vector>

namespace aura3d {

/// A vector whose first @p Count elements live on the stack.
///
/// For the short-lived snapshots UI dispatch takes (hover chains, child lists)
/// a heap vector per event was the dominant allocation. A stack-backed one
/// costs nothing in the common case and still spills to the heap when a
/// snapshot is larger. Being a local, not a shared pool, it stays correct
/// when a callback re-enters the same code path: every nesting level owns its
/// own storage, and elements are destroyed when the scope ends.
template<class T, std::size_t Count = 16>
class InlineScratch {
    /// Backing bytes for the first @p Count elements.
    alignas(T) std::array<std::byte, sizeof(T) * Count> _storage;
    /// Hands out @c _storage first, then the default resource for overflow.
    std::pmr::monotonic_buffer_resource _resource{_storage.data(), _storage.size()};
public:
    /// The scratch container. Use it like any vector.
    std::pmr::vector<T> values{&_resource};
    /// Reserves the inline capacity so the first @p Count pushes never spill.
    InlineScratch() { values.reserve(Count); }
};

} // namespace aura3d
