#ifndef __khrplatform_h_
#define __khrplatform_h_

/*
** Copyright (c) 2008-2018 The Khronos Group Inc.
**
** Permission is hereby granted, free of charge, to any person obtaining a
** copy of this software and/or associated documentation files (the
** "Materials"), to deal in the Materials without restriction, including
** without limitation the rights to use, copy, modify, merge, publish,
** distribute, sublicense, and/or sell copies of the Materials, and to
** permit persons to whom the Materials are furnished to do so, subject to
** the following conditions:
**
** The above copyright notice and this permission notice shall be included
** in all copies or substantial portions of the Materials.
**
** THE MATERIALS ARE PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
** IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
** FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
** THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
** OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
** ARISING FROM, OUT OF OR IN CONNECTION WITH THE MATERIALS OR THE USE OR
** OTHER DEALINGS IN THE MATERIALS.
*/

/* Khronos platform-specific types and definitions.
 *
 * Adopters may modify this file to suit their platform. Adopters are
 * encouraged to submit platform specific modifications to the Khronos
 * group so that they can be included in future versions of this file.
 * Please submit changes by filing an issue or pull request on the
 * public Khronos Registry repository:
 * https://github.com/KhronosGroup/EGL-Registry/
 *
 * A predefined template exists for the platform-specific portions:
 * platform/khrplatform.h.template
 */

#if defined(__SCITECH_SNAP__) && !defined(KHR_PLATFORM_USE_INTTYPES)
#define KHR_PLATFORM_USE_INTTYPES
#endif

/*-------------------------------------------------------------------------
 * Basic type definitions
 *-----------------------------------------------------------------------*/
#if (defined(__STDC_VERSION__) && __STDC_VERSION__ >= 199901L) || defined(__GNUC__) || defined(__SCO__) || defined(__USLC__)

#include <stdint.h>
typedef int32_t khronos_int32_t;
typedef uint32_t khronos_uint32_t;
typedef int64_t khronos_int64_t;
typedef uint64_t khronos_uint64_t;
#define KHRONOS_SUPPORT_INT64 1
#define KHRONOS_SUPPORT_FLOAT 1

#elif defined(__VMS) || defined(__sgi)

#include <inttypes.h>
typedef int32_t khronos_int32_t;
typedef uint32_t khronos_uint32_t;
typedef int64_t khronos_int64_t;
typedef uint64_t khronos_uint64_t;
#define KHRONOS_SUPPORT_INT64 1
#define KHRONOS_SUPPORT_FLOAT 1

#elif defined(_WIN32) && !defined(__SCITECH_SNAP__)

typedef __int32 khronos_int32_t;
typedef unsigned __int32 khronos_uint32_t;
typedef __int64 khronos_int64_t;
typedef unsigned __int64 khronos_uint64_t;
#define KHRONOS_SUPPORT_INT64 1
#define KHRONOS_SUPPORT_FLOAT 1

#elif defined(__sun__) || defined(__digital__)

#include <inttypes.h>
typedef int32_t khronos_int32_t;
typedef uint32_t khronos_uint32_t;
#if defined(__arch64__) || defined(_LP64)
typedef int64_t khronos_int64_t;
typedef uint64_t khronos_uint64_t;
#define KHRONOS_SUPPORT_INT64 1
#else
#define KHRONOS_SUPPORT_INT64 0
#endif
#define KHRONOS_SUPPORT_FLOAT 1

#elif 0

typedef int khronos_int32_t;
typedef unsigned int khronos_uint32_t;
#define KHRONOS_SUPPORT_INT64 0
#define KHRONOS_SUPPORT_FLOAT 1

#else

#include <stdint.h>
typedef int32_t khronos_int32_t;
typedef uint32_t khronos_uint32_t;
typedef int64_t khronos_int64_t;
typedef uint64_t khronos_uint64_t;
#define KHRONOS_SUPPORT_INT64 1
#define KHRONOS_SUPPORT_FLOAT 1

#endif

/*-------------------------------------------------------------------------
 * Deprecated Khronos types
 *-----------------------------------------------------------------------*/
#include <stddef.h>
typedef signed char khronos_int8_t;
typedef unsigned char khronos_uint8_t;
typedef signed short int khronos_int16_t;
typedef unsigned short int khronos_uint16_t;
typedef signed long int khronos_intptr_t;
typedef unsigned long int khronos_uintptr_t;
typedef signed long int khronos_ssize_t;
typedef unsigned long int khronos_usize_t;

#if KHRONOS_SUPPORT_FLOAT
typedef float khronos_float_t;
#endif

#if KHRONOS_SUPPORT_INT64
typedef khronos_int64_t khronos_utime_nanoseconds_t;
typedef khronos_uint64_t khronos_stime_nanoseconds_t;
#endif

#define KHRONOS_MAX_ENUM 0x7FFFFFFF

typedef enum {
    KHRONOS_FALSE = 0,
    KHRONOS_TRUE = 1,
    KHRONOS_BOOLEAN_ENUM_FORCE_SIZE = KHRONOS_MAX_ENUM
} khronos_boolean_enum_t;

#endif /* __khrplatform_h_ */
