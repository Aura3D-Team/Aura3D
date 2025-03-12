#ifndef AURA_HPP
#define AURA_HPP

#pragma once

#include <array>

#define APPLICATION_NAME "Aura3D"

#define MAX_FRAMES_IN_FLIGHT 2
#define MAX_ATTRIBUTE_DESCRIPTION 3

template <typename T>
using VkFixedArray = std::array<T, MAX_FRAMES_IN_FLIGHT>;

template <typename T>
using AttributeDescriptionArray = std::array<T, MAX_ATTRIBUTE_DESCRIPTION>;

#endif // AURA_HPP
