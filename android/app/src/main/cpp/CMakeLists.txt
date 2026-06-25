cmake_minimum_required(VERSION 4.3.3)
project(AuraNative C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

project(AuraNative)

set(AURA3D_ROOT
    "${CMAKE_CURRENT_SOURCE_DIR}/../../../../../.."
)

add_subdirectory(
    ${AURA3D_ROOT}
    aura3d
)

add_library(main SHARED android_main.cpp)

target_link_libraries(
    main
    PRIVATE
        Aura3D::Aura3D
        android
        log
)