#ifndef ALIGNEDVECTOR_H
#define ALIGNEDVECTOR_H

#pragma once

#include <ink/ink.hpp>

// Make sure the namespace is correct
template<typename T, std::size_t Alignment = 32>  // 32-byte alignment for AVX
using AlignedVector = std::vector<T, ink::AlignedAllocator<T, Alignment>>;

#endif // ALIGNEDALLOCATOR_H
