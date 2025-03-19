#ifndef AURAASSERT_H
#define AURAASSERT_H

#pragma once

#include <cstring>

#include "AuraLogger/AuraLogger.h"

// Configuration macros
#if defined(AURA_CONFIG_DIST)
#define AURA_DISABLE_ASSERTS
#endif

inline void ReportAssertionFailure(const char* expression, const std::string& message, const char* file, int line) {
    // Extract filename from path
    const char* filename = file;
    const char* lastSlash = strrchr(file, '/');
    const char* lastBackslash = strrchr(file, '\\');

    if (lastSlash != nullptr) {
        filename = lastSlash + 1;
    } else if (lastBackslash != nullptr) {
        filename = lastBackslash + 1;
    }

    const char* msg = message.c_str();

    // Print to stderr with colors
    fprintf(stderr, "%s%sASSERTION FAILED: %s%s\n",
            aura3d::Logger::Colors::BOLD, aura3d::Logger::Colors::RED,
            expression, aura3d::Logger::Colors::RESET);

    if (msg && msg[0] != '\0') {
        fprintf(stderr, "%s%sMessage: %s%s\n",
                aura3d::Logger::Colors::BOLD, aura3d::Logger::Colors::RED,
                msg, aura3d::Logger::Colors::RESET);
    }

    fprintf(stderr, "%s%sLocation: %s:%d%s\n",
            aura3d::Logger::Colors::BOLD, aura3d::Logger::Colors::RED,
            filename, line, aura3d::Logger::Colors::RESET);

// Break into the debugger if available
#if defined(_MSC_VER)
    __debugbreak();
#elif defined(__GNUC__) || defined(__clang__)
#if defined(__x86_64__) || defined(__i386__)
    __asm__ volatile("int $3");
#else
    raise(SIGTRAP);
#endif
#else
    // Fallback using signal
    raise(SIGABRT);
#endif
}

/**
     * Basic assertion that breaks if the condition is false
     */
#define AURA_ASSERT(condition) \
do { \
        if (!(condition)) { \
            ReportAssertionFailure(#condition, "Condition Not Satisfied!", __FILE__, __LINE__); \
    } \
} while (false)

/**
     * Assertion with custom message
     */
#define AURA_ASSERT_MSG(condition, message) \
    do { \
        if (!(condition)) { \
            ReportAssertionFailure(#condition, message, __FILE__, __LINE__); \
    } \
} while (false)

#endif // AURAASSERT_H
