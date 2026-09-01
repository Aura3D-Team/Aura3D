if(DEFINED ENV{LOCAL_PREFIX})
    set(_AURA_SEARCH_PREFIX "$ENV{LOCAL_PREFIX}")
else()
    set(_AURA_SEARCH_PREFIX "/usr/local")
endif()

if(ANDROID)
    set(_AURA_PLATFORM "android")
elseif(EMSCRIPTEN)
    set(_AURA_PLATFORM "wasm")
elseif(APPLE)
    # Mirrors the windows/linux branches below, for this project's own
    # macos-debug/macos-release/ios presets. iOS gets its own prefix rather than
    # sharing macOS's: ink/wma installed there are cross-compiled for arm64-apple-ios
    # and are not linkable into a macOS build (or vice versa), so one prefix for
    # both would silently pick the wrong slice.
    if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
        set(_AURA_PLATFORM "ios")
    elseif(CMAKE_BUILD_TYPE STREQUAL "Debug")
        set(_AURA_PLATFORM "macos/debug")
    else()
        set(_AURA_PLATFORM "macos/release")
    endif()
elseif(WIN32)
    # Mirrors the linux/debug|release branch below, for this project's own
    # windows-debug/windows-release presets (single-config Ninja generator,
    # so CMAKE_BUILD_TYPE is known here). libwma has the matching
    # windows-debug/windows-release presets on its own side; libink has no
    # Windows presets of its own (see docs/10-platform-builds.md#windows).
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        set(_AURA_PLATFORM "windows/debug")
    else()
        set(_AURA_PLATFORM "windows/release")
    endif()
else()
    # libink/libwma's linux presets install to linux/debug or linux/release
    # (single-config Ninja generator, so CMAKE_BUILD_TYPE is known here).
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        set(_AURA_PLATFORM "linux/debug")
    else()
        set(_AURA_PLATFORM "linux/release")
    endif()
endif()

set(ink_DIR "${_AURA_SEARCH_PREFIX}/${_AURA_PLATFORM}/lib/cmake/ink")
set(wma_DIR "${_AURA_SEARCH_PREFIX}/${_AURA_PLATFORM}/lib/cmake/wma")

unset(_AURA_SEARCH_PREFIX)
unset(_AURA_PLATFORM)

find_package(Threads REQUIRED)
find_package(wma CONFIG REQUIRED)
find_package(ink CONFIG REQUIRED)

# Graphics Backends Dependencies Tracking
if(AURA_ENABLE_OPENGL AND NOT EMSCRIPTEN AND NOT ANDROID)
    find_package(OpenGL REQUIRED)
endif()

if(AURA_ENABLE_VULKAN)
    find_package(Vulkan REQUIRED)
endif()