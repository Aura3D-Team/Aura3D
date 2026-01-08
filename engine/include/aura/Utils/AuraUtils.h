#ifndef AURAUTILS_H
#define AURAUTILS_H

#pragma once

#include "aura/aura.h"

namespace aura3d {

class AuraUtils
{
public:
    AuraUtils();

    static f32 fast_sqrt(f32 x);
    static unsigned int fast_int_sqrt(unsigned int x);
};

}

#endif // AURAUTILS_H
