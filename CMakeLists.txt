cmake_minimum_required(VERSION 4.3.3)

project(Aura3D
    VERSION 1.0.0
    DESCRIPTION "Aura3D — Configurable Multi-Backend Engine"
    LANGUAGES C CXX
)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Options
option(BUILD_SHARED_LIBS       "Build shared libraries"              OFF)
option(AURA_BUILD_SANDBOX      "Build Sandbox executable"            ON)
option(AURA_INSTALL_SANDBOX    "Install Sandbox executable"          OFF)

option(AURA_ENABLE_VULKAN      "Enable Vulkan backend"               ON)
option(AURA_ENABLE_OPENGL      "Enable OpenGL backend"               ON)
option(AURA_ENABLE_CPU         "Enable CPU renderer backend"         ON)

option(AURA_ENABLE_LTO         "Enable LTO in Release builds"        ON)
option(AURA_NATIVE_OPTIMIZE    "Use -march=native in Release builds" ON)

# Platform constraints
if(EMSCRIPTEN)
    message(STATUS "[Aura3D] Platform: WebAssembly / Emscripten ${EMSCRIPTEN_VERSION}")
    # Vulkan is not exposed in browsers.
    # The existing OpenGL backend compiles against GLES3 via the GLAD WASM stub
    # and Emscripten translates the calls to WebGL2 at runtime.
    set(AURA_ENABLE_VULKAN     OFF CACHE BOOL "" FORCE)
    set(AURA_ENABLE_OPENGL     ON  CACHE BOOL "" FORCE)
    set(AURA_ENABLE_CPU        OFF CACHE BOOL "" FORCE)
    # -march=native and LTO do not apply to WASM targets.
    set(AURA_NATIVE_OPTIMIZE   OFF CACHE BOOL "" FORCE)
    set(AURA_ENABLE_LTO        OFF CACHE BOOL "" FORCE)

    option(AURA_WASM_ASYNCIFY      "Enable Emscripten Asyncify (blocking main loop)" ON)
    option(AURA_WASM_PTHREAD       "Enable Emscripten pthreads support"              OFF)
    set(AURA_WASM_INITIAL_MEMORY   "67108864"   CACHE STRING "Initial WASM heap (bytes, must be a multiple of 65536)")
    set(AURA_WASM_MAX_MEMORY       "536870912"  CACHE STRING "Max WASM heap (bytes)")
    set(AURA_WASM_OUTPUT_NAME      "Aura3D"     CACHE STRING "WASM output file base name")
endif()

if(ANDROID)
    message(STATUS "[Aura3D] Platform: Android  ABI=${ANDROID_ABI}  API=${ANDROID_PLATFORM}")
    # Vulkan-only on Android: modern GPU API, best performance, NDK native support.
    set(AURA_ENABLE_VULKAN     ON  CACHE BOOL "" FORCE)
    set(AURA_ENABLE_OPENGL     OFF CACHE BOOL "" FORCE)
    set(AURA_ENABLE_CPU        OFF CACHE BOOL "" FORCE)
    set(AURA_BUILD_SANDBOX     OFF CACHE BOOL "" FORCE)
    set(AURA_NATIVE_OPTIMIZE   OFF CACHE BOOL "" FORCE)

    list(APPEND CMAKE_PREFIX_PATH
        "${AURA_ANDROID_DEPS}"
    )
endif()

if(NOT (AURA_ENABLE_VULKAN OR AURA_ENABLE_OPENGL OR AURA_ENABLE_CPU))
    message(FATAL_ERROR "[Aura3D] At least one renderer backend must be enabled.")
endif()

# Dependencies
find_package(Threads REQUIRED)
find_package(wma CONFIG REQUIRED)
find_package(ink CONFIG REQUIRED)

# OpenGL: system package only needed for desktop builds.
# On WASM the Emscripten toolchain provides WebGL2 via link flags.
# On Android we use Vulkan exclusively.
if(AURA_ENABLE_OPENGL AND NOT EMSCRIPTEN AND NOT ANDROID)
    find_package(OpenGL REQUIRED)
endif()

if(AURA_ENABLE_VULKAN)
    find_package(Vulkan REQUIRED)
endif()

