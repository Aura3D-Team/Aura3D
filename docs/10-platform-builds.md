# 10. Platform Builds

Aura3D targets three platforms from one CMake project: Linux (native), Android
(NDK), and WebAssembly (Emscripten). Each has its own preset and its own
backend restrictions, applied automatically by `cmake/Platform.cmake` — you
don't set `AURA_ENABLE_*` by hand per platform.

| Platform | Backends compiled in | Why |
|---|---|---|
| Linux | Vulkan, OpenGL, CPU | Everything — the default dev target. |
| Android | Vulkan only | `AURA_ENABLE_OPENGL`/`AURA_ENABLE_CPU` are force-disabled; Vulkan is the modern mobile GPU path. |
| WASM | OpenGL only | Compiles to WebGL2; `AURA_ENABLE_VULKAN`/`AURA_ENABLE_CPU` are force-disabled (no browser Vulkan, and the CPU backend's `wma` framebuffer path isn't wired for Emscripten). |

This means `renderer.backend` in `settings.json` only really has a choice on
Linux — Android and WASM builds only ever have one backend available, and
`RendererFactory` will resolve to it regardless of what the JSON says (see
[03-engine-and-renderer.md](03-engine-and-renderer.md#backend-resolution--fallback)).

Aura3D depends on `libink` and `libwma`, built for the **same platform**
first — each preset's `CMAKE_PREFIX_PATH` points at where those two are
expected to already be installed.

## Linux

```bash
cmake --preset linux-release   # or linux-debug
cmake --build --preset linux-release
```

Output lands under `build/linux/release/`. No Emscripten or NDK needed —
this is the fast inner-loop target for actual gameplay iteration.

## Android

Prerequisites: `ANDROID_NDK_HOME` (NDK 27+), `ANDROID_HOME` (SDK with
build-tools and platform 29+), `libink`/`libwma` built for Android and
installed where the Android preset's `CMAKE_PREFIX_PATH` expects them.

```bash
./scripts/build_android.sh                      # arm64-v8a, libAura3D.a only
./scripts/build_android.sh --apk                # + full Sandbox APK via Gradle
./scripts/build_android.sh --abi x86_64 --debug  # other ABI / build type
```

Or the equivalent by hand:

```bash
cmake -S . -B build/android \
    -DCMAKE_TOOLCHAIN_FILE="$ANDROID_NDK_HOME/build/cmake/android.toolchain.cmake" \
    -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-29 -DANDROID_STL=c++_shared \
    -DCMAKE_BUILD_TYPE=Release
cmake --build build/android -j"$(nproc)"
```

`AURA_BUILD_SANDBOX` is force-disabled on Android (`cmake/Platform.cmake`) —
the Sandbox demo's desktop `main()` isn't the Android entry point. The APK
build instead compiles `android/app/src/main/cpp/android_main.cpp` as a
`SHARED` library loaded by the Java/Kotlin activity shell in `android/`. If
you're embedding Aura3D into your own app rather than using the bundled
Sandbox APK, link against the `libAura3D.a` the CMake-only invocation above
produces.

```bash
./scripts/build_android.sh --apk
```
finds APKs under `android/app/build/outputs/apk/`.

## WebAssembly

Prerequisites: Emscripten activated (`source $EMSDK/emsdk_env.sh`),
`libink`/`libwma` built with Emscripten and installed under the WASM
preset's `CMAKE_PREFIX_PATH`.

```bash
./scripts/build_wasm.sh                 # RelWithDebInfo
./scripts/build_wasm.sh --debug          # larger output, source maps
./scripts/build_wasm.sh --serve          # + local HTTP server on :8080
```

Or by hand:

```bash
emcmake cmake -S . -B build/wasm-release \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_PREFIX_PATH=/path/to/wasm/deps \
    -DCMAKE_FIND_ROOT_PATH_MODE_PACKAGE=BOTH
cmake --build build/wasm-release -j"$(nproc)"
```

Output is `build/wasm-<type>/pkg/Aura3D.js` + `Aura3D.wasm`, with the
top-level `index.html` copied alongside — open that (via the `--serve`
server, or any static file server; `file://` won't work for WASM fetches)
to run in a browser.

`AURA_WASM_ASYNCIFY` (default `OFF`) only matters if your app ever blocks
synchronously inside the frame callback (a blocking network call, a modal
dialog); the render loop itself (`windowManager->process()`) already hands
control back to the browser's `requestAnimationFrame` correctly without it.
Enabling it costs both build time and runtime size.

## CMake options that apply everywhere

See the [top-level README](../README.md#cmake-options) for the full table
(`AURA_ENABLE_VULKAN/OPENGL/CPU`, `AURA_BUILD_SANDBOX/ORGLOGO/TESTS`,
`AURA_ENABLE_LTO`, `AURA_NATIVE_OPTIMIZE`). `Platform.cmake` overrides the
backend and optimization flags per platform as described above — anything
you pass explicitly for a disabled backend on Android/WASM is silently
forced back off, not an error.

## Troubleshooting: OpenGL desktop fails to create a window surface

If the OpenGL backend throws `unable to create an EGL window surface` (or
similar) on Linux while Vulkan and the CPU backend both start fine, this is
almost always the **environment**, not Aura3D: Vulkan and the CPU backend
have their own, more forgiving paths to the display (a Vulkan surface
extension, or SDL's plain software-surface API), while desktop OpenGL asks
SDL for a full EGL/GLX context — something containerized or virtualized GPU
setups (Docker with a passthrough/virtual GPU, some remote-desktop or CI
environments) frequently can't hand out even when Vulkan works perfectly on
the same machine. Confirm this is what's happening by running the same
build's Vulkan or CPU backend (`renderer.backend` in `settings.json`) on the
same machine — if those render normally, the OpenGL failure is a host/EGL
configuration gap, not a code path to debug in Aura3D. Test the OpenGL
backend on a real desktop GPU (not inside a container) before relying on it
for a release.

## Cross-repo build order

Because Aura3D depends on `libink` and `libwma`, a from-scratch build of the
whole stack for any one platform is always:

```
libink  →  libwma  →  Aura3D
```

each installed to the same prefix before the next is configured, per
platform. If `find_package(wma CONFIG REQUIRED)` or
`find_package(ink CONFIG REQUIRED)` fails during Aura3D's configure step,
it means one of these wasn't built for the platform you're targeting, or
wasn't installed where `CMAKE_PREFIX_PATH` is looking.
