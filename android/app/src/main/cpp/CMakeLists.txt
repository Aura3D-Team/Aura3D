cmake_minimum_required(VERSION 4.3.3)
project(AuraNative C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Locate Aura3D root
# Passed by Gradle: -DAURA3D_ROOT=<path>
if(NOT DEFINED AURA3D_ROOT)
    get_filename_component(AURA3D_ROOT
        "${CMAKE_CURRENT_SOURCE_DIR}/../../../../../.."
        ABSOLUTE
    )
    message(STATUS "[AuraNative] AURA3D_ROOT resolved to: ${AURA3D_ROOT}")
endif()

# Configure Aura3D: Vulkan-only for Android
set(AURA_ENABLE_VULKAN  ON  CACHE BOOL "" FORCE)
set(AURA_ENABLE_OPENGL  OFF CACHE BOOL "" FORCE)
set(AURA_ENABLE_CPU     OFF CACHE BOOL "" FORCE)
set(AURA_BUILD_SANDBOX  OFF CACHE BOOL "" FORCE)

add_subdirectory(${AURA3D_ROOT} ${CMAKE_CURRENT_BINARY_DIR}/aura3d_build)

# JNI entry-point shared library
# SDL3 on Android loads "libmain.so" and calls SDL_main() via JNI.
# android_main.cpp includes <SDL3/SDL_main.h> which renames main→SDL_main,
# then unity-includes apps/Sandbox/main.cpp so the rename applies.
add_library(main SHARED
    android_main.cpp
)

target_include_directories(main PRIVATE
    ${AURA3D_ROOT}/engine/include
    ${AURA3D_ROOT}
)

target_link_libraries(main
    PRIVATE
        Aura3D::Aura3D
        android          # Android NDK glue
        log              # __android_log_print
)
