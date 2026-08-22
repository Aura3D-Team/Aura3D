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
 * Each resource kind (texture, vertex buffer, audio clip, ...) instantiates
 * this with a distinct @p Tag, so the compiler rejects a TextureHandle passed
 * where a VertexBufferHandle is expected -- unlike a bare `u32`.
 *
 * The wrapped value has no meaning outside the subsystem that issued it.
 * Default-constructed to the invalid handle, so `Handle<Tag>{}` already means
 * "no resource"; see isValid() / isValidHandle().
 *
 * No arithmetic beyond ++ (for issuing handles from a pool counter). Code that
 * needs a raw storage index does so explicitly via value().
 *
 * @tparam Tag Unique, never-defined type distinguishing one resource family
 *             from another (e.g. `struct TextureTag;`). Used only as a
 *             template parameter, so it need not be complete.
 */
template <typename Tag>
class Handle
{
public:
    using ValueType = u32;

    //! Sentinel for "no resource". isValid() also rejects 0, matching every
    //! pool's 1-based indexing (index 0 reserved, never a valid handle).
    static constexpr ValueType kInvalidValue = std::numeric_limits<ValueType>::max();

    constexpr Handle() noexcept = default;

    //! Explicit: constructing from a raw integer is always a deliberate act.
    constexpr explicit Handle(ValueType value) noexcept : _value(value) {}

    [[nodiscard]] constexpr ValueType value() const noexcept { return _value; }

    [[nodiscard]] constexpr bool isValid() const noexcept
    {
        return _value != kInvalidValue && _value != 0;
    }

    friend constexpr bool operator==(const Handle&, const Handle&) noexcept = default;

    //! For issuing handles from a pool counter (`auto handle = _nextHandle++;`).
    //! No other arithmetic operator is exposed.
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

/// True when @p handle refers to a resource rather than the sentinel.
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
