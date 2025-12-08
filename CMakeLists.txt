cmake_minimum_required(VERSION 3.14)

project(Aura3D LANGUAGES CXX VERSION 1.0.0)

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -Wall -Wextra")
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -march=native -mtune=native -flto -funroll-loops -fno-plt") # for multithread flags -fopenmp -pthread
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static-libstdc++ -static-libgcc")

message(STATUS "C++ Compiler: ${CMAKE_CXX_COMPILER}")

# =================================================================
# Find required packages (No ENV vars or HINTS)
# =================================================================
find_package(Threads REQUIRED)
find_package(OpenGL REQUIRED)
find_package(Vulkan REQUIRED)
find_package(wma REQUIRED)
find_package(ink REQUIRED)

# Shader compiler
set(GLSLC_EXECUTABLE "$ENV{VULKAN_SDK}/bin/glslc")
set(SHADERS_OUTPUT_DIR ${CMAKE_BINARY_DIR}/shaders)

# Vulkan Shader compilation
file(GLOB_RECURSE VULKAN_SHADERS
    ${CMAKE_SOURCE_DIR}/shaders/vulkan/*.frag
    ${CMAKE_SOURCE_DIR}/shaders/vulkan/*.vert
)

file(GLOB_RECURSE OPENGL_SHADERS
    ${CMAKE_SOURCE_DIR}/shaders/opengl/*.frag
    ${CMAKE_SOURCE_DIR}/shaders/opengl/*.vert
)

add_custom_target(shaders
    SOURCES
    ${VULKAN_SHADERS}
    ${OPENGL_SHADERS}
)

foreach(SHADER_FILE ${VULKAN_SHADERS})
    get_filename_component(SHADER_NAME ${SHADER_FILE} NAME_WE)
    get_filename_component(SHADER_EXT ${SHADER_FILE} EXT)

    string(SUBSTRING ${SHADER_EXT} 1 -1 SHADER_EXT_STRIPPED)
    set(SPIRV_FILE ${SHADERS_OUTPUT_DIR}/${SHADER_EXT_STRIPPED}/${SHADER_NAME}_${SHADER_EXT_STRIPPED}.spv)

    file(MAKE_DIRECTORY ${SHADERS_OUTPUT_DIR}/${SHADER_EXT_STRIPPED})

    add_custom_command(
        OUTPUT ${SPIRV_FILE}
        COMMAND ${GLSLC_EXECUTABLE} ${SHADER_FILE} -o ${SPIRV_FILE}
        DEPENDS ${SHADER_FILE}
        COMMENT "Compiling ${SHADER_FILE} to ${SPIRV_FILE}"
    )

    list(APPEND SPIRV_FILES ${SPIRV_FILE})
endforeach()

foreach(SHADER_FILE ${OPENGL_SHADERS})
    get_filename_component(SHADER_NAME ${SHADER_FILE} NAME_WE)
    get_filename_component(SHADER_EXT ${SHADER_FILE} EXT)

    string(SUBSTRING ${SHADER_EXT} 1 -1 SHADER_EXT_STRIPPED)
    file(RELATIVE_PATH SHADER_REL_PATH ${CMAKE_SOURCE_DIR}/shaders/opengl ${SHADER_FILE})
    set(GLSL_DEST_FILE ${SHADERS_OUTPUT_DIR}/${SHADER_EXT_STRIPPED}/${SHADER_REL_PATH})

    get_filename_component(GLSL_DEST_DIR ${GLSL_DEST_FILE} DIRECTORY)

    add_custom_command(
        OUTPUT ${GLSL_DEST_FILE}
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${SHADER_FILE} ${GLSL_DEST_FILE}
        DEPENDS ${SHADER_FILE}
        COMMENT "Copiando ${SHADER_FILE} (para OpenGL)"
    )

    list(APPEND GLSL_COPIED_FILES ${GLSL_DEST_FILE})
endforeach()

add_custom_target(CompileShaders ALL DEPENDS ${SPIRV_FILES})
add_custom_target(CopyGLSL ALL DEPENDS ${GLSL_COPIED_FILES})

# Collect sources
file(GLOB_RECURSE SOURCES
    ${CMAKE_SOURCE_DIR}/src/*.cpp
    ${CMAKE_SOURCE_DIR}/src/*.h
    ${CMAKE_SOURCE_DIR}/src/*.hpp
)

file(GLOB_RECURSE MICROUI
    ${CMAKE_SOURCE_DIR}/portables/microui/*.c
    ${CMAKE_SOURCE_DIR}/portables/microui/*.h
)

set(APP_CONFIGS "${CMAKE_SOURCE_DIR}/config.json")
set(APP_CONFIGS_DEST "${CMAKE_BINARY_DIR}")

add_custom_target(copy_app_configs ALL
    COMMAND ${CMAKE_COMMAND} -E make_directory "${APP_CONFIGS_DEST}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${APP_CONFIGS}"
        "${APP_CONFIGS_DEST}/config.json"
)

# Add executable target
add_executable(${PROJECT_NAME} ${SOURCES} ${MICROUI})
add_dependencies(${PROJECT_NAME} copy_app_configs)
add_dependencies(${PROJECT_NAME} CompileShaders)
add_dependencies(${PROJECT_NAME} CopyGLSL)

target_include_directories(Aura3D PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_compile_definitions(Aura3D PRIVATE GLFW_INCLUDE_VULKAN)

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
    # Compiler flags
    target_compile_options(${PROJECT_NAME} PRIVATE -g -O0)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(${PROJECT_NAME} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
else()
    target_compile_options(${PROJECT_NAME} PRIVATE -O3 -DNDEBUG)
endif()

add_definitions(-DCMAKE_SOURCE_DIR=\"${CMAKE_SOURCE_DIR}\")

# =================================================================
# Link libraries using their IMPORTED TARGETS
# =================================================================
target_link_libraries(Aura3D PRIVATE
    # Your custom libraries
    wma::wma
    ink::ink

    # Windowing & Graphics
    Vulkan::Vulkan
    OpenGL::GL

    # System
    Threads::Threads
    dl
)

# Installation settings
install(TARGETS ${PROJECT_NAME}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
