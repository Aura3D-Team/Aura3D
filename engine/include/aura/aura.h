#ifndef AURA_HPP
#define AURA_HPP

#include <ink/ink_base.hpp>
#include <ink/Inkogger.h>

#define AURA_VERSION_MAJOR  0
#define AURA_VERSION_MINOR  0
#define AURA_VERSION_PATCH  1
#define AURA_VERSION_STRING INK_STR(AURA_VERSION_MAJOR) "." INK_STR(AURA_VERSION_MINOR) "." INK_STR(AURA_VERSION_PATCH)

#ifndef APPLICATION_NAME
#define APPLICATION_NAME "Aura3D"
#endif

#define ENGINE_NAME "Aura3D Engine"

#endif // AURA_HPP
