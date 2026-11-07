#ifndef CPU_SIMD_H
#define CPU_SIMD_H

#pragma once

//! Private to CpuRasterizer.cpp: the lane types are force-inlined into its kernels, so they cannot live in a
//! translation unit of their own. Internal linkage keeps a second includer from clashing with it.

#include <array>
#include <bit>
#include <cmath>
#include <limits>

#include <ink/ink_base.hpp>

// AURA_CPU_RASTER_SCALAR forces the one-lane path, which is how the vector paths are checked against it.
#if defined(AURA_CPU_RASTER_SCALAR)
#define AURA_RASTER_SCALAR 1
#elif defined(__AVX2__)
#define AURA_RASTER_AVX2 1
#include <immintrin.h>
#elif defined(__SSE2__) || defined(_M_X64) || defined(_M_AMD64)
#define AURA_RASTER_SSE2 1
#include <emmintrin.h>
#if defined(__SSE4_1__) || defined(__AVX__)
#include <smmintrin.h>
#endif
#elif (defined(__aarch64__) && defined(__ARM_NEON)) || defined(_M_ARM64)
#define AURA_RASTER_NEON 1
#include <arm_neon.h>
#else
#define AURA_RASTER_SCALAR 1
#endif

// The kernels are one function per shading variant; everything they call per row or per chunk is
// forced inline, or vector structs wider than 16 bytes go through the stack on every call.
#if defined(_MSC_VER) && !defined(__clang__)
#define AURA_RASTER_INLINE __forceinline
#define AURA_RASTER_NOINLINE __declspec(noinline)
#else
#define AURA_RASTER_INLINE [[gnu::always_inline]] inline
#define AURA_RASTER_NOINLINE [[gnu::noinline]]
#endif

