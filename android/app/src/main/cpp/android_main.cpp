/*
 * android_main.cpp — SDL3 entry-point shim for Android
 *
 * SDL3's Android Activity (org.libsdl.app.SDLActivity) loads libmain.so and
 * calls the symbol SDL_main() via JNI.  Including <SDL3/SDL_main.h> makes the
 * preprocessor rename  int main(...)  →  int SDL_main(...) in this translation
 * unit, so we unity-include apps/Sandbox/main.cpp here to apply that rename
 * without touching the Sandbox source.
 *
 * This is the idiomatic SDL3 Android shim pattern.
 */

// SDL_main.h MUST be included before any definition of main().
#include <SDL3/SDL_main.h>

// Unity-include the Sandbox entry point.
// The SDL_main macro from above turns its `int main(...)` into `int SDL_main(...)`.
#include "apps/Sandbox/main.cpp"
