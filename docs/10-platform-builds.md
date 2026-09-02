# 10. Platform Builds

Aura3D targets four platforms from one CMake project: Linux (native), Windows
(native), Android (NDK), and WebAssembly (Emscripten). Each has its own preset
and its own backend restrictions, applied automatically by `cmake/Platform.cmake`
— you don't set `AURA_ENABLE_*` by hand per platform.

| Platform | Backends compiled in | Why |
|---|---|---|
| Linux | Vulkan, OpenGL, CPU | Everything — the default dev target. |
| Windows | Vulkan, OpenGL, CPU | Everything, same as Linux — MSVC gets `/EHsc /utf-8 /Zc:__cplusplus` plus `NOMINMAX`/`WIN32_LEAN_AND_MEAN` in place of the GCC/Clang flags (`cmake/Platform.cmake`). |
| Android | Vulkan only | `AURA_ENABLE_OPENGL`/`AURA_ENABLE_CPU` are force-disabled; Vulkan is the modern mobile GPU path. |
| WASM | OpenGL only | Compiles to WebGL2; `AURA_ENABLE_VULKAN`/`AURA_ENABLE_CPU` are force-disabled (no browser Vulkan, and the CPU backend's `wma` framebuffer path isn't wired for Emscripten). |

This means `renderer.backend` in `settings.json` only really has a choice on
Linux — Android and WASM builds only ever have one backend available, and
`RendererFactory` will resolve to it regardless of what the JSON says (see
[03-engine-and-renderer.md](03-engine-and-renderer.md#backend-resolution--fallback)).

Aura3D depends on `libink` and `libwma`, built for the **same platform**
first — each preset's `CMAKE_PREFIX_PATH` points at where those two are
expected to already be installed. glm is the third dependency; it is
header-only, so one copy serves every target, but `cmake/Dependencies.cmake`
does look for it explicitly and a configure fails without it.

## Linux

```bash
cmake --preset linux-release   # or linux-debug
cmake --build --preset linux-release
```

Output lands under `build/linux/release/`. No Emscripten or NDK needed —
this is the fast inner-loop target for actual gameplay iteration.

## Windows

Prerequisites: a C++23 MSVC toolchain (Visual Studio 2022 17.10+, or the
Build Tools equivalent) on `PATH` — open a "Developer Command Prompt"/"Developer
PowerShell", or run `vcvarsall.bat x64` yourself first — plus
[vcpkg](https://vcpkg.io) for `libink`/`libwma`'s own dependencies
(`nlohmann_json`, SDL3, GLFW) and Aura3D's own glm and Vulkan
headers/loader. `windows-latest` GitHub runners ship vcpkg preinstalled at
`%VCPKG_INSTALLATION_ROOT%`; locally,
[clone and bootstrap it](https://learn.microsoft.com/vcpkg/get_started/get-started)
if you don't have it yet.

```powershell
vcpkg install nlohmann-json:x64-windows-static sdl3:x64-windows-static `
    glfw3:x64-windows-static glm:x64-windows-static `
    vulkan-headers:x64-windows-static vulkan-loader:x64-windows-static

# windows-release below, or windows-debug for a Debug build
cmake --preset windows-release `
    -DCMAKE_TOOLCHAIN_FILE="$env:VCPKG_INSTALLATION_ROOT/scripts/buildsystems/vcpkg.cmake" `
    -DVCPKG_TARGET_TRIPLET=x64-windows-static `
    -DCMAKE_MSVC_RUNTIME_LIBRARY='MultiThreaded$<$<CONFIG:Debug>:Debug>'
cmake --build --preset windows-release
```

Output lands under `build/windows/release/`, same layout as Linux. Unlike
Linux/Android/WASM, `libink`/`libwma` have no prebuilt copy waiting in a
container for Windows — build and `cmake --install` each into the same
prefix first (see [Cross-repo build order](#cross-repo-build-order) below),
then point `CMAKE_PREFIX_PATH` at it: `windows-release`'s
`CMAKE_PREFIX_PATH` cache variable already resolves to
`$env:LOCAL_PREFIX/windows/release`, matching `cmake/Dependencies.cmake`'s
platform-prefix lookup. `.github/workflows/ci.yml`'s `windows-build` job is
a complete worked example of the whole ink → wma → Aura3D chain built from
source with vcpkg, if you'd rather read a script than prose.

`x64-windows-static` puts the vcpkg ports on the static CRT (`/MT`), so
`CMAKE_MSVC_RUNTIME_LIBRARY` has to say the same for this project — CMake's
own default is `/MD`, and the mismatch surfaces as `LNK2038` at the first
executable link, not at configure time.

`AURA_ENABLE_VULKAN`/`AURA_ENABLE_OPENGL`/`AURA_ENABLE_CPU` all stay `ON` by
default here — MSVC has no `-march=native` equivalent, so
`AURA_NATIVE_OPTIMIZE` is silently a no-op rather than something you need to
turn off yourself (`check_cxx_compiler_flag` fails cleanly under `cl.exe`).

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
cmake --install build/android --prefix "$LOCAL_PREFIX/android"   # optional
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

### Installing on a real device: signed vs. unsigned APKs

`assembleDebug` is signed automatically with Android's default debug
keystore. `assembleRelease` is **not** signed by default (`android/app/build.gradle`'s
`release {}` block has no `signingConfig`) — installing
`app-release-unsigned.apk` via `adb install` fails outright with
`INSTALL_PARSE_FAILED_NO_CERTIFICATES`. For device testing during
development, install the debug APK instead:

```bash
adb install -r android/app/build/outputs/apk/debug/app-debug.apk
```

### Debugging on a real device over Wi-Fi (no USB, no emulator)

Useful when the dev environment is a container without USB passthrough or
`/dev/kvm` (so the Android emulator isn't an option either) — wireless ADB
only needs the phone and the machine running `adb` to be on the same
network.

**One-time pairing**, on the phone: Settings → Developer options (tap
"Build number" 7 times under About phone if this isn't visible yet) →
Wireless debugging → **Pair device with pairing code**. This shows an IP,
a port, and a 6-digit code — note that this pairing port is *different*
from the port used to actually connect (below).

```bash
adb pair <ip>:<pairing-port>   # enter the 6-digit code when prompted
```

**Connecting** (each session — the main Wireless debugging screen shows a
separate IP:port for this, not the pairing one):

```bash
adb connect <ip>:<connect-port>
adb devices                     # confirm it shows up as "device", not "unauthorized"
```

**Installing and launching:**

```bash
adb install -r path/to/app.apk
adb shell am force-stop com.aura3d.sandbox        # kill a previous run first
adb shell am start -n com.aura3d.sandbox/.MainActivity
```

**Reading logs** — `-c` clears the buffer so you only see what happens
next, `-d` dumps once and exits rather than streaming:

```bash
adb logcat -c
adb shell am start -n com.aura3d.sandbox/.MainActivity
adb logcat -d -s SDL:V                             # SDL's own lifecycle/JNI log lines
adb logcat -d | grep -iE 'fatal|aura3d::|libc\+\+abi|assert'   # crash signal + backtrace
```

A crash backtrace (tag `DEBUG`, one frame per line, `pc <offset> <library>
(<demangled symbol>+<offset>)`) is usually enough to identify which Aura3D
function crashed without needing a debugger attached.

**Screenshot** (handy for visually confirming a fix without a monitor/capture
card on hand):

```bash
adb exec-out screencap -p > screen.png
```

**Inspecting a native library's exported symbols** — useful for diagnosing
JNI/dlsym lookup failures (e.g. confirming `SDL_main` is present and
unmangled, not C++ name-mangled):

```bash
$ANDROID_NDK_HOME/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-nm -D path/to/libmain.so | grep -i main
```

**Disconnecting when you're done:**

```bash
adb disconnect <ip>:<connect-port>   # drop one device
adb disconnect                        # drop every network-attached device
```

`disconnect` only closes the TCP connection — the pairing survives, so the
next session just needs `adb connect` again (no re-pairing with a code)
provided the phone keeps Wireless debugging enabled. Note the phone's
connect **port changes** each time Wireless debugging is toggled off/on (or
after a reboot), so check the current one on the Wireless debugging screen
rather than assuming the previous port still applies.

To also shut down the local `adb` server (releases port 5037; the next `adb`
command starts a fresh one automatically):

```bash
adb kill-server
```

Turning off **Wireless debugging** on the phone disconnects it from that side
too, and is worth doing when you're finished — it leaves the debug channel
open to the local network otherwise.

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

### The server must send COOP/COEP headers

`ink` propagates `-pthread`, so Emscripten emits a **shared**
`WebAssembly.Memory`. A shared memory requires `SharedArrayBuffer`, and every
current browser only exposes that to a *cross-origin isolated* page. The
server therefore has to send both of these on every response:

```
Cross-Origin-Opener-Policy: same-origin
Cross-Origin-Embedder-Policy: require-corp
```

`--serve` already does this. A plain `python3 -m http.server` does **not**, and
neither do most static hosts by default — without the headers the module
throws while instantiating its memory and the canvas just stays blank, with
nothing obviously wrong in the console. If a deployed build shows a blank
canvas, check these headers first.

On GitHub Pages, which cannot set custom headers, the usual workaround is a
service worker that re-serves responses with the headers attached (e.g.
`coi-serviceworker`); anything you control directly (nginx, Caddy, CloudFront,
S3 + Lambda\@Edge) can just add them.

### The worker pool is pre-spawned

The WASM link adds `-sPTHREAD_POOL_SIZE=navigator.hardwareConcurrency`.
`JobSystem` builds its `ink::ThreadPool` lazily on the first `dispatch()`,
which happens on the main thread — and a browser Worker only starts once the
main thread returns to the event loop, which `dispatch()` never does before
blocking on its band futures. Pre-spawning the pool at startup is what keeps
that first call from deadlocking rather than merely being slow.

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

On Windows specifically, `libink` doesn't ship its own `windows-debug`/
`windows-release` presets (only `libwma` and Aura3D do), so build it with a
plain out-of-preset invocation first:

```powershell
cmake -S libink -B libink/build -G Ninja `
    -DCMAKE_BUILD_TYPE=Release `
    -DCMAKE_TOOLCHAIN_FILE="$env:VCPKG_INSTALLATION_ROOT/scripts/buildsystems/vcpkg.cmake" `
    -DVCPKG_TARGET_TRIPLET=x64-windows-static
cmake --build libink/build --parallel
cmake --install libink/build --prefix "$env:LOCAL_PREFIX/windows/release"
```

then `libwma`'s and Aura3D's own `windows-release` presets (pointed at the
same `CMAKE_PREFIX_PATH`) pick it up normally.
