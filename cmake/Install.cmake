if(NOT EMSCRIPTEN AND NOT ANDROID)

install(
    TARGETS Aura3D
    EXPORT Aura3DTargets
    FILE_SET public_headers
)

endif()