cmake_minimum_required(VERSION 4.2.3)

project(Aura3D
    VERSION 1.0.0
    DESCRIPTION "Aura3D — Configurable Multi-Backend Engine"
    LANGUAGES C CXX
)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# =================================================================
# Options
# =================================================================
option(BUILD_SHARED_LIBS       "Build shared libraries"              OFF)
option(AURA_BUILD_SANDBOX      "Build Sandbox executable"            ON)
option(AURA_INSTALL_SANDBOX    "Install Sandbox executable"          OFF)

option(AURA_ENABLE_VULKAN      "Enable Vulkan backend"               ON)
option(AURA_ENABLE_OPENGL      "Enable OpenGL backend"               ON)
option(AURA_ENABLE_CPU         "Enable CPU renderer backend"         ON)

option(AURA_ENABLE_LTO         "Enable LTO in Release builds"        ON)
option(AURA_NATIVE_OPTIMIZE    "Use -march=native in Release builds" ON)

if(NOT (AURA_ENABLE_VULKAN OR AURA_ENABLE_OPENGL OR AURA_ENABLE_CPU))
    message(FATAL_ERROR "At least one Aura3D renderer backend must be enabled")
endif()

# =================================================================
# Dependencies
# =================================================================
find_package(Threads REQUIRED)
find_package(wma CONFIG REQUIRED)
find_package(ink CONFIG REQUIRED)

if(AURA_ENABLE_OPENGL)
    find_package(OpenGL REQUIRED)
endif()

if(AURA_ENABLE_VULKAN)
    find_package(Vulkan REQUIRED)
endif()

