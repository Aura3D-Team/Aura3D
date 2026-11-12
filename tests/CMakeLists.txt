# Aura3D automated test suite.
#
# Plain CTest executables with no external test framework (TestUtils.h),
# matching the engine's vendor-header-only dependency style. Each suite is
# a standalone binary that exits non-zero if any check failed.

set(AURA_TEST_ASSETS_DIR ${CMAKE_CURRENT_SOURCE_DIR}/assets)

# Unguarded, including the three debug-mode suites: AllocationTracker,
# BenchmarkStats and DebugMode are all compiled regardless of
# AURA_ENABLE_DEBUG_MODE (only the global allocation hooks and Engine's
# instance are gated), so these run in a default build too. The parts that
# genuinely need the hooks are skipped from inside test_allocation_tracker,
# which reports the skip rather than passing silently.
set(AURA_TEST_SUITES
    test_allocation_tracker
    test_asset_loader
    test_audio_clip_loader
    test_audio_engine
    test_audio_realtime
    test_benchmark_stats
    test_canvas
    test_cpu_raster
    test_culling
    test_debug_mode_report
    test_font_atlas
    test_frame_contract
    test_golden
    test_gltf
    test_image_loader
    test_mesh_loader
    test_renderer_factory
    test_runtime_safety
    test_settings
    test_shader_loader
    test_texture_sampling
)

# Guarded because AURA_ENABLE_UI=OFF leaves aura3d::ui out of the library
# entirely, and these are the only suites in the tree that link against it.
if(AURA_ENABLE_UI)
    list(APPEND AURA_TEST_SUITES
        test_aui_layout
        test_aui_clipboard
        test_aui_input
        test_aui_lifecycle
        test_aui_signal
        test_aui_text
        test_aui_paint
        test_aui_overlay
        test_aui_raster
        test_aui_window_decoration)
endif()

foreach(_aura_test_name IN LISTS AURA_TEST_SUITES)
    add_executable(${_aura_test_name} ${_aura_test_name}.cpp)

    target_link_libraries(${_aura_test_name} PRIVATE Aura3D::Aura3D)

    # An absolute, compile-time path straight into the source tree's
    # tests/assets/: avoids a configure-time file(COPY) of the golden assets
    # going stale relative to edits made without re-running cmake.
    target_compile_definitions(${_aura_test_name} PRIVATE
        AURA_TEST_ASSETS_DIR="${AURA_TEST_ASSETS_DIR}")

    add_test(NAME ${_aura_test_name} COMMAND ${_aura_test_name})
endforeach()

# Reads the window's own pixels back through Xlib, so only where X11 is there to link. It opens windows, so it only
# runs against a disposable X server named by AURA_TEST_DISPLAY (Xvfb) and reports skipped without one.
find_package(X11 QUIET)
if(AURA_ENABLE_UI AND X11_FOUND AND NOT CMAKE_CROSSCOMPILING AND NOT APPLE)
    add_executable(test_frame_conformance test_frame_conformance.cpp XlibWindow.cpp)
    target_link_libraries(test_frame_conformance PRIVATE Aura3D::Aura3D X11::X11)
    add_test(NAME test_frame_conformance COMMAND test_frame_conformance)
    set_tests_properties(test_frame_conformance PROPERTIES TIMEOUT 120 SKIP_RETURN_CODE 77)
endif()

# test_settings compares the committed schema and reference with what the engine generates.
target_compile_definitions(test_settings PRIVATE AURA_DOCS_DIR="${PROJECT_SOURCE_DIR}/docs")

if(NOT CMAKE_CROSSCOMPILING)
    # Rewrites docs/aura.schema.json and docs/settings-reference.md after a setting changes.
    add_executable(gen_settings_docs gen_settings_docs.cpp)
    target_link_libraries(gen_settings_docs PRIVATE Aura3D::Aura3D)
    add_custom_target(update-settings-docs
        COMMAND gen_settings_docs "${PROJECT_SOURCE_DIR}/docs"
        DEPENDS gen_settings_docs
        COMMENT "Regenerating the settings reference and schema"
        VERBATIM)
endif()

# Includes a generated header, so it must not compile before the engine's are written.
add_dependencies(test_shader_loader aura3d_embedded_shaders)

# Explicit benchmark executable; requires a display only when a backend is passed.
if(AURA_ENABLE_UI AND NOT CMAKE_CROSSCOMPILING)
    add_executable(benchmark_runtime benchmark_runtime.cpp)
    target_link_libraries(benchmark_runtime PRIVATE Aura3D::Aura3D)
    # The compute and pipeline checks' shaders: SPIR-V next to the binary when a compiler is found; GL reads the source.
    target_compile_definitions(benchmark_runtime PRIVATE
        AURA_TEST_SHADER_DIR="${CMAKE_CURRENT_SOURCE_DIR}/shaders"
        AURA_TEST_SPIRV_DIR="${CMAKE_CURRENT_BINARY_DIR}/shaders")
    if(AURA_GLSL_COMPILER)
        set(_test_spirv "")
        foreach(_name grid.comp pipeline.vert pipeline.frag depth.frag shade.frag fullscreen.vert invert.frag many.frag)
            set(_source "${CMAKE_CURRENT_SOURCE_DIR}/shaders/${_name}")
            set(_spirv "${CMAKE_CURRENT_BINARY_DIR}/shaders/${_name}.spv")
            if(AURA_GLSL_COMPILER MATCHES "glslang")
                set(_compile "${AURA_GLSL_COMPILER}" -V "${_source}" -o "${_spirv}")
            else()
                set(_compile "${AURA_GLSL_COMPILER}" "${_source}" -o "${_spirv}")
            endif()
            add_custom_command(
                OUTPUT "${_spirv}"
                COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/shaders"
                COMMAND ${_compile}
                DEPENDS "${_source}"
                COMMENT "Compiling SPIR-V: ${_name}"
                VERBATIM)
            list(APPEND _test_spirv "${_spirv}")
        endforeach()
        add_custom_target(benchmark_runtime_shaders DEPENDS ${_test_spirv})
        add_dependencies(benchmark_runtime benchmark_runtime_shaders)
    endif()
endif()
