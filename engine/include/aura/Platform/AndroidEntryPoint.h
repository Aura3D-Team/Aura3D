#ifndef AURA_PLATFORM_ANDROID_ENTRY_POINT_H
#define AURA_PLATFORM_ANDROID_ENTRY_POINT_H

#pragma once

#include <SDL3/SDL_main.h>

/**
 * @file AndroidEntryPoint.h
 * @brief The one thing an Android app embedding Aura3D needs to know about
 * SDL3's JNI entry point.
 *
 * Android's SDL glue (org.libsdl.app.SDLActivity) finds your app's entry
 * point by looking up the exact, unmangled C symbol "SDL_main" via dlsym.
 * <SDL3/SDL_main.h>'s usual `#define main SDL_main` preprocessor trick only
 * gives that renamed function C linkage if its signature exactly matches
 * SDL_main.h's own forward declaration
 * (`extern "C" int SDL_main(int argc, char *argv[])`) -- a renamed
 * `int main()` with no parameters, for example, does NOT inherit that
 * linkage, and silently gets ordinary (name-mangled) C++ linkage instead.
 * The failure then shows up at *runtime* ("Couldn't find function SDL_main
 * in library ..."), not at build time.
 *
 * This header provides the correctly extern "C" SDL_main trampoline for you
 * -- implement AuraAppMain() in your game code instead of dealing with
 * SDL_main directly.
 *
 * Important: include this header directly in your Android shim .cpp -- the
 * one compiled straight into your app's SHARED "main" library target -- NOT
 * through Aura3D's static library. A static-library-based trampoline does
 * NOT work here: the linker only pulls an object file out of a static
 * archive to resolve an otherwise-undefined symbol reference, and nothing
 * in ordinary C++ code ever calls SDL_main -- Android's Java-side glue finds
 * it purely via dlsym at runtime, which the linker can't see as a reference.
 * This header has to be compiled straight into the final shared object,
 * exactly like your own main.cpp is.
 *
 * Usage, in your own Android shim .cpp:
 *
 * @code
 *   #define main AuraAppMain
 *   #include "path/to/your_own_main.cpp"
 *   #include "aura/Platform/AndroidEntryPoint.h"
 * @endcode
 *
 * (see Aura3D's own android/app/src/main/cpp/android_main.cpp for the
 * reference example, unity-including apps/Sandbox/main.cpp the same way) --
 * your actual game code keeps writing a normal, platform-agnostic
 * `int main()`, completely unchanged from your Linux/WASM builds.
 */

//! Implemented by your game code (via the `#define main AuraAppMain` trick
//! above, included *before* this header).
int AuraAppMain();

extern "C" int SDL_main(int argc, char* argv[])
{
    (void)argc;
    (void)argv;
    return AuraAppMain();
}

#endif // AURA_PLATFORM_ANDROID_ENTRY_POINT_H
