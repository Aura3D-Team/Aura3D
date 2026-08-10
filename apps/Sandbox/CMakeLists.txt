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

if(AURA_PROFILE_DRAW_RECORDING)
    target_compile_definitions(Sandbox PRIVATE AURA_PROFILE_DRAW_RECORDING)
endif()

if(EMSCRIPTEN)
    # index.html at the repo root expects the glue script at exactly
    # pkg/Aura3D.js (see build_wasm.sh, which stages index.html next to
    # this output directory after the build).
    set_target_properties(Sandbox PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/pkg"
        OUTPUT_NAME "${AURA_WASM_OUTPUT_NAME}"
    )

    target_link_options(Sandbox PRIVATE
        # wma already links the real SDL3 port (see cmake/Dependencies.cmake);
        # disable Emscripten's own automatic SDL activation so the two don't
        # collide over the same USE_SDL port.
        "-sUSE_SDL=0"
        "-sALLOW_MEMORY_GROWTH=1"
        # The render loop never returns (windowManager->process() drives
        # emscripten_set_main_loop), so there's no runtime-exit path to
        # unwind for.
        "-sEXIT_RUNTIME=0"
        "$<$<BOOL:${AURA_WASM_ASYNCIFY}>:-sASYNCIFY=1>"
        # Debug builds trade size/speed for a real exception message instead
        # of Emscripten's opaque "Aborted(undefined)".
        "$<$<CONFIG:Debug>:-sASSERTIONS=1>"
    )
endif()

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
