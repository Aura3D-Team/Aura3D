#ifndef AURA_UI_SIGNAL_H
#define AURA_UI_SIGNAL_H

#pragma once

#include <functional>
#include <memory>
#include <utility>
#include <vector>

#include <ink/ink_base.hpp>

/**
 * @file Signal.h
 * @brief One-to-many callbacks with a disconnect that stays safe.
 *
 * @code
 * button.clicked.connect([] { launch(); });
 *
 * aui::ScopedConnection sub = slider.valueChanged.connect(
 *     [&](f32 v) { exposure = v; });   // disconnected when `sub` dies
 * @endcode
 *
 * Two hazards a naive vector-of-callbacks gets wrong, both handled here: a
 * slot that disconnects (or connects) *during* an emit, and a Connection that
 * outlives the Signal it came from.
 */

namespace aura3d::ui {

namespace detail {

/// The indirection a @ref Connection actually holds. Type-erased so
/// Connection can be a plain class while Signal is a template, and shared so a
/// Connection outliving its Signal sees @c owner nulled rather than a dangling
/// pointer.
struct SignalHub {
    void* owner = nullptr;
    void (*drop)(void* owner, u64 id) noexcept = nullptr;
};

} // namespace detail

/**
 * @class Connection
 * @brief A handle to one connected slot. Copyable, and safe to keep forever.
 *
 * Disconnecting through a Connection whose Signal is already gone does
 * nothing, so an owner does not have to outlive-check by hand.
 */
class Connection {
public:
    Connection() noexcept = default;

    void disconnect() const noexcept
    {
        if (const auto hub = _hub.lock(); hub && hub->owner)
            hub->drop(hub->owner, _id);
    }

    /// True while both the Signal and this particular slot are alive.
    [[nodiscard]] bool connected() const noexcept
    {
        const auto hub = _hub.lock();
        return hub && hub->owner != nullptr && _id != 0;
    }

private:
    template <class...>
    friend class Signal;

    Connection(std::weak_ptr<detail::SignalHub> hub, u64 id) noexcept
        : _hub(std::move(hub)), _id(id)
    {
    }

    std::weak_ptr<detail::SignalHub> _hub;
    u64 _id = 0;
};

/**
 * @class ScopedConnection
 * @brief A @ref Connection that disconnects when it goes out of scope.
 *
 * What an observer that does not own the signal should hold: a lambda
 * capturing @c this stops being called the moment the capturing object dies.
 */
class ScopedConnection {
public:
    ScopedConnection() noexcept = default;
    ScopedConnection(Connection connection) noexcept : _connection(std::move(connection)) {}

    ~ScopedConnection() { _connection.disconnect(); }

    ScopedConnection(const ScopedConnection&) = delete;
    ScopedConnection& operator=(const ScopedConnection&) = delete;

    ScopedConnection(ScopedConnection&& other) noexcept
        : _connection(std::exchange(other._connection, Connection{}))
    {
    }

    ScopedConnection& operator=(ScopedConnection&& other) noexcept
    {
        if (this != &other)
        {
            _connection.disconnect();
            _connection = std::exchange(other._connection, Connection{});
        }
        return *this;
    }

    void disconnect() noexcept
    {
        _connection.disconnect();
        _connection = Connection{};
    }

private:
    Connection _connection;
};

/**
 * @class Signal
 * @brief The event a widget publishes: `clicked`, `valueChanged`, `toggled`.
 *
 * Declare the parameters the way a slot should receive them -- @c
 * Signal<const std::string&> passes by reference, @c Signal<f32> by value.
 *
 * @note Moving a Signal re-points its Connections at the new object, so a
 *       widget holding signals stays movable. Not copyable: two objects
 *       cannot share one slot list without ambiguity about which owns it.
 */
template <class... Args>
class Signal {
public:
    using Slot = std::function<void(Args...)>;

    Signal() : _hub(std::make_shared<detail::SignalHub>(this, &Signal::_drop)) {}

    ~Signal()
    {
        if (_hub)
            _hub->owner = nullptr;
    }

    Signal(const Signal&) = delete;
    Signal& operator=(const Signal&) = delete;

    Signal(Signal&& other) noexcept
        : _slots(std::move(other._slots)), _nextId(other._nextId), _hub(std::move(other._hub))
    {
        if (_hub)
            _hub->owner = this;
        other._slots.clear();
    }

    Signal& operator=(Signal&& other) noexcept
    {
        if (this != &other)
        {
            if (_hub)
                _hub->owner = nullptr;

            _slots = std::move(other._slots);
            _nextId = other._nextId;
            _hub = std::move(other._hub);

            if (_hub)
                _hub->owner = this;

            other._slots.clear();
        }
        return *this;
    }

    /// Connects @p slot and returns a handle to it. Not [[nodiscard]]: the
    /// usual case is a widget connecting to something it owns, where the
    /// connection dies with both ends and nobody needs the handle. Keep it
    /// (in a @ref ScopedConnection) only when the observer can outlive the
    /// signal.
    Connection connect(Slot slot)
    {
        if (!slot)
            return {};

        const u64 id = _nextId++;
        _slots.push_back(Entry{id, std::move(slot)});
        return Connection{_hub, id};
    }

    void disconnect(const Connection& connection) noexcept { connection.disconnect(); }

    void clear() noexcept
    {
        if (_emitting > 0)
        {
            for (Entry& entry : _slots)
                entry.slot = nullptr;
            _stale = true;
            return;
        }
        _slots.clear();
    }

    /**
     * @brief Calls every slot connected at the moment of the call.
     *
     * A slot connected by another slot runs on the *next* emit, not this one;
     * a slot disconnected by another slot is skipped even if it had not run
     * yet. Both are the behaviour that keeps re-entrancy predictable.
     */
    void emit(Args... args)
    {
        ++_emitting;

        //! Snapshot: connect() may push while this loop runs, and the new slot
        //! must not observe an event that predates it.
        const usize count = _slots.size();
        for (usize i = 0; i < count; ++i)
        {
            if (_slots[i].slot)
                _slots[i].slot(args...);
        }

        if (--_emitting == 0 && _stale)
        {
            std::erase_if(_slots, [](const Entry& entry) { return !entry.slot; });
            _stale = false;
        }
    }

    void operator()(Args... args) { emit(args...); }

    [[nodiscard]] bool empty() const noexcept { return _slots.empty(); }

private:
    struct Entry {
        u64 id = 0;
        Slot slot;
    };

    //! Erasure is deferred while an emit is on the stack: the loop indexes
    //! into _slots, so compacting under it would skip or repeat a slot.
    static void _drop(void* owner, u64 id) noexcept
    {
        auto* self = static_cast<Signal*>(owner);

        for (Entry& entry : self->_slots)
        {
            if (entry.id != id)
                continue;

            entry.slot = nullptr;

            if (self->_emitting > 0)
                self->_stale = true;
            else
                std::erase_if(self->_slots, [](const Entry& e) { return !e.slot; });
            return;
        }
    }

    std::vector<Entry> _slots;
    u64 _nextId = 1;
    u32 _emitting = 0;
    bool _stale = false;

    std::shared_ptr<detail::SignalHub> _hub;
};

} // namespace aura3d::ui

#endif // AURA_UI_SIGNAL_H
