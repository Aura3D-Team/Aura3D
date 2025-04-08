#include "AuraUtils.h"

#include <math.h>
#include <cstdio>

namespace aura3d {

AuraUtils::AuraUtils() {}

f32 AuraUtils::fast_sqrt(f32 x) {
    if (x <= 0.0f) return 0.0f;  // Handle edge case
    f32 approx = x * 0.5f;
    int i = *(int*)&x;  // Interpret bits as integer
    i = 0x5f3759df - (i >> 1);  // Magic number initial guess
    f32 y = *(f32*)&i;  // Convert back to f32

    // Two Newton-Raphson iterations
    y = y * (1.5f - approx * y * y);
    // y = y * (1.5f - approx * y * y);

    return x * y;  // Convert 1/sqrt(x) into sqrt(x)
}

unsigned int AuraUtils::fast_int_sqrt(unsigned int x) {
    return std::ceil(fast_sqrt(x));
}

}
