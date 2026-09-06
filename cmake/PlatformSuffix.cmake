#! Platform+ABI tag distinguishing installed libraries that share one prefix.
#!
#! Headers are identical across targets and install once to <prefix>/include;
#! the library name is what separates a wasm build from an arm64 one. Installed
#! next to Aura3DConfig.cmake so a consumer resolves its own tag with the same
#! code that produced the name.
function(aura3d_platform_suffix out)
    if(EMSCRIPTEN)
        if(CMAKE_SIZEOF_VOID_P EQUAL 8)
            set(tag "wasm64")
        else()
            set(tag "wasm32")
        endif()
    elseif(ANDROID)
        set(tag "android_${ANDROID_ABI}")
    elseif(CMAKE_SYSTEM_NAME STREQUAL "iOS")
        set(tag "ios_${CMAKE_SYSTEM_PROCESSOR}")
    elseif(APPLE)
        set(tag "macos_${CMAKE_SYSTEM_PROCESSOR}")
    elseif(WIN32)
        set(tag "windows_${CMAKE_SYSTEM_PROCESSOR}")
    else()
        set(tag "linux_${CMAKE_SYSTEM_PROCESSOR}")
    endif()

    #! Apple reports CMAKE_OSX_ARCHITECTURES rather than the processor when
    #! cross-building, and it is the ABI that actually shipped.
    if(APPLE AND CMAKE_OSX_ARCHITECTURES)
        string(REPLACE ";" "_" archs "${CMAKE_OSX_ARCHITECTURES}")
        string(REGEX REPLACE "_[^_]+$" "" tag "${tag}")
        set(tag "${tag}_${archs}")
    endif()

    #! Debug and Release differ in ABI (assertions, _GLIBCXX_DEBUG-style flags),
    #! so they cannot share a name in a flat lib directory.
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        set(tag "${tag}_debug")
    endif()

    string(TOLOWER "${tag}" tag)
    string(REGEX REPLACE "[^a-z0-9]+" "_" tag "${tag}")

    set(${out} "${tag}" PARENT_SCOPE)
endfunction()

#! Resolves to a tag that is actually installed under @dir, falling back from a
#! Debug tag to its Release twin: the two are ABI-compatible here, and an image
#! that ships Release builds only would otherwise break every Debug consumer.
#! Returns the caller's own tag unchanged when nothing matches, so the config
#! can report it.
function(aura3d_resolve_installed_suffix dir stem out)
    aura3d_platform_suffix(tag)

    if(EXISTS "${dir}/${stem}-${tag}.cmake")
        set(${out} "${tag}" PARENT_SCOPE)
        return()
    endif()

    string(REGEX REPLACE "_debug$" "" fallback "${tag}")
    if(NOT fallback STREQUAL tag AND EXISTS "${dir}/${stem}-${fallback}.cmake")
        message(STATUS "aura3d: no ${tag} build installed, using ${fallback}")
        set(${out} "${fallback}" PARENT_SCOPE)
        return()
    endif()

    set(${out} "${tag}" PARENT_SCOPE)
endfunction()
