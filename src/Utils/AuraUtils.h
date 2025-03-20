#ifndef AURAUTILS_H
#define AURAUTILS_H

#pragma once

#include <string>

namespace aura3d {

class AuraUtils
{
public:
    AuraUtils();

    static std::string exec_command(const std::string& cmd);

    static float fast_sqrt(float x);
    static unsigned int fast_int_sqrt(unsigned int x);
};

}

#endif // AURAUTILS_H