# Engine sources
file(GLOB_RECURSE AURA_ENGINE_PRIVATE_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/engine/src/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/engine/src/*.c"
)

file(GLOB_RECURSE AURA_ENGINE_PUBLIC_HEADERS CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/engine/include/*.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/engine/include/*.h"
)

if(NOT AURA_ENGINE_PRIVATE_SOURCES)
    message(FATAL_ERROR "[Aura3D] engine/src is empty.")
endif()

# GLAD / OpenGL loader
# Desktop: compile vendor/glad/glad.c into the library.
# WASM:    use vendor/glad/wasm/ stub — Emscripten provides the real bindings.
# Android: Vulkan-only, no OpenGL loader needed.
if(AURA_ENABLE_OPENGL)
    if(EMSCRIPTEN)
        set(AURA_GLAD_SOURCE "")
        set(AURA_GLAD_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/vendor/glad/wasm")
    else()
        set(AURA_GLAD_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/vendor/glad/glad.c")
        set(AURA_GLAD_INCLUDE_DIR "")
    endif()
endif()

# Engine static library
add_library(Aura3D STATIC)
add_library(Aura3D::Aura3D ALIAS Aura3D)

set_target_properties(Aura3D PROPERTIES
    VERSION                      ${PROJECT_VERSION}
    SOVERSION                    ${PROJECT_VERSION_MAJOR}
    EXPORT_NAME                  Aura3D
    POSITION_INDEPENDENT_CODE    ON
    CXX_STANDARD                 23
    CXX_STANDARD_REQUIRED        ON
    CXX_EXTENSIONS               OFF
)

target_compile_features(Aura3D PUBLIC cxx_std_23)

target_sources(Aura3D
    PRIVATE
        ${AURA_ENGINE_PRIVATE_SOURCES}

    PUBLIC
        FILE_SET public_headers
        TYPE HEADERS
        BASE_DIRS
            ${CMAKE_CURRENT_SOURCE_DIR}/engine/include
        FILES
            ${AURA_ENGINE_PUBLIC_HEADERS}
)

if(AURA_GLAD_SOURCE)
    target_sources(Aura3D PRIVATE ${AURA_GLAD_SOURCE})
endif()

target_include_directories(Aura3D
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/engine/include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/vendor/glm>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>

    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/engine/src
        ${CMAKE_CURRENT_SOURCE_DIR}/vendor/microui
        # GLAD stub directory (WASM only; empty string is filtered out)
        $<$<BOOL:${AURA_GLAD_INCLUDE_DIR}>:${AURA_GLAD_INCLUDE_DIR}>
)

target_compile_definitions(Aura3D
    PUBLIC
        $<$<BOOL:${AURA_ENABLE_VULKAN}>:AURA_HAS_VULKAN>
        $<$<BOOL:${AURA_ENABLE_OPENGL}>:AURA_HAS_OPENGL>
        $<$<BOOL:${AURA_ENABLE_CPU}>:AURA_HAS_CPU>
        # AURA_GLES: selects #version 300 es shaders in EmbeddedShaders.h
        $<$<BOOL:${EMSCRIPTEN}>:AURA_GLES>

    PRIVATE
        $<$<BOOL:${AURA_ENABLE_VULKAN}>:GLFW_INCLUDE_VULKAN>
        $<$<CONFIG:Debug>:DEBUG_MODE>
        $<$<CONFIG:Release>:NDEBUG>
        AURA_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
)

target_compile_options(Aura3D
    PRIVATE
        $<$<CONFIG:Debug>:-Wall -Wextra -g -O0>
        $<$<CONFIG:Release>:-O3 -fno-plt>
        $<$<AND:$<CONFIG:Release>,$<BOOL:${AURA_NATIVE_OPTIMIZE}>>:-march=native -mtune=native>
)

# Emscripten: make SDL3 port headers available at compile time.
if(EMSCRIPTEN AND AURA_ENABLE_OPENGL)
    target_compile_options(Aura3D PRIVATE "SHELL:-sUSE_SDL=3" "SHELL:-sUSE_WEBGL2=1")
endif()

if(AURA_ENABLE_LTO)
    include(CheckIPOSupported)
    check_ipo_supported(RESULT AURA_IPO_SUPPORTED OUTPUT AURA_IPO_ERROR)
    if(AURA_IPO_SUPPORTED)
        set_property(TARGET Aura3D PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
    else()
        message(WARNING "[Aura3D] IPO/LTO not supported: ${AURA_IPO_ERROR}")
    endif()
endif()

target_link_libraries(Aura3D
    PUBLIC
        wma::wma
        ink::ink
        Threads::Threads
    PRIVATE
        ${CMAKE_DL_LIBS}
)

# Desktop OpenGL only — WASM uses WebGL2 via Emscripten link flags below.
if(AURA_ENABLE_OPENGL AND NOT EMSCRIPTEN AND NOT ANDROID)
    target_link_libraries(Aura3D PUBLIC OpenGL::GL)
endif()

if(AURA_ENABLE_VULKAN)
    target_link_libraries(Aura3D PUBLIC Vulkan::Vulkan)
endif()

# Sandbox executable (desktop + WASM)
if(AURA_BUILD_SANDBOX)
    add_executable(Sandbox apps/Sandbox/main.cpp)

    target_link_libraries(Sandbox PRIVATE Aura3D::Aura3D)
    target_include_directories(Sandbox PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/engine/include
    )

    if(NOT EMSCRIPTEN)
        set_target_properties(Sandbox PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
        )
    endif()

    # ── Asset pipeline─
    set(PIPELINE_OUTPUTS "")

    macro(compile_or_copy_shaders INPUT_DIR OUTPUT_DIR EXTENSION COMPILER)
        file(GLOB_RECURSE RAW_FILES CONFIGURE_DEPENDS "${INPUT_DIR}/*${EXTENSION}")
        foreach(SRC_FILE ${RAW_FILES})
            get_filename_component(FILE_NAME ${SRC_FILE} NAME)
            if("${COMPILER}" STREQUAL "")
                set(OUT_FILE "${OUTPUT_DIR}/${FILE_NAME}")
                add_custom_command(
                    OUTPUT  ${OUT_FILE}
                    COMMAND ${CMAKE_COMMAND} -E make_directory ${OUTPUT_DIR}
                    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${SRC_FILE} ${OUT_FILE}
                    DEPENDS ${SRC_FILE}
                    VERBATIM
                )
            else()
                set(OUT_FILE "${OUTPUT_DIR}/${FILE_NAME}.spv")
                add_custom_command(
                    OUTPUT  ${OUT_FILE}
                    COMMAND ${CMAKE_COMMAND} -E make_directory ${OUTPUT_DIR}
                    COMMAND ${COMPILER} ${SRC_FILE} -o ${OUT_FILE}
                    DEPENDS ${SRC_FILE}
                    VERBATIM
                )
            endif()
            list(APPEND PIPELINE_OUTPUTS ${OUT_FILE})
        endforeach()
    endmacro()

    if(AURA_ENABLE_VULKAN)
        if(DEFINED ENV{VULKAN_SDK})
            set(SPV_COMPILER "$ENV{VULKAN_SDK}/bin/glslc")
        else()
            find_program(SPV_COMPILER glslc REQUIRED)
        endif()
        compile_or_copy_shaders(
            "${CMAKE_CURRENT_SOURCE_DIR}/resources/shaders/vulkan"
            "${CMAKE_BINARY_DIR}/resources/shaders/vk"
            ".vert" "${SPV_COMPILER}"
        )
        compile_or_copy_shaders(
            "${CMAKE_CURRENT_SOURCE_DIR}/resources/shaders/vulkan"
            "${CMAKE_BINARY_DIR}/resources/shaders/vk"
            ".frag" "${SPV_COMPILER}"
        )
    endif()

    if(AURA_ENABLE_OPENGL)
        compile_or_copy_shaders(
            "${CMAKE_CURRENT_SOURCE_DIR}/resources/shaders/opengl"
            "${CMAKE_BINARY_DIR}/resources/shaders/gl"
            ".vert" ""
        )
        compile_or_copy_shaders(
            "${CMAKE_CURRENT_SOURCE_DIR}/resources/shaders/opengl"
            "${CMAKE_BINARY_DIR}/resources/shaders/gl"
            ".frag" ""
        )
    endif()

    if(PIPELINE_OUTPUTS)
        add_custom_target(AssetPipeline ALL DEPENDS ${PIPELINE_OUTPUTS})
        add_dependencies(Sandbox AssetPipeline)
    endif()

    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/apps/Sandbox/settings.json")
        add_custom_command(TARGET Sandbox POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${CMAKE_CURRENT_SOURCE_DIR}/apps/Sandbox/settings.json"
                "${CMAKE_BINARY_DIR}/settings.json"
            VERBATIM
        )
    endif()

    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/vk_layer_settings.txt")
        add_custom_command(TARGET Sandbox POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${CMAKE_CURRENT_SOURCE_DIR}/vk_layer_settings.txt"
                "${CMAKE_BINARY_DIR}/vk_layer_settings.txt"
            VERBATIM
        )
    endif()

    # WebAssembly link flags
    if(EMSCRIPTEN)
        set_target_properties(Sandbox PROPERTIES
            OUTPUT_NAME                  "${AURA_WASM_OUTPUT_NAME}"
            SUFFIX                       ".js"
            RUNTIME_OUTPUT_DIRECTORY     "${CMAKE_BINARY_DIR}/pkg"
        )

        # SDL3 port headers needed at compile time too.
        target_compile_options(Sandbox PRIVATE "SHELL:-sUSE_SDL=3")

        set(_wasm_link_flags
            "-sUSE_SDL=3"
            "-sUSE_WEBGL2=1"
            "-sFULL_ES3=1"
            "-sGL_ENABLE_GET_PROC_ADDRESS=1"
            "-sALLOW_MEMORY_GROWTH=1"
            "-sINITIAL_MEMORY=${AURA_WASM_INITIAL_MEMORY}"
            "-sMAXIMUM_MEMORY=${AURA_WASM_MAX_MEMORY}"
            "-sSTACK_SIZE=5MB"
            "-sEXPORTED_RUNTIME_METHODS=[ccall,cwrap]"
            "-sENVIRONMENT=web"
            # Preload assets into the virtual Emscripten filesystem.
            "--preload-file" "${CMAKE_BINARY_DIR}/resources@/resources"
            "--preload-file" "${CMAKE_BINARY_DIR}/settings.json@/settings.json"
        )

        if(AURA_WASM_ASYNCIFY)
            # Asyncify lets the existing blocking main loop run in WASM
            # without restructuring the call graph.
            list(APPEND _wasm_link_flags "-sASYNCIFY=1")
        endif()

        if(AURA_WASM_PTHREAD)
            target_compile_options(Sandbox PRIVATE "-pthread")
            list(APPEND _wasm_link_flags "-sPTHREAD_POOL_SIZE=4" "-pthread")
        endif()

        target_link_options(Sandbox PRIVATE ${_wasm_link_flags})
    endif()
endif()

# Install
# (skipped for WASM / Android — those are not installed via CMake install())
if(NOT EMSCRIPTEN AND NOT ANDROID)
    if(AURA_INSTALL_SANDBOX AND AURA_BUILD_SANDBOX)
        install(
            TARGETS Aura3D Sandbox
            EXPORT  Aura3DTargets
            FILE_SET public_headers DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
            ARCHIVE  DESTINATION ${CMAKE_INSTALL_LIBDIR}
            LIBRARY  DESTINATION ${CMAKE_INSTALL_LIBDIR}
            RUNTIME  DESTINATION ${CMAKE_INSTALL_BINDIR}
        )
    else()
        install(
            TARGETS Aura3D
            EXPORT  Aura3DTargets
            FILE_SET public_headers DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
            ARCHIVE  DESTINATION ${CMAKE_INSTALL_LIBDIR}
            LIBRARY  DESTINATION ${CMAKE_INSTALL_LIBDIR}
            RUNTIME  DESTINATION ${CMAKE_INSTALL_BINDIR}
        )
    endif()

    install(
        EXPORT    Aura3DTargets
        FILE      Aura3DTargets.cmake
        NAMESPACE Aura3D::
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/Aura3D
    )

    configure_package_config_file(
        ${CMAKE_CURRENT_SOURCE_DIR}/cmake/Aura3DConfig.cmake.in
        ${CMAKE_CURRENT_BINARY_DIR}/Aura3DConfig.cmake
        INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/Aura3D
    )

    write_basic_package_version_file(
        ${CMAKE_CURRENT_BINARY_DIR}/Aura3DConfigVersion.cmake
        VERSION       ${PROJECT_VERSION}
        COMPATIBILITY SameMajorVersion
    )

    install(
        FILES
            ${CMAKE_CURRENT_BINARY_DIR}/Aura3DConfig.cmake
            ${CMAKE_CURRENT_BINARY_DIR}/Aura3DConfigVersion.cmake
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/Aura3D
    )
endif()
