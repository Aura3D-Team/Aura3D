#ifndef VKCOMMON_H
#define VKCOMMON_H

#pragma once

#define APPLICATION_NAME "Aura3D"

#include <array>

#define MAX_FRAMES_IN_FLIGHT 2

template <typename T>
using VkFixedArray = std::array<T, MAX_FRAMES_IN_FLIGHT>;

#endif // VKCOMMON_H
