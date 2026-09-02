cmake_minimum_required(VERSION 4.3.3)

project(Aura3D
    VERSION 0.2.0
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

# Default OFF rather than ON like the others: the Metal backend only exists on
# Apple platforms, so an ON default would make every Linux/Windows/Android/WASM
# configure fail on headers that cannot be found. cmake/Platform.cmake forces it
# ON for APPLE (and forces it back OFF elsewhere if someone passes -D).
option(AURA_ENABLE_METAL  "Compile the Metal backend (Apple only)" OFF)

# The built-in immediate-mode UI (aura3d::ui). Backend-agnostic -- it is built
# entirely on IRenderer::drawBatch2D and the glyph atlas, so it needs no
# per-backend code and no third-party dependency. ON by default because it costs
# one small translation unit; switch it off for a UI-free library.
option(AURA_ENABLE_UI     "Compile the built-in immediate-mode UI" ON)

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

# Benchmark/debug mode: CPU and GPU allocation tracking, per-frame sample
# capture and a JSON metrics report, for pipeline performance tests and for
# answering "did this change regress" in dev builds.
#
# OFF by default, and more emphatically so than the two profiling switches
# above: it replaces the global operator new/delete pair with a counting one,
# which is not something a shipping build should carry. Implies
# AURA_PROFILE_FRAME below -- the report's per-phase breakdown *is*
# FrameProfiler's output, so enabling one without the other would produce a
# report with an empty timing section.
option(AURA_ENABLE_DEBUG_MODE "Enable CPU/GPU allocation tracking and benchmark report generation" OFF)

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
# a concrete VulkanRenderer/OpenGLRenderer/MetalRenderer/CPURenderer or the
# manager classes behind it (VkDeviceManager, GlTextureManager, MtlLayerManager,
# ...). Those per-backend directories are therefore split into their own,
# non-installed file set below: still compiled and still visible to IDEs, just
# never copied by `cmake --install`.
#
# Metal's exclusion carries extra weight: its headers include metal-cpp out of
# vendor/, which is a PRIVATE include directory. Installing them would hand a
# consumer headers it has no include path for.
set(AURA_ENGINE_BACKEND_DIR_REGEX "/engine/include/aura/Renderer/(Vulkan|OpenGL|Metal|Software)/")

set(AURA_ENGINE_PUBLIC_HEADERS ${AURA_ENGINE_ALL_HEADERS})
list(FILTER AURA_ENGINE_PUBLIC_HEADERS EXCLUDE REGEX "${AURA_ENGINE_BACKEND_DIR_REGEX}")

set(AURA_ENGINE_BACKEND_HEADERS ${AURA_ENGINE_ALL_HEADERS})
list(FILTER AURA_ENGINE_BACKEND_HEADERS INCLUDE REGEX "${AURA_ENGINE_BACKEND_DIR_REGEX}")

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

if(NOT AURA_ENABLE_METAL)
    list(FILTER AURA_ENGINE_PRIVATE_SOURCES  EXCLUDE REGEX "/engine/src/Renderer/Metal/")
    list(FILTER AURA_ENGINE_BACKEND_HEADERS  EXCLUDE REGEX "/engine/include/aura/Renderer/Metal/")
endif()

if(NOT AURA_ENABLE_CPU)
    list(FILTER AURA_ENGINE_PRIVATE_SOURCES  EXCLUDE REGEX "/engine/src/Renderer/Software/")
    list(FILTER AURA_ENGINE_BACKEND_HEADERS  EXCLUDE REGEX "/engine/include/aura/Renderer/Software/")
endif()

# The UI is a consumer of the renderer rather than a backend, so its headers are
# public and drop out of AURA_ENGINE_PUBLIC_HEADERS rather than the backend set.
if(NOT AURA_ENABLE_UI)
    list(FILTER AURA_ENGINE_PRIVATE_SOURCES EXCLUDE REGEX "/engine/src/UI/")
    list(FILTER AURA_ENGINE_PUBLIC_HEADERS  EXCLUDE REGEX "/engine/include/aura/UI/")
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
    # Third-party loaders: stb_image and tinyobjloader are header-only,
    # stb_vorbis is compiled below.
    ${CMAKE_CURRENT_SOURCE_DIR}/vendor/stb
    ${CMAKE_CURRENT_SOURCE_DIR}/vendor/tinyobj
)

################################################################################
# Ogg Vorbis decoding
#
# stb_vorbis is the one vendored stb library upstream ships as a .c rather than
# a header, so it is compiled as its own translation unit the way vendor/glad's
# loader is, instead of being pulled into a #define ..._IMPLEMENTATION TU.
# AudioClipLoader.cpp includes it with STB_VORBIS_HEADER_ONLY for the
# declarations alone.
#
# The two defines are PRIVATE on the target rather than set in either file so
# that the compiled TU and the header-only include cannot disagree about which
# API exists:
#   NO_STDIO       - the engine reads the file itself (see readEntireFile), so
#                    only the in-memory entry point is ever called. Also keeps
#                    fopen out of the WASM and Android builds.
#   NO_PUSHDATA_API- the pull API is the one AudioClipLoader uses.
################################################################################

