#ifndef AURA_PLATFORM_COMPAT_H
#define AURA_PLATFORM_COMPAT_H

#pragma once

#include <functional>

namespace aura3d {

/*
 * move_only_function<Sig>
 *
 * C++23 std::move_only_function allows storing move-only callables (e.g.
 * lambdas capturing unique_ptr). On toolchains that do not yet provide it
 * (NDK clang on older Android APIs, Emscripten with an older libc++) we fall
 * back to std::function, which requires a copyable callable.
 *
 * Usage:
 *   aura3d::move_only_function<void()> onFrame = std::move(myLambda);
 */
#if defined(__cpp_lib_move_only_function)
template <typename Sig>
using move_only_function = std::move_only_function<Sig>;
#else
template <typename Sig>
using move_only_function = std::function<Sig>;
#endif

} // namespace aura3d

#endif // AURA_PLATFORM_COMPAT_H
