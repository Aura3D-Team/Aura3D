/*
 * android_main.cpp — Android entry-point shim for the Sandbox demo.
 *
 * This is the reference pattern for any Android app embedding Aura3D as a
 * library: see aura/Platform/AndroidEntryPoint.h for why these two lines are
 * everything your own equivalent shim .cpp needs, and what SDL3/JNI gotcha
 * they save you from having to know about. Your own game code still just
 * writes a normal, platform-agnostic `int main()`, unchanged from your
 * Linux/WASM builds -- only this small shim file is Android-specific.
 */
#define main AuraAppMain
#include "apps/Sandbox/main.cpp"
#include "aura/Platform/AndroidEntryPoint.h"
