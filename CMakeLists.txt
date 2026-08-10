cmake_minimum_required(VERSION 4.3.3)

project(Aura3D
    VERSION 0.1.0
    DESCRIPTION "Aura3D Multi Backend Engine"
    LANGUAGES C CXX
)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(GNUInstallDirs)

# NOTE: option() is option(<var> "<help>" [initial-value]). Passing the value
# where the help string belongs silently defaults the option to OFF, which
# previously disabled every renderer backend.
option(AURA_BUILD_SANDBOX   "Build the Sandbox demo application"   ON)
option(AURA_INSTALL_SANDBOX "Install the Sandbox demo application" OFF)
option(AURA_BUILD_ORGLOGO   "Build the OrgLogo demo application"   ON)
option(AURA_BUILD_TESTS     "Build the automated test suite"       OFF)

option(AURA_ENABLE_VULKAN "Compile the Vulkan backend"            ON)
option(AURA_ENABLE_OPENGL "Compile the OpenGL backend"            ON)
option(AURA_ENABLE_CPU    "Compile the software (CPU) backend"    ON)

option(AURA_ENABLE_LTO      "Enable link-time optimization"       ON)
option(AURA_NATIVE_OPTIMIZE "Tune codegen for the host CPU"       ON)

# Reports the CPU time drawMeshes() spends recording, averaged over 240 frames.
# OFF by default: it is a profiling aid, not something a normal run should pay
# a steady_clock read per frame for
option(AURA_PROFILE_DRAW_RECORDING "Instrument and log drawMeshes() recording time" OFF)

# Per-phase frame timing (wait/acquire/record/submit/present) from the render
# loop itself. OFF by default -- it costs two clock reads per phase per frame,
# which is material at several thousand FPS. See FrameProfiler.h: with this
# off the scopes expand to nothing, so there is no counter and no branch left
# in the build.
option(AURA_PROFILE_FRAME "Instrument and log per-phase frame timing" OFF)

# WASM (Emscripten) output. Only meaningful for the Sandbox target when
# EMSCRIPTEN; see apps/Sandbox/CMakeLists.txt for how these are consumed.
option(AURA_WASM_ASYNCIFY "Enable Emscripten Asyncify (needed if the app ever blocks synchronously; costs size/speed)" OFF)
set(AURA_WASM_OUTPUT_NAME "Aura3D" CACHE STRING "Base filename (no extension) for the WASM Sandbox build's pkg/ output")

include(cmake/Platform.cmake)
include(cmake/Dependencies.cmake)
include(cmake/Assets.cmake)

################################################################################
# Engine library
################################################################################

