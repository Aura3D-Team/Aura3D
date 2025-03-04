#ifndef AURA_HPP
#define AURA_HPP

#pragma once

#include <array>

#define APPLICATION_NAME "Aura3D"

#define MAX_FRAMES_IN_FLIGHT 2

template <typename T>
using VkFixedArray = std::array<T, MAX_FRAMES_IN_FLIGHT>;

#endif // AURA_HPP
