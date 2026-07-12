cmake_minimum_required(VERSION 4.3.3)

project(Aura3D
    VERSION 1.0.0
    DESCRIPTION "Aura3D Multi Backend Engine"
    LANGUAGES C CXX
)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(GNUInstallDirs)

option(AURA_BUILD_SANDBOX ON)
option(AURA_INSTALL_SANDBOX OFF)

option(AURA_ENABLE_VULKAN ON)
option(AURA_ENABLE_OPENGL ON)
option(AURA_ENABLE_CPU ON)

option(AURA_ENABLE_LTO ON)
option(AURA_NATIVE_OPTIMIZE ON)

include(cmake/Platform.cmake)
include(cmake/Dependencies.cmake)

add_subdirectory(engine)

if(AURA_BUILD_SANDBOX)
    add_subdirectory(apps/Sandbox)
endif()

include(cmake/Install.cmake)

file(GLOB_RECURSE AURA_ENGINE_PRIVATE_SOURCES
    CONFIGURE_DEPENDS
    src/*.cpp
    src/*.c
)

file(GLOB_RECURSE AURA_ENGINE_PUBLIC_HEADERS
    CONFIGURE_DEPENDS
    include/*.hpp
    include/*.h
)

add_library(Aura3D STATIC)

add_library(Aura3D::Aura3D ALIAS Aura3D)

target_sources(Aura3D
    PRIVATE
    ${AURA_ENGINE_PRIVATE_SOURCES}
    PUBLIC
        FILE_SET public_headers
        TYPE HEADERS
        BASE_DIRS include
        FILES ${AURA_ENGINE_PUBLIC_HEADERS}
)

target_include_directories(Aura3D
    PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

target_link_libraries(Aura3D
    PUBLIC
    wma::wma
    ink::ink
    Threads::Threads
)

if(AURA_ENABLE_OPENGL AND NOT EMSCRIPTEN AND NOT ANDROID)
    target_link_libraries(Aura3D PUBLIC OpenGL::GL)
endif()

if(AURA_ENABLE_VULKAN)
    target_link_libraries(Aura3D PUBLIC Vulkan::Vulkan)
endif()

if(ANDROID)
    add_library(Sandbox SHARED main.cpp)
else()
    add_executable(Sandbox main.cpp)
endif()

target_link_libraries(
    Sandbox
    PRIVATE
    Aura3D::Aura3D
)

include(${PROJECT_SOURCE_DIR}/cmake/Assets.cmake)