namespace aura3d::cpu
{

namespace
{

/*
 * Lane types for the span kernels: F holds floats, I 32-bit words, M a lane mask. Comparisons
 * are false for NaN, and max0() returns 0 for it, so a NaN attribute never reaches a table index.
 */
#if AURA_RASTER_AVX2

constexpr i32 kLanes = 8;
struct F
{
    __m256 v;
};
struct I
{
    __m256i v;
};
struct M
{
    __m256 v;
};

F splat(f32 s) noexcept
{
    return {_mm256_set1_ps(s)};
}
I splat(u32 s) noexcept
{
    return {_mm256_set1_epi32(static_cast<i32>(s))};
}
F laneCenters() noexcept
{
    return {_mm256_setr_ps(0.5f, 1.5f, 2.5f, 3.5f, 4.5f, 5.5f, 6.5f, 7.5f)};
}
M maskOf(bool on) noexcept
{
    return {_mm256_castsi256_ps(_mm256_set1_epi32(on ? -1 : 0))};
}
F load(const f32 *p) noexcept
{
    return {_mm256_loadu_ps(p)};
}
I load(const u32 *p) noexcept
{
    return {_mm256_loadu_si256(reinterpret_cast<const __m256i *>(p))};
}
void store(f32 *p, F a) noexcept
{
    _mm256_storeu_ps(p, a.v);
}
void store(u32 *p, I a) noexcept
{
    _mm256_storeu_si256(reinterpret_cast<__m256i *>(p), a.v);
}
F operator+(F a, F b) noexcept
{
    return {_mm256_add_ps(a.v, b.v)};
}
F operator-(F a, F b) noexcept
{
    return {_mm256_sub_ps(a.v, b.v)};
}
F operator*(F a, F b) noexcept
{
    return {_mm256_mul_ps(a.v, b.v)};
}
F operator/(F a, F b) noexcept
{
    return {_mm256_div_ps(a.v, b.v)};
}
//! a * b + c
F madd(F a, F b, F c) noexcept
{
#if defined(__FMA__) || defined(_MSC_VER)
    return {_mm256_fmadd_ps(a.v, b.v, c.v)};
#else
    return {_mm256_add_ps(_mm256_mul_ps(a.v, b.v), c.v)};
#endif
}
F min(F a, F b) noexcept
{
    return {_mm256_min_ps(a.v, b.v)};
}
F max0(F a) noexcept
{
    return {_mm256_max_ps(a.v, _mm256_setzero_ps())};
}
F floor(F a) noexcept
{
    return {_mm256_floor_ps(a.v)};
}
I truncate(F a) noexcept
{
    return {_mm256_cvttps_epi32(a.v)};
}
F toFloat(I a) noexcept
{
    return {_mm256_cvtepi32_ps(a.v)};
}
M operator<(F a, F b) noexcept
{
    return {_mm256_cmp_ps(a.v, b.v, _CMP_LT_OQ)};
}
M operator<=(F a, F b) noexcept
{
    return {_mm256_cmp_ps(a.v, b.v, _CMP_LE_OQ)};
}
M operator>(F a, F b) noexcept
{
    return {_mm256_cmp_ps(a.v, b.v, _CMP_GT_OQ)};
}
M operator==(F a, F b) noexcept
{
    return {_mm256_cmp_ps(a.v, b.v, _CMP_EQ_OQ)};
}
M operator&(M a, M b) noexcept
{
    return {_mm256_and_ps(a.v, b.v)};
}
M operator|(M a, M b) noexcept
{
    return {_mm256_or_ps(a.v, b.v)};
}
u32 bits(M m) noexcept
{
    return static_cast<u32>(_mm256_movemask_ps(m.v));
}
F select(M m, F a, F b) noexcept
{
    return {_mm256_blendv_ps(b.v, a.v, m.v)};
}
I select(M m, I a, I b) noexcept
{
    return {_mm256_castps_si256(_mm256_blendv_ps(_mm256_castsi256_ps(b.v), _mm256_castsi256_ps(a.v), m.v))};
}
I operator&(I a, I b) noexcept
{
    return {_mm256_and_si256(a.v, b.v)};
}
I operator|(I a, I b) noexcept
{
    return {_mm256_or_si256(a.v, b.v)};
}
template <int N> I shiftRight(I a) noexcept
{
    return {_mm256_srli_epi32(a.v, N)};
}
template <int N> I shiftLeft(I a) noexcept
{
    return {_mm256_slli_epi32(a.v, N)};
}

#elif AURA_RASTER_SSE2

constexpr i32 kLanes = 4;
struct F
{
    __m128 v;
};
struct I
{
    __m128i v;
};
struct M
{
    __m128 v;
};

F splat(f32 s) noexcept
{
    return {_mm_set1_ps(s)};
}
I splat(u32 s) noexcept
{
    return {_mm_set1_epi32(static_cast<i32>(s))};
}
F laneCenters() noexcept
{
    return {_mm_setr_ps(0.5f, 1.5f, 2.5f, 3.5f)};
}
M maskOf(bool on) noexcept
{
    return {_mm_castsi128_ps(_mm_set1_epi32(on ? -1 : 0))};
}
F load(const f32 *p) noexcept
{
    return {_mm_loadu_ps(p)};
}
I load(const u32 *p) noexcept
{
    return {_mm_loadu_si128(reinterpret_cast<const __m128i *>(p))};
}
void store(f32 *p, F a) noexcept
{
    _mm_storeu_ps(p, a.v);
}
void store(u32 *p, I a) noexcept
{
    _mm_storeu_si128(reinterpret_cast<__m128i *>(p), a.v);
}
F operator+(F a, F b) noexcept
{
    return {_mm_add_ps(a.v, b.v)};
}
F operator-(F a, F b) noexcept
{
    return {_mm_sub_ps(a.v, b.v)};
}
F operator*(F a, F b) noexcept
{
    return {_mm_mul_ps(a.v, b.v)};
}
F operator/(F a, F b) noexcept
{
    return {_mm_div_ps(a.v, b.v)};
}
F madd(F a, F b, F c) noexcept
{
    return {_mm_add_ps(_mm_mul_ps(a.v, b.v), c.v)};
}
F min(F a, F b) noexcept
{
    return {_mm_min_ps(a.v, b.v)};
}
F max0(F a) noexcept
{
    return {_mm_max_ps(a.v, _mm_setzero_ps())};
}
M operator<(F a, F b) noexcept
{
    return {_mm_cmplt_ps(a.v, b.v)};
}
M operator<=(F a, F b) noexcept
{
    return {_mm_cmple_ps(a.v, b.v)};
}
M operator>(F a, F b) noexcept
{
    return {_mm_cmpgt_ps(a.v, b.v)};
}
M operator==(F a, F b) noexcept
{
    return {_mm_cmpeq_ps(a.v, b.v)};
}
M operator&(M a, M b) noexcept
{
    return {_mm_and_ps(a.v, b.v)};
}
M operator|(M a, M b) noexcept
{
    return {_mm_or_ps(a.v, b.v)};
}
u32 bits(M m) noexcept
{
    return static_cast<u32>(_mm_movemask_ps(m.v));
}
F select(M m, F a, F b) noexcept
{
#if defined(__SSE4_1__) || defined(__AVX__)
    return {_mm_blendv_ps(b.v, a.v, m.v)};
#else
    return {_mm_or_ps(_mm_and_ps(m.v, a.v), _mm_andnot_ps(m.v, b.v))};
#endif
}
I select(M m, I a, I b) noexcept
{
    const __m128i mask = _mm_castps_si128(m.v);
    return {_mm_or_si128(_mm_and_si128(mask, a.v), _mm_andnot_si128(mask, b.v))};
}
I truncate(F a) noexcept
{
    return {_mm_cvttps_epi32(a.v)};
}
F toFloat(I a) noexcept
{
    return {_mm_cvtepi32_ps(a.v)};
}
//! Only called on texel coordinates, which stay far inside the int range.
F floor(F a) noexcept
{
#if defined(__SSE4_1__) || defined(__AVX__)
    return {_mm_floor_ps(a.v)};
#else
    const __m128 truncated = _mm_cvtepi32_ps(_mm_cvttps_epi32(a.v));
    return {_mm_sub_ps(truncated, _mm_and_ps(_mm_cmpgt_ps(truncated, a.v), _mm_set1_ps(1.0f)))};
#endif
}
I operator&(I a, I b) noexcept
{
    return {_mm_and_si128(a.v, b.v)};
}
I operator|(I a, I b) noexcept
{
    return {_mm_or_si128(a.v, b.v)};
}
template <int N> I shiftRight(I a) noexcept
{
    return {_mm_srli_epi32(a.v, N)};
}
template <int N> I shiftLeft(I a) noexcept
{
    return {_mm_slli_epi32(a.v, N)};
}

#elif AURA_RASTER_NEON

constexpr i32 kLanes = 4;
struct F
{
    float32x4_t v;
};
struct I
{
    uint32x4_t v;
};
struct M
{
    uint32x4_t v;
};

F splat(f32 s) noexcept
{
    return {vdupq_n_f32(s)};
}
I splat(u32 s) noexcept
{
    return {vdupq_n_u32(s)};
}
F laneCenters() noexcept
{
    constexpr std::array<f32, 4> centers{0.5f, 1.5f, 2.5f, 3.5f};
    return {vld1q_f32(centers.data())};
}
M maskOf(bool on) noexcept
{
    return {vdupq_n_u32(on ? ~0u : 0u)};
}
F load(const f32 *p) noexcept
{
    return {vld1q_f32(p)};
}
I load(const u32 *p) noexcept
{
    return {vld1q_u32(p)};
}
void store(f32 *p, F a) noexcept
{
    vst1q_f32(p, a.v);
}
void store(u32 *p, I a) noexcept
{
    vst1q_u32(p, a.v);
}
F operator+(F a, F b) noexcept
{
    return {vaddq_f32(a.v, b.v)};
}
F operator-(F a, F b) noexcept
{
    return {vsubq_f32(a.v, b.v)};
}
F operator*(F a, F b) noexcept
{
    return {vmulq_f32(a.v, b.v)};
}
F operator/(F a, F b) noexcept
{
    return {vdivq_f32(a.v, b.v)};
}
F madd(F a, F b, F c) noexcept
{
    return {vfmaq_f32(c.v, a.v, b.v)};
}
F min(F a, F b) noexcept
{
    return {vminq_f32(a.v, b.v)};
}
//! vmaxnm, not vmax: the latter propagates NaN.
F max0(F a) noexcept
{
    return {vmaxnmq_f32(a.v, vdupq_n_f32(0.0f))};
}
F floor(F a) noexcept
{
    return {vrndmq_f32(a.v)};
}
I truncate(F a) noexcept
{
    return {vreinterpretq_u32_s32(vcvtq_s32_f32(a.v))};
}
F toFloat(I a) noexcept
{
    return {vcvtq_f32_s32(vreinterpretq_s32_u32(a.v))};
}
M operator<(F a, F b) noexcept
{
    return {vcltq_f32(a.v, b.v)};
}
M operator<=(F a, F b) noexcept
{
    return {vcleq_f32(a.v, b.v)};
}
M operator>(F a, F b) noexcept
{
    return {vcgtq_f32(a.v, b.v)};
}
M operator==(F a, F b) noexcept
{
    return {vceqq_f32(a.v, b.v)};
}
M operator&(M a, M b) noexcept
{
    return {vandq_u32(a.v, b.v)};
}
M operator|(M a, M b) noexcept
{
    return {vorrq_u32(a.v, b.v)};
}
u32 bits(M m) noexcept
{
    constexpr std::array<u32, 4> weights{1, 2, 4, 8};
    return vaddvq_u32(vandq_u32(m.v, vld1q_u32(weights.data())));
}
F select(M m, F a, F b) noexcept
{
    return {vbslq_f32(m.v, a.v, b.v)};
}
I select(M m, I a, I b) noexcept
{
    return {vbslq_u32(m.v, a.v, b.v)};
}
I operator&(I a, I b) noexcept
{
    return {vandq_u32(a.v, b.v)};
}
I operator|(I a, I b) noexcept
{
    return {vorrq_u32(a.v, b.v)};
}
template <int N> I shiftRight(I a) noexcept
{
    if constexpr (N == 0)
        return a;
    else
        return {vshrq_n_u32(a.v, N)};
}
template <int N> I shiftLeft(I a) noexcept
{
    return {vshlq_n_u32(a.v, N)};
}

#else // AURA_RASTER_SCALAR

constexpr i32 kLanes = 1;
struct F
{
    f32 v;
};
struct I
{
    u32 v;
};
struct M
{
    bool v;
};

F splat(f32 s) noexcept
{
    return {s};
}
I splat(u32 s) noexcept
{
    return {s};
}
F laneCenters() noexcept
{
    return {0.5f};
}
M maskOf(bool on) noexcept
{
    return {on};
}
F load(const f32 *p) noexcept
{
    return {*p};
}
I load(const u32 *p) noexcept
{
    return {*p};
}
void store(f32 *p, F a) noexcept
{
    *p = a.v;
}
void store(u32 *p, I a) noexcept
{
    *p = a.v;
}
F operator+(F a, F b) noexcept
{
    return {a.v + b.v};
}
F operator-(F a, F b) noexcept
{
    return {a.v - b.v};
}
F operator*(F a, F b) noexcept
{
    return {a.v * b.v};
}
F operator/(F a, F b) noexcept
{
    return {a.v / b.v};
}
F madd(F a, F b, F c) noexcept
{
    return {a.v * b.v + c.v};
}
F min(F a, F b) noexcept
{
    return {std::min(a.v, b.v)};
}
F max0(F a) noexcept
{
    return {a.v > 0.0f ? a.v : 0.0f};
}
F floor(F a) noexcept
{
    return {std::floor(a.v)};
}
//! Lanes reaching here are finite and in range: texel coordinates and clamped table indices.
I truncate(F a) noexcept
{
    return {static_cast<u32>(static_cast<i32>(a.v))};
}
F toFloat(I a) noexcept
{
    return {static_cast<f32>(static_cast<i32>(a.v))};
}
M operator<(F a, F b) noexcept
{
    return {a.v < b.v};
}
M operator<=(F a, F b) noexcept
{
    return {a.v <= b.v};
}
M operator>(F a, F b) noexcept
{
    return {a.v > b.v};
}
M operator==(F a, F b) noexcept
{
    return {a.v == b.v};
}
M operator&(M a, M b) noexcept
{
    return {a.v && b.v};
}
M operator|(M a, M b) noexcept
{
    return {a.v || b.v};
}
u32 bits(M m) noexcept
{
    return m.v ? 1u : 0u;
}
F select(M m, F a, F b) noexcept
{
    return m.v ? a : b;
}
I select(M m, I a, I b) noexcept
{
    return m.v ? a : b;
}
I operator&(I a, I b) noexcept
{
    return {a.v & b.v};
}
I operator|(I a, I b) noexcept
{
    return {a.v | b.v};
}
template <int N> I shiftRight(I a) noexcept
{
    return {a.v >> N};
}
template <int N> I shiftLeft(I a) noexcept
{
    return {a.v << N};
}

#endif

static_assert(std::has_single_bit(static_cast<u32>(kLanes)), "chunks align with a mask");
constexpr u32 kAllLanes = (1u << static_cast<u32>(kLanes)) - 1u;

//! Per-lane scratch for table lookups and texel fetches; scalar loads from L1 beat a gather here.
template <class T> using Lanes = std::array<T, kLanes>;

//! Runs @p body for each lane set in @p active: a chunk on a triangle's edge pays only for its pixels.
template <class Body> AURA_RASTER_INLINE void forEachLane(u32 active, Body &&body) noexcept
{
    if (active == kAllLanes)
        for (i32 lane = 0; lane < kLanes; ++lane)
            body(lane);
    else
        for (u32 rest = active; rest != 0; rest &= rest - 1)
            body(std::countr_zero(rest));
}

} // namespace

} // namespace aura3d::cpu

#endif // CPU_SIMD_H
