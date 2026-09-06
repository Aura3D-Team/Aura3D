# AuraUIDemo -- the retained widget toolkit, as one settings page. Built
# on the public AuraUI API alone. One executable, one source file.

if(ANDROID)
    # Mirrors Sandbox: the Android app builds its own SHARED entry point.
    add_library(AuraUIDemo SHARED main.cpp)
elseif(CMAKE_SYSTEM_NAME STREQUAL "iOS")
    add_executable(AuraUIDemo MACOSX_BUNDLE main.cpp)

    set_target_properties(AuraUIDemo PROPERTIES
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.aura3d.aurauidemo"
        MACOSX_BUNDLE_BUNDLE_NAME "AuraUI Demo"
        MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
        XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED "NO"
        XCODE_ATTRIBUTE_CODE_SIGNING_REQUIRED "NO"
    )
else()
    add_executable(AuraUIDemo main.cpp)
endif()

target_link_libraries(AuraUIDemo
    PRIVATE
    Aura3D::Aura3D
)

if(EMSCRIPTEN)
    set_target_properties(AuraUIDemo PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/pkg-aurauidemo"
        OUTPUT_NAME "AuraUIDemo"
    )

    target_link_options(AuraUIDemo PRIVATE
        "-sUSE_SDL=0"
        "-sALLOW_MEMORY_GROWTH=1"
        "-sEXIT_RUNTIME=0"
        "$<$<CONFIG:Debug>:-sASSERTIONS=1>"
    )
endif()

# Stage settings.json + resources/ next to the binary, and compile the Vulkan
# GLSL sources to SPIR-V when a shader compiler is available.
aura_copy_assets(AuraUIDemo)

aura_compile_shaders(AuraUIDemo
    ${PROJECT_SOURCE_DIR}/resources/shaders/vulkan
    $<TARGET_FILE_DIR:AuraUIDemo>/resources/shaders/vulkan
)
