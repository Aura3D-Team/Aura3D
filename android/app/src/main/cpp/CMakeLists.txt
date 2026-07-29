cmake_minimum_required(VERSION 4.3.3)
project(AuraNative C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# android/app/build.gradle already passes -DAURA3D_ROOT=... (computed from
# rootProject.projectDir, i.e. android/..) -- only fall back to a relative
# path here for a manual/non-Gradle configure of this file directly. Going up
# from src/main/cpp/ to the repo root (the parent of android/) is 5 levels
# (cpp -> main -> src -> app -> android -> repo root), not 6.
if(NOT DEFINED AURA3D_ROOT)
    set(AURA3D_ROOT
        "${CMAKE_CURRENT_SOURCE_DIR}/../../../../.."
    )
endif()

add_subdirectory(
    ${AURA3D_ROOT}
    aura3d
)

add_library(main SHARED android_main.cpp)

# android_main.cpp unity-includes "apps/Sandbox/main.cpp" relative to the
# repo root to reuse the Sandbox demo unmodified (see that file's comment).
target_include_directories(main PRIVATE ${AURA3D_ROOT})

target_link_libraries(
    main
    PRIVATE
        Aura3D::Aura3D
        android
        log
)