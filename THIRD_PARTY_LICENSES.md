# Third-party licenses

Aura3D itself is Apache-2.0 ([LICENSE](LICENSE), [NOTICE](NOTICE)).

## Bundled in `vendor/`

Compiled into the Aura3D archive unless noted.

| Component | Path | Version | License |
|---|---|---|---|
| stb_image, stb_truetype, stb_vorbis | `vendor/stb` | 2.30 / 1.26 / 1.22 | MIT **or** Unlicense (elected: MIT) |
| tinyobjloader | `vendor/tinyobj` | 2.0.0 | MIT |
| glad (loader, GL 4.6 compat) | `vendor/glad` | 0.1.36 | Loader: Public Domain / WTFPL / CC0. Khronos registry content: Apache-2.0 |
| KHR/khrplatform.h | `vendor/KHR` | 2008-2018 | Khronos MIT-style (below) |
| metal-cpp (Apple only, not installed) | `vendor/metal-cpp` | macOS 27 | Apache-2.0, `vendor/metal-cpp/LICENSE.txt` |
| GLES/GLAD shims | `vendor/glad/{android,wasm}` | — | Aura3D (Apache-2.0) |

Vendored files keep their upstream notices in place. stb and tinyobjloader are
byte-identical to upstream; glad is generator output, unedited.

## Used from the system, compiled into Aura3D

Not distributed here; their code is inlined into the library, so the notice
travels with it.

| Component | License | Notes |
|---|---|---|
| glm | MIT **or** Happy Bunny (elected: MIT) | Public headers |
| Vulkan Memory Allocator | MIT | `vk_mem_alloc.h`, implementation built into the library |
| Vulkan-Headers | Apache-2.0 | |
| libink, libwma | Apache-2.0 | Same author |

## Reached through libink / libwma

Linked by those libraries, not by Aura3D. Not audited here; their own repos
carry the obligations.

| Component | License |
|---|---|
| SDL3, GLFW | zlib |
| nlohmann/json | MIT |
| libwayland, libxkbcommon, Xlib | MIT |
| ALSA (`libasound`) | LGPL-2.1, linked dynamically |

## Build tooling shipped in the repo

| Component | Path | License |
|---|---|---|
| Gradle wrapper | `android/gradle/wrapper`, `android/gradlew*` | Apache-2.0 |

## Notices

### MIT

| Component | Copyright |
|---|---|
| stb | Copyright (c) 2017 Sean Barrett |
| tinyobjloader | Copyright (c) 2012-Present, Syoyo Fujita and many contributors |
| Vulkan Memory Allocator | Copyright (c) 2017-2026 Advanced Micro Devices, Inc. |
| glm | Copyright (c) 2005 - G-Truc Creation |

```text
Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
```

### Khronos (`khrplatform.h`)

Copyright (c) 2008-2018 The Khronos Group Inc. Full text at the top of
[vendor/KHR/khrplatform.h](vendor/KHR/khrplatform.h).

### Apache-2.0 components

Khronos registry data in glad (Copyright (c) 2013-2020 The Khronos Group Inc.),
Vulkan-Headers (Copyright 2015-2026 The Khronos Group Inc.), metal-cpp
(Copyright 2024 Apple Inc.) and the Gradle wrapper are under the license in
[LICENSE](LICENSE).
