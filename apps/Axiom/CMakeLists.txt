# Axiom -- worked examples from *Mathematics for Machine Learning*, built
# directly on the public Aura3D API and glm. One executable, one source file.

if(ANDROID)
    # Mirrors Sandbox: the Android app builds its own SHARED entry point.
    add_library(Axiom SHARED main.cpp)
elseif(CMAKE_SYSTEM_NAME STREQUAL "iOS")
    add_executable(Axiom MACOSX_BUNDLE main.cpp)

    set_target_properties(Axiom PROPERTIES
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.aura3d.axiom"
        MACOSX_BUNDLE_BUNDLE_NAME "Aura3D Axiom"
        MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
        XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED "NO"
        XCODE_ATTRIBUTE_CODE_SIGNING_REQUIRED "NO"
    )
else()
    add_executable(Axiom main.cpp)
endif()

target_link_libraries(Axiom
    PRIVATE
    Aura3D::Aura3D
)

if(EMSCRIPTEN)
    set_target_properties(Axiom PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/pkg-axiom"
        OUTPUT_NAME "Axiom"
    )

    target_link_options(Axiom PRIVATE
        "-sUSE_SDL=0"
        "-sALLOW_MEMORY_GROWTH=1"
        "-sEXIT_RUNTIME=0"
        "$<$<CONFIG:Debug>:-sASSERTIONS=1>"
    )
endif()

# Stage settings.json + resources/ next to the binary, and compile the Vulkan
# GLSL sources to SPIR-V when a shader compiler is available.
aura_copy_assets(Axiom)

aura_compile_shaders(Axiom
    ${PROJECT_SOURCE_DIR}/resources/shaders/vulkan
    $<TARGET_FILE_DIR:Axiom>/resources/shaders/vulkan
)
