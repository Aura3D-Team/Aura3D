cmake_minimum_required(VERSION $ENV{CMAKE_VERSION})

project(Aura3D
    LANGUAGES CXX
    VERSION 1.0.0
)

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Add library paths
list(APPEND CMAKE_PREFIX_PATH "$ENV{LIBRARY_PATH}")

# Find required packages
find_package(Threads REQUIRED)
find_package(OpenGL REQUIRED)
find_package(Vulkan REQUIRED)
find_library(GLFW_LIB glfw3 PATHS "$ENV{LIBRARY_PATH}/lib" REQUIRED)

# Set paths for third-party libraries
set(IMGUI_PATH "$ENV{LIBRARY_PATH}/include/imgui")
set(GLM_PATH "$ENV{LIBRARY_PATH}/include/glm")
set(PLOG_PATH "$ENV{LIBRARY_PATH}/include/plog")

set(GLSLC_EXECUTABLE "$ENV{VULKAN_SDK}/bin/glslc")
set(SPIRV_OUTPUT_DIR ${CMAKE_BINARY_DIR}/shaders)

set(IMGUI_SOURCES
    ${IMGUI_PATH}/imgui.cpp
    ${IMGUI_PATH}/imgui_draw.cpp
    ${IMGUI_PATH}/imgui_tables.cpp
    ${IMGUI_PATH}/imgui_widgets.cpp
    ${IMGUI_PATH}/backends/imgui_impl_glfw.cpp
    ${IMGUI_PATH}/backends/imgui_impl_vulkan.cpp
)

# Collect source files
file(GLOB_RECURSE SOURCES
    ${CMAKE_SOURCE_DIR}/src/*.cpp
    ${CMAKE_SOURCE_DIR}/src/*.h
)

# Collect shader files
file(GLOB_RECURSE SHADERS
    ${CMAKE_SOURCE_DIR}/shaders/*.frag
    ${CMAKE_SOURCE_DIR}/shaders/*.vert
)

foreach(SHADER_FILE ${SHADERS})
    get_filename_component(SHADER_NAME ${SHADER_FILE} NAME_WE)
    get_filename_component(SHADER_EXT ${SHADER_FILE} EXT)

    string(SUBSTRING ${SHADER_EXT} 1 -1 SHADER_EXT_STRIPPED)

    # Determine the output SPIR-V file path
    set(SPIRV_FILE ${SPIRV_OUTPUT_DIR}/${SHADER_EXT_STRIPPED}/${SHADER_NAME}_${SHADER_EXT_STRIPPED}.spv)

    file(MAKE_DIRECTORY ${SPIRV_OUTPUT_DIR}/${SHADER_EXT_STRIPPED})

    # Add a custom command to compile the shader
    add_custom_command(
        OUTPUT ${SPIRV_FILE}
        COMMAND ${GLSLC_EXECUTABLE} ${SHADER_FILE} -o ${SPIRV_FILE}
        DEPENDS ${SHADER_FILE}
        COMMENT "Compiling ${SHADER_FILE} to ${SPIRV_FILE}"
    )

    # Add the output as part of the build target
    list(APPEND SPIRV_FILES ${SPIRV_FILE})
endforeach()

add_custom_target(CompileShaders ALL DEPENDS ${SPIRV_FILES})

list(APPEND SOURCES ${IMGUI_SOURCES})

# Define executable target
add_executable(${PROJECT_NAME}
    ${SOURCES}
    ${SHADERS}
)

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

# Include directories for the main executable
target_include_directories(${PROJECT_NAME} PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    "$ENV{LIBRARY_PATH}/include"
    ${IMGUI_PATH}
    ${IMGUI_PATH}/backends
    ${GLM_PATH}
    ${PLOG_PATH}
)

# Link libraries
target_link_libraries(${PROJECT_NAME} PUBLIC
    ${GLFW_LIB}
    Vulkan::Vulkan
    OpenGL::GL
    Threads::Threads
    dl
)

# Installation settings
include(GNUInstallDirs)
install(TARGETS ${PROJECT_NAME}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
