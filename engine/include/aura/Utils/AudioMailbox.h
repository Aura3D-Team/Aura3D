#pragma once

#include <array>
#include <atomic>
#include <type_traits>

namespace aura3d {

/// Single-producer, single-consumer latest-value slot for the audio thread.
///
/// A triple buffer: the producer always has a free slot to write, the consumer
/// always reads the newest complete value, and neither side ever waits. Updates
/// published between two consumes coalesce -- only the last one is seen --
/// which is the intended contract for control state (gain, position, play/stop).
///
/// The consumer is the real-time thread, so @p T must be trivially copyable:
/// the copy in consume() is a memcpy with no allocation and no destructor.
template<class T>
class AudioMailbox {
    static_assert(std::is_trivially_copyable_v<T>);
    static_assert(std::atomic<unsigned>::is_always_lock_free);
public:
    /// Replaces the pending value, overwriting one not yet consumed rather
    /// than queueing behind it. Producer thread only.
    void publish(const T& value) noexcept
    {
        _values[_write] = value;
        _write = _middle.exchange(_write | kReady, std::memory_order_acq_rel) & kIndex;
    }

    /// Takes the newest published value into @p value. Consumer thread only.
    /// @return `true` when @p value was written; `false` leaves it untouched.
    bool consume(T& value) noexcept
    {
        if (!(_middle.load(std::memory_order_acquire) & kReady)) return false;
        _read = _middle.exchange(_read, std::memory_order_acq_rel) & kIndex;
        value = _values[_read];
        return true;
    }

private:
    /// kReady flags an unconsumed value in the middle slot; kIndex masks its index.
    static constexpr unsigned kReady = 4, kIndex = 3;
    /// One slot each for the writer, the reader and the value in transit.
    std::array<T, 3> _values{};
    /// Index of the slot in transit, with kReady set once it holds a fresh value.
    std::atomic<unsigned> _middle{1};
    /// Slot the producer writes next; swapped with the middle on publish().
    unsigned _write = 2;
    /// Slot the consumer last took; swapped with the middle on consume().
    unsigned _read = 0;
};

} // namespace aura3d
