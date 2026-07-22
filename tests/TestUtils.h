#ifndef AURA_TEST_UTILS_H
#define AURA_TEST_UTILS_H

#pragma once

#include <iostream>
#include <string>

// Minimal CTest-friendly check macros. No external framework: matches the
// engine's vendor-header-only dependency style. A test binary exits non-zero
// (and CTest reports FAILED) if any AURA_CHECK* failed, printing every
// failure rather than stopping at the first one.
//
// Deliberately not built on assert(): assert() compiles to nothing under
// NDEBUG, which would silently turn every Release-configured test into a
// no-op.

namespace aura3d::test {

inline int& failureCount()
{
    static int count = 0;
    return count;
}

inline void check(bool condition, const std::string& description, const char* file, int line)
{
    if (condition) {
        std::cout << "[PASS] " << description << "\n";
    } else {
        std::cerr << "[FAIL] " << description << " (" << file << ":" << line << ")\n";
        ++failureCount();
    }
}

} // namespace aura3d::test

#define AURA_CHECK(cond, description) \
    ::aura3d::test::check((cond), (description), __FILE__, __LINE__)

// Runs `expr` and checks that it throws ExceptionType (exactly, via catch-by-base
// as usual). Any other exception, or none at all, counts as a failure.
#define AURA_CHECK_THROWS(expr, ExceptionType, description)                 \
    do {                                                                    \
        bool threw = false;                                                 \
        try {                                                               \
            (void)(expr);                                                   \
        } catch (const ExceptionType&) {                                    \
            threw = true;                                                   \
        } catch (...) {                                                     \
            threw = false;                                                  \
        }                                                                   \
        ::aura3d::test::check(threw, description, __FILE__, __LINE__);      \
    } while (0)

#define AURA_TEST_MAIN_RETURN() \
    return ::aura3d::test::failureCount() == 0 ? 0 : 1

#endif // AURA_TEST_UTILS_H
