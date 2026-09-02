# Emscripten is the one target with nothing to install: its deliverable is the
# Sandbox's pkg/ output (see apps/Sandbox/CMakeLists.txt), not a library plus
# headers. Android does get the normal install rules -- the .a is meant to be
# linked into a host app's own build, but that app still needs the headers and
# the package config, and ink/wma both install to their own android prefix the
# same way.
if(NOT EMSCRIPTEN)

include(CMakePackageConfigHelpers)

set(AURA_INSTALL_CMAKEDIR ${CMAKE_INSTALL_LIBDIR}/cmake/Aura3D)

install(
    TARGETS Aura3D
    EXPORT Aura3DTargets
    FILE_SET public_headers
)

install(
    EXPORT Aura3DTargets
    NAMESPACE Aura3D::
    DESTINATION ${AURA_INSTALL_CMAKEDIR}
)

configure_package_config_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/Aura3DConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/Aura3DConfig.cmake
    INSTALL_DESTINATION ${AURA_INSTALL_CMAKEDIR}
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
    DESTINATION ${AURA_INSTALL_CMAKEDIR}
)

endif()