file(GLOB_RECURSE AURA_ENGINE_PRIVATE_SOURCES
    CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/engine/src/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/engine/src/*.c
)

file(GLOB_RECURSE AURA_ENGINE_ALL_HEADERS
    CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/engine/include/*.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/engine/include/*.h
)

# A game only ever touches aura3d::IRenderer (obtained from Engine or
# RendererFactory) plus the backend-agnostic Core/Renderer/Utils API never
# a concrete VulkanRenderer/OpenGLRenderer/CPURenderer or the manager classes
# behind it (VkDeviceManager, GlTextureManager, ...). Those per-backend
# directories are therefore split into their own, non-installed file set below:
# still compiled and still visible to IDEs, just never copied by `cmake --install`.
set(AURA_ENGINE_PUBLIC_HEADERS ${AURA_ENGINE_ALL_HEADERS})
list(FILTER AURA_ENGINE_PUBLIC_HEADERS EXCLUDE REGEX "/engine/include/aura/Renderer/(Vulkan|OpenGL|Software)/")

set(AURA_ENGINE_BACKEND_HEADERS ${AURA_ENGINE_ALL_HEADERS})
list(FILTER AURA_ENGINE_BACKEND_HEADERS INCLUDE REGEX "/engine/include/aura/Renderer/(Vulkan|OpenGL|Software)/")

# Backends are selected at configure time: drop the sources of any backend that
# is switched off so their API headers are never required.
if(NOT AURA_ENABLE_VULKAN)
    list(FILTER AURA_ENGINE_PRIVATE_SOURCES  EXCLUDE REGEX "/engine/src/Renderer/Vulkan/")
    list(FILTER AURA_ENGINE_BACKEND_HEADERS  EXCLUDE REGEX "/engine/include/aura/Renderer/Vulkan/")
endif()

if(NOT AURA_ENABLE_OPENGL)
    list(FILTER AURA_ENGINE_PRIVATE_SOURCES  EXCLUDE REGEX "/engine/src/Renderer/OpenGL/")
    list(FILTER AURA_ENGINE_BACKEND_HEADERS  EXCLUDE REGEX "/engine/include/aura/Renderer/OpenGL/")
endif()

if(NOT AURA_ENABLE_CPU)
    list(FILTER AURA_ENGINE_PRIVATE_SOURCES  EXCLUDE REGEX "/engine/src/Renderer/Software/")
    list(FILTER AURA_ENGINE_BACKEND_HEADERS  EXCLUDE REGEX "/engine/include/aura/Renderer/Software/")
endif()

add_library(Aura3D STATIC)

add_library(Aura3D::Aura3D ALIAS Aura3D)

target_sources(Aura3D
    PRIVATE
    ${AURA_ENGINE_PRIVATE_SOURCES}
    PUBLIC
        FILE_SET public_headers
        TYPE HEADERS
        BASE_DIRS engine/include
        FILES ${AURA_ENGINE_PUBLIC_HEADERS}
    PRIVATE
        FILE_SET backend_headers
        TYPE HEADERS
        BASE_DIRS engine/include
        FILES ${AURA_ENGINE_BACKEND_HEADERS}
)

target_include_directories(Aura3D
    PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/engine/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    PRIVATE
    # Header-only third-party loaders (stb_image, tinyobjloader).
    ${CMAKE_CURRENT_SOURCE_DIR}/vendor/stb
    ${CMAKE_CURRENT_SOURCE_DIR}/vendor/tinyobj
)

target_link_libraries(Aura3D
    PUBLIC
    wma::wma
    ink::ink
    Threads::Threads
)

if(EMSCRIPTEN)
    # Emscripten compiles with exceptions disabled by default,
    # which turns that internal catch into a fatal abort instead of a
    # graceful fallback. Actually emit and support C++ exception unwinding
    target_compile_options(Aura3D PUBLIC "-fexceptions")
    target_link_options(Aura3D PUBLIC "-fexceptions")
endif()

################################################################################
# Backend wiring
#
# AURA_HAS_* drive RendererFactory's compile-time backend switch and must stay
# in sync with the source filtering above.
################################################################################

if(AURA_ENABLE_VULKAN)
    target_compile_definitions(Aura3D PUBLIC AURA_HAS_VULKAN)
    target_link_libraries(Aura3D PUBLIC Vulkan::Vulkan)
endif()

if(AURA_ENABLE_OPENGL)
    target_compile_definitions(Aura3D PUBLIC AURA_HAS_OPENGL)

    if(EMSCRIPTEN)
        # WebGL2 == OpenGL ES 3.0: GLAD is a thin stub over the browser context.
        target_include_directories(Aura3D PUBLIC
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/vendor/glad/wasm>)
        target_compile_definitions(Aura3D PUBLIC AURA_GLES)
        # Emscripten only exposes ES3/WebGL2 entry points (uniform blocks,
        # glBindBufferBase, ...) when explicitly told to target WebGL2 --
        # PUBLIC so it reaches the final link of whatever executable links
        # Aura3D, not just this static library's own (nonexistent) link step.
        target_link_options(Aura3D PUBLIC "-sMAX_WEBGL_VERSION=2" "-sMIN_WEBGL_VERSION=2")
    elseif(ANDROID)
        target_include_directories(Aura3D PUBLIC
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/vendor/glad/android>)
        target_compile_definitions(Aura3D PUBLIC AURA_GLES)
        target_link_libraries(Aura3D PUBLIC GLESv3 EGL)
    else()
        # vendor/ is the include root so that <glad/glad.h> resolves.
        target_include_directories(Aura3D PUBLIC
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/vendor>)
        target_sources(Aura3D PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/vendor/glad/glad.c)
        target_link_libraries(Aura3D PUBLIC OpenGL::GL)
    endif()
endif()

if(AURA_ENABLE_CPU)
    target_compile_definitions(Aura3D PUBLIC AURA_HAS_CPU)
endif()

# PUBLIC: FrameProfiler.h is a public header, so the macro must agree between
# the library's own translation units and any consumer that includes it
if(AURA_PROFILE_FRAME)
    target_compile_definitions(Aura3D PUBLIC AURA_PROFILE_FRAME)
endif()

################################################################################
# Optimization
################################################################################

if(AURA_ENABLE_LTO)
    include(CheckIPOSupported)
    check_ipo_supported(RESULT AURA_IPO_SUPPORTED OUTPUT AURA_IPO_ERROR)
    if(AURA_IPO_SUPPORTED)
        set_property(TARGET Aura3D PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
    else()
        message(STATUS "Aura3D: LTO unavailable (${AURA_IPO_ERROR})")
    endif()
endif()

if(AURA_NATIVE_OPTIMIZE AND NOT EMSCRIPTEN AND NOT ANDROID)
    include(CheckCXXCompilerFlag)
    check_cxx_compiler_flag("-march=native" AURA_HAS_MARCH_NATIVE)
    if(AURA_HAS_MARCH_NATIVE)
        target_compile_options(Aura3D PRIVATE -march=native)
    endif()
endif()

################################################################################
# Sandbox
################################################################################

if(AURA_BUILD_SANDBOX)
    add_subdirectory(apps/Sandbox)
endif()

if(AURA_BUILD_ORGLOGO)
    add_subdirectory(apps/OrgLogo)
endif()

################################################################################
# Tests
################################################################################

if(AURA_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

include(cmake/Install.cmake)
