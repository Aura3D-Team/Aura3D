# Every platform installs alike, Android and Emscripten included. The Sandbox's
# pkg/ output is the browser demo, not the library, and is governed separately
# by AURA_INSTALL_SANDBOX.
include(CMakePackageConfigHelpers)

set(AURA_INSTALL_CMAKEDIR ${CMAKE_INSTALL_LIBDIR}/cmake/Aura3D)

install(
    TARGETS Aura3D
    EXPORT Aura3DTargets
    FILE_SET public_headers
)

install(
    EXPORT Aura3DTargets
    FILE Aura3DTargets-${AURA_PLATFORM_SUFFIX}.cmake
    NAMESPACE Aura3D::
    DESTINATION ${AURA_INSTALL_CMAKEDIR}
)

configure_package_config_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/Aura3DConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/Aura3DConfig.cmake
    INSTALL_DESTINATION ${AURA_INSTALL_CMAKEDIR}
)

# ARCH_INDEPENDENT: one version file serves every platform here, and the default
# stamps the building machine's word size into it -- a wasm32 install would then
# be rejected by a 64-bit consumer. ABI matching is the targets suffix's job.
write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/Aura3DConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
    ARCH_INDEPENDENT
)

# This build's own find_dependency() calls, keyed by ABI tag alongside its
# targets file -- see cmake/Aura3D-deps.cmake.in for why they cannot sit in the
# shared config.
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/Aura3D-deps.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/Aura3D-deps-${AURA_PLATFORM_SUFFIX}.cmake
    @ONLY
)

install(
    FILES
        ${CMAKE_CURRENT_BINARY_DIR}/Aura3DConfig.cmake
        ${CMAKE_CURRENT_BINARY_DIR}/Aura3DConfigVersion.cmake
        ${CMAKE_CURRENT_BINARY_DIR}/Aura3D-deps-${AURA_PLATFORM_SUFFIX}.cmake
        ${CMAKE_CURRENT_SOURCE_DIR}/cmake/PlatformSuffix.cmake
    DESTINATION ${AURA_INSTALL_CMAKEDIR}
)