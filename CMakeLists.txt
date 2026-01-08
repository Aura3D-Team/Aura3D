cmake_minimum_required(VERSION 3.14)

# Project Name and Version
project(Aura3D LANGUAGES CXX C VERSION 1.0.0)

# =================================================================
# 1. Global Settings & Dependencies
# =================================================================
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Optimization Flags
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -march=native -mtune=native -flto -funroll-loops -fno-plt")
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -Wall -Wextra")

# Find Required Packages
find_package(Threads REQUIRED)
find_package(OpenGL REQUIRED)
find_package(Vulkan REQUIRED)
find_package(wma REQUIRED) # Your custom Window Manager
find_package(ink REQUIRED) # Your custom Logger

# =================================================================
# 2. Define Paths
# =================================================================
set(ENGINE_DIR "${CMAKE_SOURCE_DIR}/engine")
set(VENDOR_DIR "${CMAKE_SOURCE_DIR}/vendor")
set(APPS_DIR   "${CMAKE_SOURCE_DIR}/apps")
set(RES_DIR    "${CMAKE_SOURCE_DIR}/resources")

# =================================================================
# 3. The Engine Library (Aura3D)
# =================================================================
message(STATUS "Configuring Engine: Aura3D")

# Collect Engine Sources (Private Implementation)
file(GLOB_RECURSE ENGINE_SOURCES
    "${ENGINE_DIR}/src/*.cpp"
    "${ENGINE_DIR}/src/*.c"
    "${ENGINE_DIR}/src/*.h"    # <--- ADDED THIS
    "${ENGINE_DIR}/src/*.hpp"  # <--- ADDED THIS
    "${ENGINE_DIR}/include/*.h"   # <--- ADDED THIS (Public Headers)
    "${ENGINE_DIR}/include/*.hpp" # <--- ADDED THIS
)

# Collect Vendor Sources (Only those that need compilation like .c files)
# Adjust these paths if your vendor folder structure differs slightly
file(GLOB_RECURSE VENDOR_SOURCES
    "${VENDOR_DIR}/microui/*.c"
    "${VENDOR_DIR}/microui/*.h"
)

# Define the Library Target
add_library(Aura3D STATIC 
    ${ENGINE_SOURCES} 
    ${VENDOR_SOURCES}
)

# Define Include Directories
target_include_directories(Aura3D
    PUBLIC  
        # The Game sees these headers:
        "${ENGINE_DIR}/include"
        "${VENDOR_DIR}/glm"           # Math is usually needed by the game
        "${VENDOR_DIR}/json/include"  # JSON is usually needed by the game
    PRIVATE 
        # Only the Engine sees these headers:
        "${ENGINE_DIR}/src"
        "${VENDOR_DIR}/glad/include"  # GLAD headers
        "${VENDOR_DIR}/microui"       # MicroUI headers
        "${VENDOR_DIR}"               # Generic vendor root
)

# Link Engine Dependencies
target_link_libraries(Aura3D PUBLIC
    wma::wma
    ink::ink
    Vulkan::Vulkan
    OpenGL::GL
    Threads::Threads
    ${CMAKE_DL_LIBS} # For dlopen/dlsym
)

# Defines for the Engine
target_compile_definitions(Aura3D PRIVATE 
    GLFW_INCLUDE_VULKAN
    CMAKE_SOURCE_DIR="${CMAKE_SOURCE_DIR}" # Pass root dir for asset loading
)

# =================================================================
# 4. The Game Executable (Sandbox)
# =================================================================
message(STATUS "Configuring App: Sandbox")

add_executable(Sandbox "${APPS_DIR}/Sandbox/main.cpp")

# Link the Game to the Engine
target_link_libraries(Sandbox PRIVATE Aura3D)

# Ensure the game can find "aura/aura.h" and friends
target_include_directories(Sandbox PRIVATE "${ENGINE_DIR}/include")

# =================================================================
# 5. Resource & Asset Management (Shaders, Configs)
# =================================================================

