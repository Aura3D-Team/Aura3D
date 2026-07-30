if(NOT EMSCRIPTEN AND NOT ANDROID)

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