# ink/wma install every platform into one prefix, telling their configs apart
# by ABI tag rather than by directory, so this needs no platform mapping -- just
# point CMAKE_PREFIX_PATH at that prefix.
if(DEFINED ENV{LOCAL_PREFIX})
    list(APPEND CMAKE_PREFIX_PATH "$ENV{LOCAL_PREFIX}")
endif()

find_package(Threads REQUIRED)
find_package(wma CONFIG REQUIRED)
find_package(ink CONFIG REQUIRED)

# Graphics Backends Dependencies Tracking
#
# AURA_FIND_* is what the installed config repeats: an enabled backend is not
# always a package. Emscripten/Android reach GL through link options and NDK
# system libs, so find_dependency(OpenGL) there fails on a target that links.
if(AURA_ENABLE_OPENGL AND NOT EMSCRIPTEN AND NOT ANDROID)
    find_package(OpenGL REQUIRED)
    set(AURA_FIND_OPENGL ON)
else()
    set(AURA_FIND_OPENGL OFF)
endif()

if(AURA_ENABLE_VULKAN)
    find_package(Vulkan REQUIRED)
    set(AURA_FIND_VULKAN ON)
else()
    set(AURA_FIND_VULKAN OFF)
endif()

# glm reaches the public headers, so it must be a real link dependency, not
# something each TU happens to find through ink/wma's include path.
#
# Skipped for iOS: CMAKE_PREFIX_PATH is pinned to LOCAL_PREFIX above, but
# CMake's builtin system prefix list still searches Homebrew regardless, and
# Homebrew's glm ships a compiled libglm.dylib built for macOS -- linking it
# into an iOS binary fails at link time ("building for iOS, but linking in
# dylib built for macOS"). glm is header-only upstream, so the find_path
# fallback below is correct for iOS too.
if(NOT CMAKE_SYSTEM_NAME STREQUAL "iOS")
    find_package(glm CONFIG QUIET)
endif()

if(TARGET glm::glm)
    set(AURA_GLM_HAS_PACKAGE ON)
else()
    # NO_CMAKE_FIND_ROOT_PATH: glm is header-only, so the host prefix's copy is
    # the right one for a cross build too, and the NDK/Emscripten toolchains
    # otherwise confine the search to their sysroot.
    find_path(AURA_GLM_INCLUDE_DIR glm/glm.hpp NO_CMAKE_FIND_ROOT_PATH REQUIRED)

    add_library(glm::glm INTERFACE IMPORTED GLOBAL)
    set_target_properties(glm::glm PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES "${AURA_GLM_INCLUDE_DIR}")

    set(AURA_GLM_HAS_PACKAGE OFF)
endif()
