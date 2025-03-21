#include "AuraUtils.h"

#include <math.h>
#include <memory>
#include <cstdio>
#include <vector>

#include "AuraException/AuraException.h"

namespace aura3d {

AuraUtils::AuraUtils() {}

float AuraUtils::fast_sqrt(float x) {
    if (x <= 0.0f) return 0.0f;  // Handle edge case
    float approx = x * 0.5f;
    int i = *(int*)&x;  // Interpret bits as integer
    i = 0x5f3759df - (i >> 1);  // Magic number initial guess
    float y = *(float*)&i;  // Convert back to float

    // Two Newton-Raphson iterations
    y = y * (1.5f - approx * y * y);
    // y = y * (1.5f - approx * y * y);

    return x * y;  // Convert 1/sqrt(x) into sqrt(x)
}

unsigned int AuraUtils::fast_int_sqrt(unsigned int x) {
    return std::ceil(fast_sqrt(x));
}

}
