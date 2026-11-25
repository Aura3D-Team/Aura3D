add_executable(RedixelFpsBench main.cpp)
target_link_libraries(RedixelFpsBench PRIVATE Aura3D::Aura3D)
aura_copy_assets(RedixelFpsBench)
aura_compile_shaders(RedixelFpsBench
    ${PROJECT_SOURCE_DIR}/resources/shaders/vulkan
    $<TARGET_FILE_DIR:RedixelFpsBench>/resources/shaders/vulkan
)
