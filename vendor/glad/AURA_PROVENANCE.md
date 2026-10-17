# glad (vendored)

| | |
|---|---|
| Generator | glad 0.1.36, <https://github.com/Dav1dde/glad> |
| Options | `--api="gl=4.6" --profile="compatibility" --generator="c" --spec="gl"`, all extensions (full line in `glad.c`) |
| Output | `glad.c`, `glad.h`; `../KHR/khrplatform.h` is the Khronos header it includes |

## License

| Part | License |
|---|---|
| Generated loader code | Public Domain, WTFPL or CC0 |
| Khronos registry content in it | Apache-2.0, Copyright (c) 2013-2020 The Khronos Group Inc. |
| `KHR/khrplatform.h` | Khronos MIT-style, notice in the file |

The Apache-2.0 part is why the project `LICENSE` text ships with every build.

`android/` and `wasm/` are Aura3D's own GLES shims, not glad output.
