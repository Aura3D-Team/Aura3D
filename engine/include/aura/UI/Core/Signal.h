#ifndef AURA_UI_SIGNAL_H
#define AURA_UI_SIGNAL_H

#pragma once

#include <algorithm>
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
    bool (*contains)(void* owner, u64 id) noexcept = nullptr;
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
        return hub && hub->owner && hub->contains(hub->owner, _id);
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

    Signal() = default;
    ~Signal() { _invalidate(); }

    Signal(const Signal&) = delete;
    Signal& operator=(const Signal&) = delete;
    Signal(Signal&& other) noexcept : _state(std::move(other._state)) {}
    Signal& operator=(Signal&& other) noexcept
    {
        if (this != &other)
        {
            _invalidate();
            _state = std::move(other._state);
        }
        return *this;
    }

    Connection connect(Slot slot)
    {
        if (!slot)
            return {};
        if (!_state)
            _state = std::make_shared<State>();
        const u64 id = _state->nextId++;
        _state->slots.push_back(std::make_shared<Entry>(id, std::move(slot), true));
        return Connection{_state->hub, id};
    }

    void disconnect(const Connection& connection) noexcept { connection.disconnect(); }

    void clear() noexcept
    {
        if (!_state)
            return;
        for (const auto& entry : _state->slots)
            entry->connected = false;
        if (_state->emitting == 0)
            _state->slots.clear();
    }

    /// New slots wait for the next emit; disconnected slots are skipped.
    void emit(Args... args)
    {
        // Both the signal and the active callback may be removed by a slot.
        const auto state = _state;
        if (!state)
            return;
        struct Emission {
            State& state;
            explicit Emission(State& value) : state(value) { ++state.emitting; }
            ~Emission()
            {
                if (--state.emitting == 0)
                    std::erase_if(state.slots, [](const auto& entry) { return !entry->connected; });
            }
        } emission(*state);

        const usize count = state->slots.size();
        for (usize i = 0; i < count && state->hub->owner; ++i)
        {
            const auto entry = state->slots[i];
            if (entry->connected)
                entry->slot(args...);
        }
    }

    void operator()(Args... args) { emit(args...); }
    [[nodiscard]] bool empty() const noexcept
    {
        return !_state || std::ranges::none_of(_state->slots,
            [](const auto& entry) { return entry->connected; });
    }

private:
    struct Entry {
        u64 id;
        Slot slot;
        bool connected;
    };

    struct State {
        std::vector<std::shared_ptr<Entry>> slots;
        u64 nextId = 1;
        u32 emitting = 0;
        std::shared_ptr<detail::SignalHub> hub =
            std::make_shared<detail::SignalHub>(this, &Signal::_drop, &Signal::_contains);
    };

    static bool _contains(void* owner, u64 id) noexcept
    {
        const auto& state = *static_cast<State*>(owner);
        return std::ranges::any_of(state.slots,
            [id](const auto& entry) { return entry->id == id && entry->connected; });
    }

    static void _drop(void* owner, u64 id) noexcept
    {
        auto& state = *static_cast<State*>(owner);
        for (const auto& entry : state.slots)
            if (entry->id == id)
                entry->connected = false;
        if (state.emitting == 0)
            std::erase_if(state.slots, [](const auto& entry) { return !entry->connected; });
    }

    void _invalidate() noexcept
    {
        if (_state)
            _state->hub->owner = nullptr;
    }

    std::shared_ptr<State> _state;
};

} // namespace aura3d::ui

#endif // AURA_UI_SIGNAL_H
