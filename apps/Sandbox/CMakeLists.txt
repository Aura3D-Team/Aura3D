# Sandbox — headless-capable demo application for the Aura3D engine.

if(ANDROID)
    # The Android app builds its own SHARED entry point (android/app/.../cpp).
    add_library(Sandbox SHARED main.cpp)
else()
    add_executable(Sandbox main.cpp)
endif()

target_link_libraries(Sandbox
    PRIVATE
    Aura3D::Aura3D
)

# Stage settings.json + resources/ next to the binary, and compile the Vulkan
# GLSL sources to SPIR-V when a shader compiler is available.
aura_copy_assets(Sandbox)

aura_compile_shaders(Sandbox
    ${PROJECT_SOURCE_DIR}/resources/shaders/vulkan
    $<TARGET_FILE_DIR:Sandbox>/resources/shaders/vulkan
)

if(AURA_INSTALL_SANDBOX)
    install(TARGETS Sandbox RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()
