#include "AuraUtils.h"

#include <math.h>
#include <memory>
#include <cstdio>
#include <vector>

#include "AuraException/AuraException.h"

namespace aura3d {

AuraUtils::AuraUtils() {}

std::string AuraUtils::exec_command(const std::string& cmd)
{
    // Use unique_ptr with custom deleter for RAII on the pipe
    std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(cmd.c_str(), "r"), pclose);
    if (!pipe) {
        throw AuraException("Command execution failed");
    }

    std::string result;
    result.reserve(16384); // 16KB initial reservation

    std::vector<char> buffer(8192);

    // Read directly into our buffer
    size_t bytesRead;
    while ((bytesRead = fread(buffer.data(), 1, buffer.size(), pipe.get())) > 0) {
        // Append only the bytes actually read
        result.append(buffer.data(), bytesRead);

        if (bytesRead < buffer.size())
            break;
    }

    return result;
}

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
