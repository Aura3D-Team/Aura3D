#ifndef AURA_CORE_HANDLE_H
#define AURA_CORE_HANDLE_H

#pragma once

#include <cstdint>
#include <functional>
#include <limits>
#include <ostream>

#include "aura/aura.h"

namespace aura3d {

/**
 * @brief A type-safe, opaque reference to an engine-owned resource.
 *
 * Every resource kind (a texture, a vertex buffer, an audio clip, ...) gets
 * its own instantiation via a distinct @p Tag, so the compiler rejects a
 * TextureHandle passed where a VertexBufferHandle is expected -- a class of
 * bug a bare `u32` cannot catch, and one this engine used to be exposed to:
 * every handle alias in RenderHandles.h/AudioHandles.h was previously
 * `using X = u32`, so nothing stopped a mesh handle from being handed to an
 * API expecting a material handle.
 *
 * The wrapped value has no meaning outside the subsystem that issued it --
 * callers pass a Handle back to the API that created it and never interpret
 * the number itself. Default-constructed to the invalid handle, so
 * `Handle<Tag>{}` and `return {};` already mean "no resource"; see isValid()
 * and the free function isValidHandle() below.
 *
 * Deliberately arithmetic-free beyond ++ (see below): a Handle compares
 * equal/unequal and converts explicitly to/from its raw value (value() /
 * `Handle{u32}`). Code that needs to derive a storage index or pack extra
 * bits into a handle (see AudioEngine's slot/generation encoding, or a
 * backend's `vector[handle.value() - 1]` 1-based pool) does so on the raw
 * value at that one call site rather than through operators legal -- and
 * easy to misuse -- everywhere a handle is merely held or passed around.
 *
 * @tparam Tag Unique, never-defined type that distinguishes one resource
 *             family from another at compile time (e.g. `struct TextureTag;`).
 *             Only ever used as a template parameter, so it need not be a
 *             complete type.
 */
template <typename Tag>
class Handle
{
public:
    using ValueType = u32;

    //! Sentinel for "no resource". isValid() also rejects the numeric 0 (see
    //! below), so a default-constructed handle and a zero-valued one both
    //! read as absent -- matching every pool's 1-based indexing convention,
    //! where index 0 is reserved so a valid handle can never collide with it.
    static constexpr ValueType kInvalidValue = std::numeric_limits<ValueType>::max();

    constexpr Handle() noexcept = default;

    //! Explicit: building a handle from a raw integer is a deliberate act --
    //! issuing one from a pool, decoding one from storage -- never an
    //! implicit fallback from an unrelated integer.
    constexpr explicit Handle(ValueType value) noexcept : _value(value) {}

    [[nodiscard]] constexpr ValueType value() const noexcept { return _value; }

    //! False for both the sentinel and 0; see kInvalidValue.
    [[nodiscard]] constexpr bool isValid() const noexcept
    {
        return _value != kInvalidValue && _value != 0;
    }

    friend constexpr bool operator==(const Handle&, const Handle&) noexcept = default;

    //! Pre/post increment only, for the one legitimate arithmetic use a
    //! handle has in this engine: a monotonically issued pool counter (e.g.
    //! `auto handle = _nextHandle++;`). Nothing past this is exposed --
    //! there is no `+`, `-`, `<`, or bitwise operator -- so a handle cannot
    //! be treated as a general-purpose integer by accident.
    constexpr Handle& operator++() noexcept
    {
        ++_value;
        return *this;
    }

    constexpr Handle operator++(int) noexcept
    {
        const Handle previous = *this;
        ++_value;
        return previous;
    }

    friend std::ostream& operator<<(std::ostream& os, const Handle& handle)
    {
        return os << handle._value;
    }

private:
    ValueType _value = kInvalidValue;
};

/**
 * @brief True when @p handle refers to a resource rather than the sentinel.
 *
 * Free-function spelling kept (rather than requiring `handle.isValid()`
 * everywhere) because it is already the idiom used throughout the engine.
 */
template <typename Tag>
[[nodiscard]] constexpr bool isValidHandle(Handle<Tag> handle) noexcept
{
    return handle.isValid();
}

} // namespace aura3d

template <typename Tag>
struct std::hash<aura3d::Handle<Tag>>
{
    [[nodiscard]] size_t operator()(const aura3d::Handle<Tag>& handle) const noexcept
    {
        return std::hash<typename aura3d::Handle<Tag>::ValueType>{}(handle.value());
    }
};

#endif // AURA_CORE_HANDLE_H
