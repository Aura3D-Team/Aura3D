# Sandbox — headless-capable demo application for the Aura3D engine.

if(ANDROID)
    # The Android app builds its own SHARED entry point (android/app/.../cpp).
    add_library(Sandbox SHARED main.cpp)
elseif(CMAKE_SYSTEM_NAME STREQUAL "iOS")
    # iOS has no way to launch a bare executable: the only runnable artefact is a
    # signed .app bundle, so the demo is built as one. cmake/Platform.cmake sets
    # CMAKE_MACOSX_BUNDLE for the same reason; MACOSX_BUNDLE is repeated on the
    # target so this file reads correctly on its own.
    add_executable(Sandbox MACOSX_BUNDLE main.cpp)

    set_target_properties(Sandbox PROPERTIES
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.aura3d.sandbox"
        MACOSX_BUNDLE_BUNDLE_NAME "Aura3D Sandbox"
        MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
        # Only the Xcode generator reads XCODE_ATTRIBUTE_*; the Ninja-based "ios"
        # preset never codesigns at all, which is why CI can build the bundle
        # with no signing identity. These exist so an Xcode-generator build
        # behaves the same, and a real device install then supplies an identity
        # via -DCMAKE_XCODE_ATTRIBUTE_DEVELOPMENT_TEAM.
        XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED "NO"
        XCODE_ATTRIBUTE_CODE_SIGNING_REQUIRED "NO"
    )
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
    # BUNDLE alongside RUNTIME: on iOS the artefact is Sandbox.app, and a
    # bundle target is installed through the BUNDLE destination -- naming only
    # RUNTIME there installs nothing at all, silently. The two destinations
    # are mutually exclusive per platform, so listing both is correct
    # everywhere rather than a special case.
    install(TARGETS Sandbox
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        BUNDLE  DESTINATION ${CMAKE_INSTALL_BINDIR}
    )
endif()
