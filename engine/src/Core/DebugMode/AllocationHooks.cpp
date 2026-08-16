#include "aura/Core/DebugMode/AllocationTracker.h"

/**
 * @file AllocationHooks.cpp
 * @brief Replacement global operator new/delete that feed AllocationTracker.
 *
 * Entirely inside AURA_ENABLE_DEBUG_MODE: replacing the global allocation
 * functions is a whole-program decision, and a translation unit that defines
 * them is one a release build must not link. With the switch off this file
 * compiles to nothing at all, which is what makes the engine's "no manual
 * new/delete" rule a statement about engine code rather than one this file
 * would have to break.
 *
 * ## Why a header rather than sized deallocation
 *
 * C++14's sized `operator delete(void*, size_t)` looks like it makes the free
 * path free, but the unsized overload still has to work -- it is what gets
 * called for a deletion through an incomplete type, for arrays whose element
 * type has no destructor, and by any library compiled without sized
 * deallocation. An unsized free that cannot name the size can only guess, and a
 * guess makes the live-bytes counter drift without bound over a long run, which
 * is precisely the measurement this exists to make.
 *
 * So every block carries a 16-byte header immediately in front of the payload
 * holding the exact requested size. The sized overloads then ignore the size
 * the compiler passes and read the header too, so both paths agree by
 * construction.
 *
 * ## Why alignment is done by hand
 *
 * The payload is placed at the first correctly aligned address past the header
 * inside one plain `std::malloc` block, and the base-to-payload distance is
 * recorded in the header. No `std::aligned_alloc` (C11, and its
 * size-multiple-of-alignment precondition varies by libc), no `_aligned_malloc`
 * (MSVC-only, and pairs with `_aligned_free` rather than `free`), no
 * assumption that `__STDCPP_DEFAULT_NEW_ALIGNMENT__` is within what malloc
 * guarantees. One code path covers Linux, Windows, Android and WASM, and every
 * alignment an over-aligned type can ask for.
 */

#ifdef AURA_ENABLE_DEBUG_MODE

#include <cstddef>
#include <cstdlib>
#include <limits>
#include <new>

namespace {

/**
 * @brief Bookkeeping stored immediately in front of every payload.
 *
 * Deliberately no fixed size: 16 bytes on a 64-bit target and 12 on a 32-bit
 * one (wasm32, armeabi-v7a), because @c usize is @c size_t. Nothing depends on
 * which. Reading the header back needs only that @c payload - sizeof(header)
 * stays header-aligned, and that follows from two facts that hold everywhere:
 * the payload is aligned to at least kMinAlign below, and @c sizeof is always a
 * multiple of @c alignof.
 */
struct BlockHeader
{
    usize bytes;    ///< Payload size exactly as the caller requested it.
    u32   offset;   ///< Distance from the std::malloc base to the payload.
    u32   counted;  ///< 1 when the allocation was recorded; see the mute note.
};

//! The offset field is 32 bits, so a header too large to describe its own
//! placement would silently truncate into a wild free. Cannot happen as written;
//! would matter to anyone who adds a field.
static_assert(sizeof(BlockHeader) <= std::numeric_limits<u32>::max() / 2,
              "BlockHeader must fit comfortably in the 32-bit offset field");

//! Header loads must be aligned; the payload alignment is therefore floored at
//! the header's own, whatever the caller asked for.
constexpr usize kMinAlign = alignof(BlockHeader);

//! Rounds @p value up to the next multiple of the power of two @p alignment.
[[nodiscard]] constexpr usize roundUp(usize value, usize alignment) noexcept
{
    return (value + alignment - 1) & ~(alignment - 1);
}

/**
 * @brief One malloc block, laid out as [padding][header][payload].
 *
 * @return nullptr when the request cannot be satisfied (including a size whose
 *         overhead would overflow), leaving the caller to run the new-handler
 *         protocol.
 */
[[nodiscard]] void* tryAllocate(usize size, usize alignment) noexcept
{
    const usize align = alignment < kMinAlign ? kMinAlign : alignment;

    //! Worst case the payload lands sizeof(BlockHeader) + align - 1 past the
    //! base, so that much has to be reserved on top of the payload itself.
    const usize overhead = sizeof(BlockHeader) + align - 1;
    if (size > std::numeric_limits<usize>::max() - overhead)
        return nullptr;

    auto* base = static_cast<std::byte*>(std::malloc(size + overhead));
    if (base == nullptr)
        return nullptr;

    auto* payload = reinterpret_cast<std::byte*>(
        roundUp(reinterpret_cast<usize>(base) + sizeof(BlockHeader), align));

    const auto offset = static_cast<usize>(payload - base);

    /*
     * Cannot happen for any alignment a type can actually request (the offset
     * is bounded by sizeof(BlockHeader) + align - 1, and align is an
     * alignof()), but the header field is 32 bits and silently truncating it
     * would turn into a wild free rather than a failed allocation.
     */
    if (offset > std::numeric_limits<u32>::max())
    {
        std::free(base);
        return nullptr;
    }

    auto* header = reinterpret_cast<BlockHeader*>(payload - sizeof(BlockHeader));
    header->bytes  = size;
    header->offset = static_cast<u32>(offset);

    /*
     * Whether this allocation was counted is recorded per block rather than
     * re-derived at free time: the mute flag is per thread and scoped, so a
     * block allocated inside the report writer can easily be freed outside it.
     * Counting only the half that fell outside would walk the live-bytes
     * counter downwards forever.
     */
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

//! The full operator new contract: retry while a new-handler is installed and
//! keeps returning, then give up.
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

//! As allocateOrThrow(), reporting exhaustion as nullptr. A new-handler is
//! still allowed to throw, and that must not escape a noexcept operator new.
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

//! The single free path every operator delete overload routes through: the
//! header knows the exact size and the base offset, so no caller has to.
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

/*
 * The replacement set, in full. Partial replacement is the classic way to get a
 * malloc'd pointer into a free() that expects a header: leaving, say, the
 * array forms to the default implementation would mean `new[]` returns a
 * pointer this file never framed and `delete[]` reads a header that was never
 * written. Every overload the standard defines is therefore replaced here, and
 * every one of them routes through the same two functions above.
 */

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

/*
 * The sized and aligned delete overloads discard both the size and the
 * alignment the compiler passes. Not an oversight: the header already holds the
 * exact requested size and the exact base offset, and trusting those keeps the
 * sized and unsized paths from ever disagreeing about how many bytes a block
 * was.
 */

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

#endif // AURA_ENABLE_DEBUG_MODE
