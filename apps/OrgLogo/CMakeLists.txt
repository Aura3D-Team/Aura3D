# OrgLogo — procedurally generated, animated organization logo for the Aura3D engine.

add_executable(OrgLogo
    main.cpp
    LogoAssets.cpp
    StarField.cpp
)

target_link_libraries(OrgLogo
    PRIVATE
    Aura3D::Aura3D
)

# Stage settings.json + resources/ next to the binary, and compile the Vulkan
# GLSL sources to SPIR-V when a shader compiler is available.
aura_copy_assets(OrgLogo)

aura_compile_shaders(OrgLogo
    ${PROJECT_SOURCE_DIR}/resources/shaders/vulkan
    $<TARGET_FILE_DIR:OrgLogo>/resources/shaders/vulkan
)
