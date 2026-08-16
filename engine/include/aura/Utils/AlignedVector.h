#ifndef AURA_ALIGNEDVECTOR_H
#define AURA_ALIGNEDVECTOR_H

#pragma once

#include <vector>

#include <ink/AlignedAllocator.h>

namespace aura3d {

/**
 * @brief A std::vector whose storage is over-aligned to @p Alignment bytes.
 *
 * For buffers a SIMD kernel loads from: the software rasteriser's framebuffer
 * and the 2D vertex/index batches the overlay and the UI build every frame. An
 * aligned load on unaligned storage is the difference between one instruction
 * and a fault (or, on x86, a silent slowdown), and std::vector's own allocator
 * only promises alignof(T).
 *
 * @tparam Alignment Defaults to 32 bytes -- one AVX register, and a multiple of
 *         the 16 SSE and NEON want, so one default serves every SIMD path the
 *         engine has.
 *
 * @note In namespace aura3d, unlike its previous incarnation at global scope:
 *       this is an installed public header, and `AlignedVector` is exactly the
 *       kind of name a consuming project is liable to have its own version of.
 */
template <typename T, std::size_t Alignment = 32>
using AlignedVector = std::vector<T, ink::AlignedAllocator<T, Alignment>>;

} // namespace aura3d

#endif // AURA_ALIGNEDVECTOR_H
