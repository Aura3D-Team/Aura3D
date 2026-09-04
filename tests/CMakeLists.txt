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
    test_audio_clip_loader
    test_audio_engine
    test_benchmark_stats
    test_cpu_raster
    test_debug_mode_report
    test_font_atlas
    test_image_loader
    test_mesh_loader
    test_renderer_factory
    test_settings
    test_shader_loader
    test_texture_sampling
)

# Guarded because AURA_ENABLE_UI=OFF leaves aura3d::ui out of the library
# entirely, and these are the only suites in the tree that link against it.
if(AURA_ENABLE_UI)
    list(APPEND AURA_TEST_SUITES
        test_aui_layout
        test_aui_input
        test_aui_text
        test_aui_paint
        test_aui_overlay)
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
