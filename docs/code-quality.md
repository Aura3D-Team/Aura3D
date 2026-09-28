# C++ checks

LLVM 21: `clang-format-21` and `clang-tidy-21`. Configure with
`-DCMAKE_EXPORT_COMPILE_COMMANDS=ON -DCMAKE_CXX_SCAN_FOR_MODULES=OFF` and build once
so generated headers exist. Module scanning adds GCC flags that clang-tidy
cannot parse. The Linux Debug preset already sets both options.

Inside `vulkan-dev`, from this repository:

```sh
python3 ci/check-cpp.py format
python3 ci/check-cpp.py format --fix
python3 ci/check-cpp.py lint --build build/linux/debug
```

| Option | Scope |
|---|---|
| Default | Changes against `HEAD`, including unstaged and untracked C++ files |
| `--base REV` | Changed lines against a PR base or previous push |
| `--all` | Audit all owned C++ files, including existing style/lint debt |
| Explicit file paths | Check entire named files |

CI checks formatting before the Linux Debug build and lint after it. Diagnostics
fail the job. Header changes analyze all owned translation units in the Linux
compilation database; diagnostics remain limited to changed lines. Platform
sources outside that database are reported separately. Vendor, generated and
build directories are excluded. No repository-wide reformat is required.

The container jobs build pinned Ink 0.5.0 sources for their target ABI with
[setup-ink](../.github/actions/setup-ink/action.yml); the image's older Ink is
insufficient. Put the action's installation prefix first in `CMAKE_PREFIX_PATH`.

The [format configuration](../.clang-format) uses the requested LLVM/Allman style,
four-space indentation and 120 columns. The [lint configuration](../.clang-tidy)
enables Clang analysis, bug-prone and performance checks; it preserves existing
public enum layouts and omits heuristic parameter-order and unsafe-C-function
checks. No lint fixes are applied automatically.
