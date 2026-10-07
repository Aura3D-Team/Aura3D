# EmbedShaderHeader.cmake — script mode (cmake -P) behind aura_embed_shaders().
#
#   -DKIND=glsl|spirv|msl  -DOUTPUT=<header>  "-DINPUTS=<file;...>"
#   [-DMETALLIB=<file>]    (msl only: a precompiled library to embed too)
#
# Symbols are the input file names with '.' -> '_': gl_batch.vert -> gl_batch_vert,
# vk_batch.vert.spv -> vk_batch_vert (+ _len).

cmake_minimum_required(VERSION 3.25)

# MSVC rejects a single string literal longer than this.
set(_max_literal 16000)

function(_aura_symbol path out)
    get_filename_component(name "${path}" NAME)
    string(REGEX REPLACE "\\.spv$" "" name "${name}")
    string(MAKE_C_IDENTIFIER "${name}" name)
    set(${out} "${name}" PARENT_SCOPE)
endfunction()

function(_aura_raw_string symbol content delimiter out)
    string(LENGTH "${content}" length)
    if(length GREATER _max_literal)
        message(FATAL_ERROR "${symbol}: ${length} bytes exceeds one string literal; split the source.")
    endif()
    string(FIND "${content}" ")${delimiter}\"" clash)
    if(NOT clash EQUAL -1)
        message(FATAL_ERROR "${symbol}: source contains the raw-string delimiter ')${delimiter}\"'.")
    endif()
    set(${out} "inline constexpr const char ${symbol}[] = R\"${delimiter}(${content})${delimiter}\";\n" PARENT_SCOPE)
endfunction()

function(_aura_byte_array symbol file out)
    file(READ "${file}" hex HEX)
    string(LENGTH "${hex}" digits)
    if(digits EQUAL 0)
        message(FATAL_ERROR "${symbol}: ${file} is empty.")
    endif()
    # Sixteen bytes per line keeps the generated header readable when debugging.
    string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," bytes "${hex}")
    string(REPEAT "0x..," 16 line)
    string(REGEX REPLACE "(${line})" "\\1\n    " bytes "${bytes}")
    string(CONCAT array "alignas(4) inline constexpr unsigned char ${symbol}[] = {\n    ${bytes}};\n"
                        "inline constexpr unsigned int ${symbol}_len = sizeof(${symbol});\n\n")
    set(${out} "${array}" PARENT_SCOPE)
endfunction()

set(header "// Generated from resources/shaders by cmake/EmbedShaderHeader.cmake. Do not edit.\n\n#pragma once\n\n")

if(KIND STREQUAL "glsl")
    set(desktop "")
    set(es "")
    foreach(input IN LISTS INPUTS)
        _aura_symbol("${input}" symbol)
        file(READ "${input}" source)
        string(REPLACE "\r\n" "\n" source "${source}")
        # One source per stage: WebGL2/GLES differs only in its preamble.
        if(NOT source MATCHES "^#version 330 core\n")
            message(FATAL_ERROR "${input}: must start with '#version 330 core' so the GLES variant can be derived.")
        endif()
        string(REGEX REPLACE "^#version 330 core\n" "#version 300 es\nprecision highp float;\n" es_source "${source}")
        _aura_raw_string(${symbol} "${source}" aura_glsl desktop_line)
        _aura_raw_string(${symbol} "${es_source}" aura_glsl es_line)
        string(APPEND desktop "${desktop_line}\n")
        string(APPEND es "${es_line}\n")
    endforeach()
    string(APPEND header "namespace aura3d::gl\n{\n\n#ifdef AURA_GLES\n\n${es}#else\n\n${desktop}#endif\n\n"
                         "} // namespace aura3d::gl\n")
elseif(KIND STREQUAL "spirv")
    string(APPEND header "namespace aura3d::vk\n{\n\n")
    foreach(input IN LISTS INPUTS)
        _aura_symbol("${input}" symbol)
        _aura_byte_array(${symbol} "${input}" array)
        string(APPEND header "${array}")
    endforeach()
    string(APPEND header "} // namespace aura3d::vk\n")
elseif(KIND STREQUAL "msl")
    # Every file goes into one library, compiled at runtime unless a metallib is embedded.
    set(source "")
    foreach(input IN LISTS INPUTS)
        file(READ "${input}" text)
        string(REPLACE "\r\n" "\n" text "${text}")
        string(APPEND source "${text}\n")
    endforeach()
    if(METALLIB)
        set(precompiled 1)
    else()
        set(precompiled 0)
    endif()
    _aura_raw_string(mtl_library_source "${source}" aura_msl source_line)
    string(APPEND header "#define AURA_METAL_HAS_EMBEDDED_METALLIB ${precompiled}\n\n"
                         "namespace aura3d::mtl\n{\n\n${source_line}\n")
    if(METALLIB)
        _aura_byte_array(mtl_library_data "${METALLIB}" array)
        string(APPEND header "${array}")
    endif()
    string(APPEND header "} // namespace aura3d::mtl\n")
else()
    message(FATAL_ERROR "EmbedShaderHeader.cmake: unknown KIND '${KIND}'.")
endif()

# Rewritten only on change, so an unrelated shader edit does not rebuild every includer.
set(staged "${OUTPUT}.tmp")
file(WRITE "${staged}" "${header}")
file(COPY_FILE "${staged}" "${OUTPUT}" ONLY_IF_DIFFERENT)
file(REMOVE "${staged}")