# =================================================================
# Engine sources
# =================================================================
file(GLOB_RECURSE AURA_ENGINE_PRIVATE_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/engine/src/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/engine/src/*.c"
)

file(GLOB_RECURSE AURA_ENGINE_PUBLIC_HEADERS CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/engine/include/*.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/engine/include/*.h"
)

if(NOT AURA_ENGINE_PRIVATE_SOURCES)
    message(FATAL_ERROR "Aura3D source list is empty. Check engine/src.")
endif()

if(NOT AURA_ENGINE_PUBLIC_HEADERS)
    message(FATAL_ERROR "Aura3D public header list is empty. Check engine/include.")
endif()

# =================================================================
# GLAD source check
# =================================================================
if(AURA_ENABLE_OPENGL)
    set(AURA_GLAD_SOURCE
        "${CMAKE_CURRENT_SOURCE_DIR}/vendor/glad/glad.c"
    )

    set(AURA_GLAD_INCLUDE_DIR
        "${CMAKE_CURRENT_SOURCE_DIR}/vendor"
    )
endif()

# =================================================================
# Engine library
# =================================================================
add_library(Aura3D STATIC)
add_library(Aura3D::Aura3D ALIAS Aura3D)

target_compile_features(Aura3D PUBLIC cxx_std_17)

set_target_properties(Aura3D PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
    EXPORT_NAME Aura3D
    POSITION_INDEPENDENT_CODE ON
)

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

# Add GLAD directly into libAura3D.a
if(AURA_ENABLE_OPENGL)
    target_sources(Aura3D
        PRIVATE
            ${AURA_GLAD_SOURCE}
    )
endif()

target_include_directories(Aura3D
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/engine/include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/vendor/glm>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/vendor/json/include>
        $<$<BOOL:${AURA_ENABLE_OPENGL}>:$<BUILD_INTERFACE:${AURA_GLAD_INCLUDE_DIR}>>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>

    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/engine/src
        ${CMAKE_CURRENT_SOURCE_DIR}/vendor/microui
        ${CMAKE_CURRENT_SOURCE_DIR}/vendor
)

target_compile_definitions(Aura3D
    PUBLIC
        $<$<BOOL:${AURA_ENABLE_VULKAN}>:AURA_HAS_VULKAN>
        $<$<BOOL:${AURA_ENABLE_OPENGL}>:AURA_HAS_OPENGL>
        $<$<BOOL:${AURA_ENABLE_CPU}>:AURA_HAS_CPU>

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

if(AURA_ENABLE_LTO)
    include(CheckIPOSupported)
    check_ipo_supported(RESULT AURA_IPO_SUPPORTED OUTPUT AURA_IPO_ERROR)

    if(AURA_IPO_SUPPORTED)
        set_property(TARGET Aura3D PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
    else()
        message(WARNING "IPO/LTO not supported: ${AURA_IPO_ERROR}")
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

if(AURA_ENABLE_OPENGL)
    target_link_libraries(Aura3D
        PUBLIC
            OpenGL::GL
    )
endif()

if(AURA_ENABLE_VULKAN)
    target_link_libraries(Aura3D
        PUBLIC
            Vulkan::Vulkan
    )

    if(Vulkan_INCLUDE_DIRS)
        target_include_directories(Aura3D PRIVATE ${Vulkan_INCLUDE_DIRS})
    endif()
endif()

# =================================================================
# Sandbox executable
# =================================================================
if(AURA_BUILD_SANDBOX)
    add_executable(Sandbox
        apps/Sandbox/main.cpp
    )

    target_link_libraries(Sandbox
        PRIVATE
            Aura3D::Aura3D
    )

    target_include_directories(Sandbox
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/engine/include
    )

    set_target_properties(Sandbox PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    )

    # =============================================================
    # Asset pipeline
    # =============================================================
    set(PIPELINE_OUTPUTS "")

    macro(compile_or_copy_shaders INPUT_DIR OUTPUT_DIR EXTENSION COMPILER)
        file(GLOB_RECURSE RAW_FILES CONFIGURE_DEPENDS "${INPUT_DIR}/*${EXTENSION}")

        foreach(SRC_FILE ${RAW_FILES})
            get_filename_component(FILE_NAME ${SRC_FILE} NAME)

            if("${COMPILER}" STREQUAL "")
                set(OUT_FILE "${OUTPUT_DIR}/${FILE_NAME}")

                add_custom_command(
                    OUTPUT ${OUT_FILE}
                    COMMAND ${CMAKE_COMMAND} -E make_directory ${OUTPUT_DIR}
                    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${SRC_FILE} ${OUT_FILE}
                    DEPENDS ${SRC_FILE}
                    VERBATIM
                )
            else()
                set(OUT_FILE "${OUTPUT_DIR}/${FILE_NAME}.spv")

                add_custom_command(
                    OUTPUT ${OUT_FILE}
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
            ".vert"
            "${SPV_COMPILER}"
        )

        compile_or_copy_shaders(
            "${CMAKE_CURRENT_SOURCE_DIR}/resources/shaders/vulkan"
            "${CMAKE_BINARY_DIR}/resources/shaders/vk"
            ".frag"
            "${SPV_COMPILER}"
        )
    endif()

    if(AURA_ENABLE_OPENGL)
        compile_or_copy_shaders(
            "${CMAKE_CURRENT_SOURCE_DIR}/resources/shaders/opengl"
            "${CMAKE_BINARY_DIR}/resources/shaders/gl"
            ".vert"
            ""
        )

        compile_or_copy_shaders(
            "${CMAKE_CURRENT_SOURCE_DIR}/resources/shaders/opengl"
            "${CMAKE_BINARY_DIR}/resources/shaders/gl"
            ".frag"
            ""
        )
    endif()

    if(PIPELINE_OUTPUTS)
        add_custom_target(AssetPipeline ALL
            DEPENDS ${PIPELINE_OUTPUTS}
        )

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
endif()

# =================================================================
# Install Aura3D static package
# =================================================================
if(AURA_INSTALL_SANDBOX AND AURA_BUILD_SANDBOX)
    install(
        TARGETS Aura3D Sandbox
        EXPORT Aura3DTargets

        FILE_SET public_headers
            DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}

        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    )
else()
    install(
        TARGETS Aura3D
        EXPORT Aura3DTargets

        FILE_SET public_headers
            DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}

        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    )
endif()

install(
    EXPORT Aura3DTargets
    FILE Aura3DTargets.cmake
    NAMESPACE Aura3D::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/Aura3D
)

# =================================================================
# Package config
# =================================================================
configure_package_config_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/Aura3DConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/Aura3DConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/Aura3D
)

write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/Aura3DConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

install(
    FILES
        ${CMAKE_CURRENT_BINARY_DIR}/Aura3DConfig.cmake
        ${CMAKE_CURRENT_BINARY_DIR}/Aura3DConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/Aura3D
)