# --- 5a. Shader Compilation (Vulkan) ---
set(GLSLC_EXECUTABLE "$ENV{VULKAN_SDK}/bin/glslc")
set(SHADERS_OUTPUT_DIR "${CMAKE_BINARY_DIR}/resources/shaders")

file(GLOB_RECURSE VK_SHADERS "${RES_DIR}/shaders/vk/*.frag" "${RES_DIR}/shaders/vk/*.vert")

foreach(SHADER_FILE ${VK_SHADERS})
    get_filename_component(SHADER_NAME ${SHADER_FILE} NAME_WE)
    get_filename_component(SHADER_EXT ${SHADER_FILE} EXT)
    string(SUBSTRING ${SHADER_EXT} 1 -1 SHADER_EXT_STRIPPED) # Remove dot
    
    set(SPIRV_FILE "${SHADERS_OUTPUT_DIR}/vk/${SHADER_NAME}_${SHADER_EXT_STRIPPED}.spv")
    file(MAKE_DIRECTORY "${SHADERS_OUTPUT_DIR}/vk")

    add_custom_command(
        OUTPUT ${SPIRV_FILE}
        COMMAND ${GLSLC_EXECUTABLE} ${SHADER_FILE} -o ${SPIRV_FILE}
        DEPENDS ${SHADER_FILE}
        COMMENT "Compiling Vulkan Shader: ${SHADER_NAME} -> .spv"
    )
    list(APPEND SPIRV_FILES ${SPIRV_FILE})
endforeach()

# --- 5b. Shader Copying (OpenGL) ---
file(GLOB_RECURSE GL_SHADERS "${RES_DIR}/shaders/gl/*.frag" "${RES_DIR}/shaders/gl/*.vert")

foreach(SHADER_FILE ${GL_SHADERS})
    get_filename_component(FILE_NAME ${SHADER_FILE} NAME)
    set(DEST_FILE "${SHADERS_OUTPUT_DIR}/gl/${FILE_NAME}")
    
    file(MAKE_DIRECTORY "${SHADERS_OUTPUT_DIR}/gl")

    add_custom_command(
        OUTPUT ${DEST_FILE}
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${SHADER_FILE} ${DEST_FILE}
        DEPENDS ${SHADER_FILE}
        COMMENT "Copying GL Shader: ${FILE_NAME}"
    )
    list(APPEND GLSL_COPIED_FILES ${DEST_FILE})
endforeach()

# --- 5c. Config & Asset Copying ---
# Copy settings.json from Sandbox folder to Build folder
set(APP_CONFIG "${APPS_DIR}/Sandbox/settings.json")
set(CONFIG_DEST "${CMAKE_BINARY_DIR}/settings.json")

add_custom_command(
    OUTPUT ${CONFIG_DEST}
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${APP_CONFIG}" "${CONFIG_DEST}"
    DEPENDS "${APP_CONFIG}"
    COMMENT "Copying settings.json"
)

# Create Targets for Resources
add_custom_target(ProcessShaders ALL DEPENDS ${SPIRV_FILES} ${GLSL_COPIED_FILES})
add_custom_target(ProcessAssets ALL DEPENDS ${CONFIG_DEST})

# Ensure Sandbox waits for resources
add_dependencies(Sandbox ProcessShaders ProcessAssets)

# =================================================================
# 6. Debug / Validation Layers
# =================================================================
if (CMAKE_BUILD_TYPE STREQUAL "Debug")
    target_compile_definitions(Aura3D PRIVATE DEBUG_MODE)
    target_compile_options(Aura3D PRIVATE -g -O0 -Wall -Wextra)

    # Copy Validation Layer Settings if exists
    if(EXISTS "${CMAKE_SOURCE_DIR}/vk_layer_settings.txt")
        file(COPY "${CMAKE_SOURCE_DIR}/vk_layer_settings.txt" DESTINATION "${CMAKE_BINARY_DIR}")
    endif()
else()
    target_compile_definitions(Aura3D PRIVATE NDEBUG)
endif()

# =================================================================
# 7. Install Instructions (Optional)
# =================================================================
install(TARGETS Aura3D Sandbox
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
