# Assets.cmake — shader compilation and runtime asset staging for Aura3D.
#
# Provides:
#   aura_compile_shaders(TARGET INPUT_DIR OUTPUT_DIR)
#       Compiles every GLSL source in INPUT_DIR to SPIR-V in OUTPUT_DIR and
#       makes TARGET depend on the result.
#
#   aura_copy_assets(TARGET)
#       Stages resources/ and the app settings.json next to TARGET's binary.

find_program(AURA_GLSL_COMPILER
    NAMES glslc glslangValidator
    DOC "GLSL -> SPIR-V compiler used to build resources/shaders/vulkan"
)

# Compile all GLSL sources found in INPUT_DIR into SPIR-V (.spv) in OUTPUT_DIR.
#
# The engine always ships a compiled copy of the 3D shaders inside
# EmbeddedSpirv.h, so a missing compiler is a warning rather than an error:
# builds still succeed and fall back to the embedded modules.
function(aura_compile_shaders TARGET INPUT_DIR OUTPUT_DIR)
    file(GLOB _aura_shader_sources CONFIGURE_DEPENDS
        "${INPUT_DIR}/*.vert"
        "${INPUT_DIR}/*.frag"
        "${INPUT_DIR}/*.comp"
        "${INPUT_DIR}/*.geom"
    )

    if(NOT _aura_shader_sources)
        return()
    endif()

    if(NOT AURA_GLSL_COMPILER)
        message(WARNING
            "aura_compile_shaders: no glslc/glslangValidator found; "
            "skipping SPIR-V compilation for ${TARGET}. "
            "The Vulkan backend will use the modules embedded in EmbeddedSpirv.h.")
        return()
    endif()

    # Compile into a fixed build-tree directory rather than OUTPUT_DIR
    # directly: OUTPUT_DIR is typically derived from
    # $<TARGET_FILE_DIR:TARGET>, and add_custom_command's OUTPUT can't use a
    # generator expression that depends on TARGET itself — TARGET's link
    # step would depend (via ${TARGET}_shaders) on a command whose output
    # path depends on TARGET's own location, which CMake can't resolve
    # ("No target ..." at generate time). Stage into a plain path instead,
    # then copy next to the binary as a POST_BUILD step (generator
    # expressions on TARGET are fine there, same as aura_copy_assets below).
    set(_stage_dir "${CMAKE_CURRENT_BINARY_DIR}/spirv/${TARGET}")

    set(_aura_spv_outputs "")
    foreach(_shader IN LISTS _aura_shader_sources)
        get_filename_component(_shader_name "${_shader}" NAME)
        set(_spv "${_stage_dir}/${_shader_name}.spv")

        if(AURA_GLSL_COMPILER MATCHES "glslangValidator")
            set(_compile_cmd "${AURA_GLSL_COMPILER}" -V "${_shader}" -o "${_spv}")
        else()
            set(_compile_cmd "${AURA_GLSL_COMPILER}" "${_shader}" -o "${_spv}")
        endif()

        add_custom_command(
            OUTPUT  "${_spv}"
            COMMAND "${CMAKE_COMMAND}" -E make_directory "${_stage_dir}"
            COMMAND ${_compile_cmd}
            DEPENDS "${_shader}"
            COMMENT "Compiling SPIR-V: ${_shader_name}"
            VERBATIM
        )
        list(APPEND _aura_spv_outputs "${_spv}")
    endforeach()

    add_custom_target(${TARGET}_shaders DEPENDS ${_aura_spv_outputs})
    add_dependencies(${TARGET} ${TARGET}_shaders)

    add_custom_command(TARGET ${TARGET} POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_directory
                "${_stage_dir}"
                "${OUTPUT_DIR}"
        COMMENT "Staging compiled SPIR-V for ${TARGET}"
        VERBATIM
    )
endfunction()

# Stage runtime assets next to the target binary so the app can be run
# straight from the build tree.
function(aura_copy_assets TARGET)
    add_custom_command(TARGET ${TARGET} POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_directory
                "${PROJECT_SOURCE_DIR}/resources"
                "$<TARGET_FILE_DIR:${TARGET}>/resources"
        COMMENT "Staging resources/ for ${TARGET}"
        VERBATIM
    )

    set(_settings "${CMAKE_CURRENT_SOURCE_DIR}/settings.json")
    if(EMSCRIPTEN AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/settings.wasm.json")
        set(_settings "${CMAKE_CURRENT_SOURCE_DIR}/settings.wasm.json")
    endif()

    if(EXISTS "${_settings}")
        add_custom_command(TARGET ${TARGET} POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                    "${_settings}"
                    "$<TARGET_FILE_DIR:${TARGET}>/settings.json"
            COMMENT "Staging settings.json for ${TARGET}"
            VERBATIM
        )
    endif()
endfunction()
