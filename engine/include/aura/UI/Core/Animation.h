#ifndef AURA_UI_ANIMATION_H
#define AURA_UI_ANIMATION_H

#pragma once

#include <algorithm>
#include <cmath>

#include <ink/ink_base.hpp>

/**
 * @file Animation.h
 * @brief Time-based interpolation, with no allocation and no scheduler.
 *
 * A @ref Transition is a value that walks to a target. Widgets own theirs and
 * tick them from Widget::onTick(); UIRoot only ticks the widgets that asked to
 * be ticked, so an idle UI does no per-frame work at all.
 *
 * @code
 * _hover.to(1.0f, 0.12f);                    // on pointer enter
 * const f32 t = _hover.value();              // in paint()
 * @endcode
 */

namespace aura3d::ui {

/// A curve, as a plain function pointer -- callable without an allocation or
/// an indirect object, which matters when every hovered control has one.
using EasingFn = f32 (*)(f32) noexcept;

namespace easing {

[[nodiscard]] inline f32 linear(f32 t) noexcept { return t; }

[[nodiscard]] inline f32 inQuad(f32 t) noexcept { return t * t; }

[[nodiscard]] inline f32 outQuad(f32 t) noexcept { return t * (2.0f - t); }

[[nodiscard]] inline f32 inOutQuad(f32 t) noexcept
{
    return t < 0.5f ? 2.0f * t * t : -1.0f + (4.0f - 2.0f * t) * t;
}

[[nodiscard]] inline f32 outCubic(f32 t) noexcept
{
    const f32 f = t - 1.0f;
    return f * f * f + 1.0f;
}

[[nodiscard]] inline f32 inOutCubic(f32 t) noexcept
{
    return t < 0.5f ? 4.0f * t * t * t : 1.0f - std::pow(-2.0f * t + 2.0f, 3.0f) * 0.5f;
}

/// Overshoots and settles. For something appearing, never for a value the user
/// is dragging.
[[nodiscard]] inline f32 outBack(f32 t) noexcept
{
    constexpr f32 kOvershoot = 1.70158f;
    const f32 f = t - 1.0f;
    return 1.0f + (kOvershoot + 1.0f) * f * f * f + kOvershoot * f * f;
}

} // namespace easing

/**
 * @class Transition
 * @brief A value that eases towards a target over a fixed duration.
 *
 * Retargeting mid-flight restarts the curve from wherever the value currently
 * is, so a control hovered and unhovered quickly never jumps.
 */
template <class T>
class Transition {
public:
    Transition() = default;
    explicit Transition(T value) noexcept : _value(value), _target(value), _from(value) {}

    /// Jumps to @p value, cancelling any animation.
    void reset(T value) noexcept
    {
        _value = value;
        _target = value;
        _from = value;
        _elapsed = 0.0f;
        _duration = 0.0f;
    }

    void to(T target, f32 seconds, EasingFn curve = easing::outCubic) noexcept
    {
        if (target == _target && running())
            return;

        if (seconds <= 0.0f)
        {
            reset(target);
            return;
        }

        _from = _value;
        _target = target;
        _duration = seconds;
        _elapsed = 0.0f;
        _curve = curve;
    }

    /// @return True while the animation is still running, which is what a
    ///         widget returns from onTick() to keep being ticked.
    bool tick(f32 deltaSeconds) noexcept
    {
        if (!running())
            return false;

        _elapsed += deltaSeconds;

        if (_elapsed >= _duration)
        {
            _value = _target;
            _duration = 0.0f;
            return false;
        }

        const f32 t = _curve(std::clamp(_elapsed / _duration, 0.0f, 1.0f));
        _value = _from + (_target - _from) * t;
        return true;
    }

    [[nodiscard]] const T& value() const noexcept { return _value; }
    [[nodiscard]] const T& target() const noexcept { return _target; }
    [[nodiscard]] bool running() const noexcept { return _duration > 0.0f; }

private:
    T _value{};
    T _target{};
    T _from{};

    f32 _elapsed = 0.0f;
    f32 _duration = 0.0f; //! Zero means "not running"; no separate flag to disagree with it.

    EasingFn _curve = easing::outCubic;
};

} // namespace aura3d::ui

#endif // AURA_UI_ANIMATION_H
