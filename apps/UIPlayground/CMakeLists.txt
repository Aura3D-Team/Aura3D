# UIPlayground — aura3d::ui driving textured 3D objects and a procedurally
# animated 2D sprite in one live scene. Companion app to
# docs/12-immediate-mode-ui.md and docs/13-auraui-internals.md.

add_executable(UIPlayground main.cpp)

target_link_libraries(UIPlayground
    PRIVATE
    Aura3D::Aura3D
)

# Stage settings.json + resources/ next to the binary, and compile the Vulkan
# GLSL sources to SPIR-V when a shader compiler is available.
aura_copy_assets(UIPlayground)

aura_compile_shaders(UIPlayground
    ${PROJECT_SOURCE_DIR}/resources/shaders/vulkan
    $<TARGET_FILE_DIR:UIPlayground>/resources/shaders/vulkan
)
