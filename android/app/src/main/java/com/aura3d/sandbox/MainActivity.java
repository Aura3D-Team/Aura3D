package com.aura3d.sandbox;

import org.libsdl.app.SDLActivity;

/**
 * Aura3D statically links SDL3 into libmain.so (see android_main.cpp and the
 * single `main` SHARED target in android/app/src/main/cpp/CMakeLists.txt),
 * so there is no separate libSDL3.so to load. SDLActivity's default
 * getLibraries() tries to dlopen "SDL3" as its own library before "main",
 * which fails here -- override it to load only what's actually packaged.
 */
public class MainActivity extends SDLActivity {
    @Override
    protected String[] getLibraries() {
        return new String[] { "main" };
    }
}