target_sources(Aura3D PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/vendor/stb/stb_vorbis.c)

target_compile_definitions(Aura3D PRIVATE
    STB_VORBIS_NO_STDIO
    STB_VORBIS_NO_PUSHDATA_API
)

# stb_vorbis is third-party C that does not build warning-clean under the
# engine's own flags; its warnings are not actionable here and would drown the
# ones that are.
set_source_files_properties(
    ${CMAKE_CURRENT_SOURCE_DIR}/vendor/stb/stb_vorbis.c
    PROPERTIES COMPILE_OPTIONS
        "$<$<NOT:$<C_COMPILER_ID:MSVC>>:-w>$<$<C_COMPILER_ID:MSVC>:/w>"
)

target_link_libraries(Aura3D
    PUBLIC
    wma::wma
    ink::ink
    Threads::Threads
    glm::glm
)

if(EMSCRIPTEN)
    # Emscripten compiles with exceptions disabled by default,
    # which turns that internal catch into a fatal abort instead of a
    # graceful fallback. Actually emit and support C++ exception unwinding
    target_compile_options(Aura3D PUBLIC "-fexceptions")
    target_link_options(Aura3D PUBLIC "-fexceptions")

    # ink carries -pthread (see its src/CMakeLists.txt), so JobSystem's
    # ink::ThreadPool is real threads here. It is built lazily on the first
    # dispatch() -- on the main thread, which in a browser cannot block while a
    # worker spins up: a Worker only starts once the main thread returns to the
    # event loop, and dispatch() blocks on its band futures immediately after.
    # Pre-spawning the pool at startup is what keeps that first dispatch() from
    # deadlocking. Sized from the same navigator.hardwareConcurrency that
    # std::thread::hardware_concurrency() reports, and JobSystem only ever asks
    # for workerCount() - 1, so the pool is never exhausted.
    target_link_options(Aura3D PUBLIC
        "-sPTHREAD_POOL_SIZE=navigator.hardwareConcurrency")
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

if(AURA_ENABLE_METAL)
    target_compile_definitions(Aura3D PUBLIC AURA_HAS_METAL)

    # PRIVATE, unlike the OpenGL/Vulkan include dirs above: nothing in the
    # installed public header set includes metal-cpp (the Metal backend headers
    # are in the non-installed backend_headers file set), so a consumer never
    # needs this on its own include path. vendor/metal-cpp is the include root so
    # that <Metal/Metal.hpp>, <Foundation/Foundation.hpp> and
    # <QuartzCore/QuartzCore.hpp> resolve the way Apple's own samples spell them.
    target_include_directories(Aura3D PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/vendor/metal-cpp)

    # metal-cpp is inline bindings over objc_msgSend, so the frameworks have to be
    # linked even though this repository contains no Objective-C at all:
    #   Metal      — MTLDevice/MTLBuffer/MTLTexture and the objc classes behind them
    #   QuartzCore — CAMetalLayer/CAMetalDrawable
    #   Foundation — NSString/NSError/NSAutoreleasePool
    # PUBLIC because Aura3D is a static library: its consumers' final link is
    # where these symbols actually have to resolve.
    target_link_libraries(Aura3D PUBLIC
        "-framework Metal"
        "-framework QuartzCore"
        "-framework Foundation")
endif()

if(AURA_ENABLE_CPU)
    target_compile_definitions(Aura3D PUBLIC AURA_HAS_CPU)
endif()

# PUBLIC so an application can guard its UI code on the same switch that decided
# whether the module is in the library at all.
if(AURA_ENABLE_UI)
    target_compile_definitions(Aura3D PUBLIC AURA_HAS_UI)
endif()

# PUBLIC for the same reason as AURA_PROFILE_FRAME below: DebugMode.h and
# AllocationTracker.h are public headers whose class layouts do not change with
# the switch, but Engine::debugMode() returns null without it, and the global
# operator new/delete hooks only exist with it. A consumer that disagreed with
# the library about the macro would read a null subsystem as a bug.
#
# The `set()` (a normal variable shadowing the cache entry) is what makes debug
# mode imply frame profiling: it is read by the if() immediately below, and
# leaves the user's own -DAURA_PROFILE_FRAME cache value untouched.
if(AURA_ENABLE_DEBUG_MODE)
    target_compile_definitions(Aura3D PUBLIC AURA_ENABLE_DEBUG_MODE)
    set(AURA_PROFILE_FRAME ON)
    message(STATUS "Aura3D: debug/benchmark mode ON (implies AURA_PROFILE_FRAME)")
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
