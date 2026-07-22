# Aura3D automated test suite.
#
# Plain CTest executables with no external test framework (TestUtils.h),
# matching the engine's vendor-header-only dependency style. Each suite is
# a standalone binary that exits non-zero if any check failed.

set(AURA_TEST_ASSETS_DIR ${CMAKE_CURRENT_SOURCE_DIR}/assets)

set(AURA_TEST_SUITES
    test_image_loader
    test_mesh_loader
    test_shader_loader
)

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
