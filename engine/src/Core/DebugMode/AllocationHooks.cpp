#include "aura/Core/DebugMode/AllocationTracker.h"

#ifdef AURA_ENABLE_DEBUG_MODE
#include <cstddef>
#include <cstdlib>
#include <limits>
#include <new>

namespace {
struct BlockHeader
{
    usize bytes;
    u32   offset;
    u32   counted;
};

static_assert(sizeof(BlockHeader) <= std::numeric_limits<u32>::max() / 2,
              "BlockHeader must fit comfortably in the 32-bit offset field");

constexpr usize kMinAlign = alignof(BlockHeader);

[[nodiscard]] constexpr usize roundUp(usize value, usize alignment) noexcept
{
    return (value + alignment - 1) & ~(alignment - 1);
}

[[nodiscard]] void* tryAllocate(usize size, usize alignment) noexcept
{
    const usize align = alignment < kMinAlign ? kMinAlign : alignment;

    const usize overhead = sizeof(BlockHeader) + align - 1;
    if (size > std::numeric_limits<usize>::max() - overhead)
        return nullptr;

    auto* base = static_cast<std::byte*>(std::malloc(size + overhead));
    if (base == nullptr)
        return nullptr;

    auto* payload = reinterpret_cast<std::byte*>(
        roundUp(reinterpret_cast<usize>(base) + sizeof(BlockHeader), align));

    const auto offset = static_cast<usize>(payload - base);

    if (offset > std::numeric_limits<u32>::max())
    {
        std::free(base);
        return nullptr;
    }

    auto* header = reinterpret_cast<BlockHeader*>(payload - sizeof(BlockHeader));
    header->bytes  = size;
    header->offset = static_cast<u32>(offset);

    if (aura3d::AllocationTracker::muted())
    {
        header->counted = 0;
    }
    else
    {
        header->counted = 1;
        aura3d::AllocationTracker::get().recordAllocation(size);
    }

    return payload;
}

[[nodiscard]] void* allocateOrThrow(usize size, usize alignment)
{
    for (;;)
    {
        if (void* payload = tryAllocate(size, alignment))
            return payload;

        std::new_handler handler = std::get_new_handler();
        if (handler == nullptr)
            throw std::bad_alloc();

        handler();
    }
}

[[nodiscard]] void* allocateNoThrow(usize size, usize alignment) noexcept
{
    try
    {
        return allocateOrThrow(size, alignment);
    }
    catch (...)
    {
        return nullptr;
    }
}

void deallocate(void* payload) noexcept
{
    if (payload == nullptr)
        return;

    auto* bytes  = static_cast<std::byte*>(payload);
    auto* header = reinterpret_cast<BlockHeader*>(bytes - sizeof(BlockHeader));

    if (header->counted != 0)
        aura3d::AllocationTracker::get().recordFree(header->bytes);

    std::free(bytes - header->offset);
}

} // namespace

void* operator new(std::size_t size)                                  { return allocateOrThrow(size, 0); }
void* operator new[](std::size_t size)                                { return allocateOrThrow(size, 0); }

void* operator new(std::size_t size, std::align_val_t alignment)
{
    return allocateOrThrow(size, static_cast<usize>(alignment));
}

void* operator new[](std::size_t size, std::align_val_t alignment)
{
    return allocateOrThrow(size, static_cast<usize>(alignment));
}

void* operator new(std::size_t size, const std::nothrow_t&) noexcept   { return allocateNoThrow(size, 0); }
void* operator new[](std::size_t size, const std::nothrow_t&) noexcept { return allocateNoThrow(size, 0); }

void* operator new(std::size_t size, std::align_val_t alignment, const std::nothrow_t&) noexcept
{
    return allocateNoThrow(size, static_cast<usize>(alignment));
}

void* operator new[](std::size_t size, std::align_val_t alignment, const std::nothrow_t&) noexcept
{
    return allocateNoThrow(size, static_cast<usize>(alignment));
}

void operator delete(void* payload) noexcept                             { deallocate(payload); }
void operator delete[](void* payload) noexcept                           { deallocate(payload); }
void operator delete(void* payload, std::size_t) noexcept                { deallocate(payload); }
void operator delete[](void* payload, std::size_t) noexcept              { deallocate(payload); }
void operator delete(void* payload, std::align_val_t) noexcept           { deallocate(payload); }
void operator delete[](void* payload, std::align_val_t) noexcept         { deallocate(payload); }
void operator delete(void* payload, std::size_t, std::align_val_t) noexcept   { deallocate(payload); }
void operator delete[](void* payload, std::size_t, std::align_val_t) noexcept { deallocate(payload); }
void operator delete(void* payload, const std::nothrow_t&) noexcept      { deallocate(payload); }
void operator delete[](void* payload, const std::nothrow_t&) noexcept    { deallocate(payload); }

void operator delete(void* payload, std::align_val_t, const std::nothrow_t&) noexcept
{
    deallocate(payload);
}

void operator delete[](void* payload, std::align_val_t, const std::nothrow_t&) noexcept
{
    deallocate(payload);
}

#endif
