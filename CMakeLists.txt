cmake_minimum_required(VERSION 3.14)

project(Aura3D LANGUAGES CXX VERSION 1.0.0)

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# set(CMAKE_CXX_FLAGS_DEBUG "-g")

message(STATUS "C++ Compiler: ${CMAKE_CXX_COMPILER}")

# Opção para escolher o gerenciador de janelas
option(SDL_WINDOW_MANAGER "Use SDL" ON)

if(SDL_WINDOW_MANAGER)
    add_definitions(-DSDL_WINDOW_MANAGER)
else()
    add_definitions(-DGLFW_WINDOW_MANAGER)
    message(STATUS "Using GLFW Window Manager.")
endif()

# Opções para escolher a API de renderização
option(USE_CPU "Use CPU rendering" OFF)
option(USE_VULKAN_API "Use Vulkan API" ON)

# Lógica para definir a API de renderização
if(USE_CPU)
    add_definitions(-DUSE_CPU)
    message(STATUS "Using CPU for rendering.")
elseif(USE_VULKAN_API)
    add_definitions(-DUSE_VULKAN_API)
    message(STATUS "Using Vulkan API for rendering.")
else()
    add_definitions(-DUSE_OPENGL_API)
    message(STATUS "Using OpenGL API for rendering.")
endif()

# Ensure Vulkan SDK is found
include_directories("$ENV{VULKAN_SDK}/include")

# All libs
set(ALL_LIBRARY_PATH "$ENV{LIBRARY_PATH}/lib")

# Find required packages
find_package(Threads REQUIRED)
find_library(VULKAN_LIB vulkan HINTS "$ENV{VULKAN_SDK}/lib")
find_library(GLFW_LIB glfw3 HINTS ${ALL_LIBRARY_PATH})
find_library(GLAD_LIB glad HINTS ${ALL_LIBRARY_PATH})
find_library(SDL2_LIB SDL2 HINTS ${ALL_LIBRARY_PATH})

# Define General library path
set(LIBRARY_INCLUDE "$ENV{LIBRARY_PATH}/include")

# Shader compiler
set(GLSLC_EXECUTABLE "$ENV{VULKAN_SDK}/bin/glslc")
set(SPIRV_OUTPUT_DIR ${CMAKE_BINARY_DIR}/shaders)

# Shader compilation
file(GLOB_RECURSE SHADERS
    ${CMAKE_SOURCE_DIR}/shaders/*.frag
    ${CMAKE_SOURCE_DIR}/shaders/*.vert
)

add_custom_target(shaders
    SOURCES
    ${SHADERS}
)

foreach(SHADER_FILE ${SHADERS})
    get_filename_component(SHADER_NAME ${SHADER_FILE} NAME_WE)
    get_filename_component(SHADER_EXT ${SHADER_FILE} EXT)

    string(SUBSTRING ${SHADER_EXT} 1 -1 SHADER_EXT_STRIPPED)
    set(SPIRV_FILE ${SPIRV_OUTPUT_DIR}/${SHADER_EXT_STRIPPED}/${SHADER_NAME}_${SHADER_EXT_STRIPPED}.spv)

    file(MAKE_DIRECTORY ${SPIRV_OUTPUT_DIR}/${SHADER_EXT_STRIPPED})

    add_custom_command(
        OUTPUT ${SPIRV_FILE}
        COMMAND ${GLSLC_EXECUTABLE} ${SHADER_FILE} -o ${SPIRV_FILE}
        DEPENDS ${SHADER_FILE}
        COMMENT "Compiling ${SHADER_FILE} to ${SPIRV_FILE}"
    )

    list(APPEND SPIRV_FILES ${SPIRV_FILE})
endforeach()

add_custom_target(CompileShaders ALL DEPENDS ${SPIRV_FILES})

# Collect sources
file(GLOB_RECURSE SOURCES
    ${CMAKE_SOURCE_DIR}/src/*.cpp
    ${CMAKE_SOURCE_DIR}/src/*.h
    ${CMAKE_SOURCE_DIR}/src/*.hpp
)

# Add executable target
add_executable(${PROJECT_NAME} ${SOURCES})
add_dependencies(${PROJECT_NAME} CompileShaders)

# Handle Debug mode specific settings
if (CMAKE_BUILD_TYPE STREQUAL "Debug")
    message("Configuring Debug build")

    set(VK_LAYER_SETTINGS_FILE "${CMAKE_SOURCE_DIR}/vk_layer_settings.txt")
    set(VK_LAYER_DESTINATION "${CMAKE_BINARY_DIR}")

    add_custom_target(copy_vk_layer_settings ALL
        COMMAND ${CMAKE_COMMAND} -E make_directory "${VK_LAYER_DESTINATION}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${VK_LAYER_SETTINGS_FILE}"
            "${VK_LAYER_DESTINATION}/vk_layer_settings.txt"
    )
    add_dependencies(${PROJECT_NAME} copy_vk_layer_settings)

    # Set debug compile definitions or flags if needed
    target_compile_definitions(${PROJECT_NAME} PRIVATE DEBUG_MODE)
endif()

add_definitions(-DCMAKE_SOURCE_DIR=\"${CMAKE_SOURCE_DIR}\")

# Include directories
target_include_directories(${PROJECT_NAME} PUBLIC
    ${CMAKE_SOURCE_DIR}/src
    ${LIBRARY_INCLUDE}
)

# Link libraries
target_link_libraries(${PROJECT_NAME} PUBLIC
    ${VULKAN_LIB}
    Threads::Threads
    ${GLFW_LIB}
    ${GLAD_LIB}
    ${SDL2_LIB}
    dl
)

# Installation settings
install(TARGETS ${PROJECT_NAME}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
