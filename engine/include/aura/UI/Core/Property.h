#ifndef AURA_UI_PROPERTY_H
#define AURA_UI_PROPERTY_H

#pragma once

#include <concepts>
#include <memory>
#include <utility>

#include "aura/UI/Core/Signal.h"

/**
 * @file Property.h
 * @brief An observable value, as a widget's public state.
 *
 * @code
 * slider.value = 0.75f;                                  // assign
 * const f32 v = slider.value;                            // read
 * slider.value.changed().connect([](f32 v) { ... });      // observe
 * @endcode
 *
 * The change signal is created on first use, so a property nobody watches
 * costs one null pointer beyond the value itself -- which is what makes it
 * affordable to expose every piece of widget state this way.
 */

namespace aura3d::ui {

/**
 * @class Property
 * @brief A value that reports its own changes.
 *
 * Assignment of an equal value is a no-op: no signal, so an observer wired to
 * a relayout is not woken by code that writes the same number every frame.
 * Types without @c operator== skip that gate and always notify.
 */
template <class T>
class Property {
public:
    using Observer = Signal<const T&>;

    Property() = default;
    explicit Property(T value) noexcept(std::is_nothrow_move_constructible_v<T>)
        : _value(std::move(value))
    {
    }

    Property(const Property&) = delete;
    Property& operator=(const Property& other) = delete;

    [[nodiscard]] const T& get() const noexcept { return _value; }
    [[nodiscard]] operator const T&() const noexcept { return _value; }
    [[nodiscard]] const T* operator->() const noexcept { return &_value; }

    /// @param notify False writes the value without waking observers -- for a
    ///        widget correcting its own state mid-edit, where the one
    ///        notification the outside world should see comes later.
    /// @return True when the stored value actually changed.
    bool set(T value, bool notify = true)
    {
        if constexpr (std::equality_comparable<T>)
        {
            if (_value == value)
                return false;
        }

        _value = std::move(value);

        if (notify && _changed)
            _changed->emit(_value);

        return true;
    }

    Property& operator=(T value)
    {
        set(std::move(value));
        return *this;
    }

    /**
     * @brief Follows @p source until the returned connection is dropped.
     *
     * @code
     * ScopedConnection link = label.text.bind(field.text);
     * @endcode
     *
     * One-way and immediate: the current value is taken now, and every later
     * change to @p source lands here. Keep the connection alive for as long as
     * the binding should hold -- letting it die is how a binding is undone.
     */
    [[nodiscard]] ScopedConnection bind(Property& source)
    {
        set(source.get());
        return source.changed().connect([this](const T& value) { set(value); });
    }

    /// As @ref bind, through a conversion -- binding a label's text to a
    /// slider's value, say.
    template <class U, class Fn>
    [[nodiscard]] ScopedConnection bindFrom(Property<U>& source, Fn transform)
    {
        set(transform(source.get()));

        return source.changed().connect(
            [this, transform = std::move(transform)](const U& value) { set(transform(value)); });
    }

    /// The change signal, created on first call.
    [[nodiscard]] Observer& changed()
    {
        if (!_changed)
            _changed = std::make_unique<Observer>();

        return *_changed;
    }

private:
    T _value{};

    //! Null until someone asks for changed(). A widget exposing a dozen
    //! properties therefore allocates nothing for the ones nobody watches.
    std::unique_ptr<Observer> _changed;
};

} // namespace aura3d::ui

#endif // AURA_UI_PROPERTY_H
