# 11. Using a Release

This covers the other side of [10-platform-builds.md](10-platform-builds.md):
not building Aura3D itself, but consuming the prebuilt archives attached to a
[GitHub Release](https://github.com/Aura3D-Team/Aura3D/releases) in your own
project. If you're contributing to Aura3D itself, skip to
[Development environment](#development-environment) below instead.

Each release ships three archives — one per platform, from
`.github/workflows/release.yml`:

| Archive | Contents | What it is |
|---|---|---|
| `aura3d-vX.Y.Z-linux-x86_64.tar.gz` | `lib/libAura3D.a`, `include/aura/*.h`, `lib/cmake/Aura3D/*` | A library to link into your own app |
| `aura3d-vX.Y.Z-android-arm64-v8a.tar.gz` | `lib/libAura3D.a`, `include/aura/*.h`, `examples/aura3d-sandbox-debug.apk` | A library to embed in your own Android app, plus a ready-to-install debug build of the Sandbox demo |
| `aura3d-vX.Y.Z-wasm.tar.gz` | `index.html`, `pkg/{Aura3D.js,Aura3D.wasm,settings.json,resources/}` | The compiled **Sandbox demo**, ready to run in a browser — not a linkable library (see [WebAssembly](#webassembly) below) |

On Linux and Android, `libAura3D.a` already has `libwma`/`libink`'s object
code merged into it (see the release workflow's "Merge wma/ink into a
self-contained static library" step) — you don't need to separately obtain
those two. Vulkan/OpenGL stay external system dependencies either way, since
those aren't things you'd bundle.

## Linux

**Prerequisites to build against it**: a C++23 compiler, CMake, and — only
for whichever backend(s) your app actually uses — the Vulkan SDK (or
`libvulkan-dev` + a loader) and/or OpenGL development headers. You do **not**
need `libwma`/`libink` installed separately; they're already inside the `.a`.

```bash
tar xzf aura3d-vX.Y.Z-linux-x86_64.tar.gz
```

```cmake
find_package(Aura3D REQUIRED PATHS /path/to/aura3d-linux-x86_64)
add_executable(MyGame main.cpp)
target_link_libraries(MyGame PRIVATE Aura3D::Aura3D)
```

**To run** an app built this way (or a prebuilt binary someone hands you),
the machine needs a working Vulkan driver + loader (or a Mesa OpenGL driver,
depending which backend `settings.json` selects) — the same requirement any
Vulkan/OpenGL application has, nothing Aura3D-specific.

## Android

**Prerequisites**: the Android NDK (27+) and Gradle/CMake, same as building
Aura3D itself. Unlike Linux, `cmake/Install.cmake` doesn't export a CMake
package for Android, so there's no `find_package(Aura3D)` here — link the
archive's contents by hand in your app's `CMakeLists.txt`:

```cmake
set(AURA3D_RELEASE "/path/to/aura3d-android-arm64-v8a")

add_library(mygame SHARED main.cpp)

target_include_directories(mygame PRIVATE "${AURA3D_RELEASE}/include")
target_link_libraries(mygame PRIVATE
    "${AURA3D_RELEASE}/lib/libAura3D.a"
    Vulkan::Vulkan   # find_package(Vulkan REQUIRED) first; Android's own
                     # libvulkan.so ships with the OS on Vulkan-capable devices
)
```

Match `ANDROID_STL=c++_shared` and `ANDROID_PLATFORM=android-29` (or newer)
in your own app's CMake configuration — this is what the released `.a` was
built with, and a mismatch here is an ABI break, not a warning.

**To run**: any device/emulator with Vulkan support (Android's Vulkan is
part of the OS image, not something a user installs separately).

**Trying it without building anything first**: `examples/aura3d-sandbox-debug.apk`
is a debug build of the Sandbox demo, signed with Android's default debug
keystore, so it installs with no signing setup on your end:

```bash
adb install -r examples/aura3d-sandbox-debug.apk
adb shell am start -n com.aura3d.sandbox/.MainActivity
```

Useful for confirming Aura3D actually renders on your device before writing
any code against the library, and for reading its logs (see
[10-platform-builds.md](10-platform-builds.md#android)'s "Reading logs"
section) if it doesn't.

## WebAssembly

The WASM archive is **not** a library — it's the compiled Sandbox demo
itself (`apps/Sandbox/CMakeLists.txt` is what Emscripten actually compiles to
`Aura3D.js`/`.wasm`; there's no separate Aura3D-for-web artifact today). So
"using" it means:

```bash
tar xzf aura3d-vX.Y.Z-wasm.tar.gz
cd aura3d-wasm
python3 -m http.server 8080   # file:// won't work — WASM needs real HTTP fetches
```

then open `http://localhost:8080` in a browser with WebGL2 support (any
current Chrome/Firefox/Safari/Edge). There is no install step on the runtime
side beyond "a browser that supports WebGL2" — this is the OpenGL/WebGL2
equivalent of the Vulkan driver requirement above.

If you want to build **your own** WASM app against Aura3D rather than run
the bundled demo, there's currently no prebuilt library artifact for that —
you'd build from source with Emscripten, per
[10-platform-builds.md#webassembly](10-platform-builds.md#webassembly).

## Development environment

If you're building Aura3D itself (not just consuming a release), the
`arthurrl/vulkan-dev:lts` container used by CI and the release workflow —
with the Vulkan SDK, Emscripten, and Android NDK all preinstalled — is built
from [Aura3D-Team/qt_dev](https://github.com/Aura3D-Team/qt_dev):

```bash
git clone https://github.com/Aura3D-Team/qt_dev
cd qt_dev
python3 build_run.py --build --base --retry 2 --image_tag="base"
python3 build_run.py --build --retry 2 --image_tag="lts"
python3 build_run.py --run --image_tag="lts"
```

Inside the running container, `libink`/`libwma` are already built and
installed for every platform Aura3D targets, so
[01-getting-started.md](01-getting-started.md) works immediately with no
further setup.
