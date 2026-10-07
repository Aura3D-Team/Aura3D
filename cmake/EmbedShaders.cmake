# EmbedShaders.cmake — the built-in shaders, compiled into the engine at build time.
#
#   aura_embed_shaders(TARGET)
#       Generates the header each enabled backend includes, under
#       <build>/generated/include, from resources/shaders/{opengl,vulkan,metal}:
#         aura/Renderer/OpenGL/EmbeddedGlsl.h          desktop and GLES sources
#         aura/Renderer/Vulkan/VkAura/EmbeddedSpirv.h  SPIR-V via AURA_GLSL_COMPILER
#         aura/Renderer/Metal/MtlAura/EmbeddedMetalLib.h  MSL, plus a .metallib
#                                                      with AURA_METAL_PRECOMPILE
#       Editing or adding a shader regenerates its header on the next build;
#       nothing generated is committed.

option(AURA_METAL_PRECOMPILE "Embed a precompiled .metallib (needs Xcode's Metal toolchain)" OFF)

set(_AURA_EMBED_SCRIPT "${CMAKE_CURRENT_LIST_DIR}/EmbedShaderHeader.cmake")

# Registers one generated header; SOURCES are extra build inputs, INPUTS what the script embeds.
function(_aura_embed_header KIND HEADER)
    cmake_parse_arguments(PARSE_ARGV 2 ARG "" "METALLIB" "INPUTS;SOURCES")
    set(_metallib_arg "")
    if(ARG_METALLIB)
        set(_metallib_arg "-DMETALLIB=${ARG_METALLIB}")
    endif()
    add_custom_command(
        OUTPUT  "${HEADER}"
        COMMAND "${CMAKE_COMMAND}" -DKIND=${KIND} "-DOUTPUT=${HEADER}" "-DINPUTS=${ARG_INPUTS}" ${_metallib_arg}
                -P "${_AURA_EMBED_SCRIPT}"
        DEPENDS ${ARG_SOURCES} ${ARG_INPUTS} "${_AURA_EMBED_SCRIPT}"
        COMMENT "Embedding ${KIND} shaders"
        VERBATIM
    )
    # Owned by the utility target alone; listing an output in two targets can duplicate its rule.
    set_property(TARGET aura3d_embedded_shaders APPEND PROPERTY SOURCES "${HEADER}")
endfunction()

function(aura_embed_shaders TARGET)
    set(_shaders "${PROJECT_SOURCE_DIR}/resources/shaders")
    set(_include "${CMAKE_CURRENT_BINARY_DIR}/generated/include")
    set(_renderer "${_include}/aura/Renderer")

    # Something for test targets to order themselves after; they include the headers too.
    add_custom_target(aura3d_embedded_shaders)
    add_dependencies(${TARGET} aura3d_embedded_shaders)
    target_include_directories(${TARGET} PUBLIC $<BUILD_INTERFACE:${_include}>)

    if(AURA_ENABLE_OPENGL)
        file(GLOB _glsl CONFIGURE_DEPENDS "${_shaders}/opengl/*.vert" "${_shaders}/opengl/*.frag")
        _aura_embed_header(glsl "${_renderer}/OpenGL/EmbeddedGlsl.h" INPUTS ${_glsl})
    endif()

    if(AURA_ENABLE_VULKAN)
        if(NOT AURA_GLSL_COMPILER)
            message(FATAL_ERROR
                "The Vulkan backend compiles its shaders at build time and needs glslc or glslangValidator. "
                "Install the Vulkan SDK (or shaderc/glslang), set AURA_GLSL_COMPILER, or pass "
                "-DAURA_ENABLE_VULKAN=OFF.")
        endif()

        file(GLOB _vulkan CONFIGURE_DEPENDS "${_shaders}/vulkan/*.vert" "${_shaders}/vulkan/*.frag")
        set(_spirv "")
        foreach(_shader IN LISTS _vulkan)
            get_filename_component(_name "${_shader}" NAME)
            set(_module "${CMAKE_CURRENT_BINARY_DIR}/generated/spirv/${_name}.spv")
            if(AURA_GLSL_COMPILER MATCHES "glslang")
                set(_compile "${AURA_GLSL_COMPILER}" -V "${_shader}" -o "${_module}")
            else()
                set(_compile "${AURA_GLSL_COMPILER}" "${_shader}" -o "${_module}")
            endif()
            add_custom_command(
                OUTPUT  "${_module}"
                COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/generated/spirv"
                COMMAND ${_compile}
                DEPENDS "${_shader}"
                COMMENT "Compiling SPIR-V: ${_name}"
                VERBATIM
            )
            list(APPEND _spirv "${_module}")
        endforeach()
        _aura_embed_header(spirv "${_renderer}/Vulkan/VkAura/EmbeddedSpirv.h" INPUTS ${_spirv})
    endif()

    if(AURA_ENABLE_METAL)
        file(GLOB _msl CONFIGURE_DEPENDS "${_shaders}/metal/*.metal")
        set(_metallib "")
        if(AURA_METAL_PRECOMPILE)
            # The same language version MtlShaderLibraryManager compiles the source form with.
            if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
                set(_sdk iphoneos)
            else()
                set(_sdk macosx)
            endif()
            set(_air "")
            foreach(_shader IN LISTS _msl)
                get_filename_component(_name "${_shader}" NAME_WE)
                set(_object "${CMAKE_CURRENT_BINARY_DIR}/generated/metal/${_name}.air")
                add_custom_command(
                    OUTPUT  "${_object}"
                    COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/generated/metal"
                    COMMAND xcrun -sdk ${_sdk} metal -std=metal3.0 -O2 -c "${_shader}" -o "${_object}"
                    DEPENDS "${_shader}"
                    COMMENT "Compiling Metal: ${_name}"
                    VERBATIM
                )
                list(APPEND _air "${_object}")
            endforeach()
            set(_metallib "${CMAKE_CURRENT_BINARY_DIR}/generated/metal/aura.metallib")
            add_custom_command(
                OUTPUT  "${_metallib}"
                COMMAND xcrun -sdk ${_sdk} metallib ${_air} -o "${_metallib}"
                DEPENDS ${_air}
                COMMENT "Linking aura.metallib"
                VERBATIM
            )
        endif()
        _aura_embed_header(msl "${_renderer}/Metal/MtlAura/EmbeddedMetalLib.h"
                           INPUTS ${_msl} METALLIB "${_metallib}" SOURCES ${_metallib})
    endif()
endfunction()
