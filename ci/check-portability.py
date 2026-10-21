#!/usr/bin/env python3
"""Fail if a Linux binary needs more of the system than the floor promises."""
import argparse
from pathlib import Path
import re
import subprocess
import sys


# What every Linux system has, and all a shipped program may link. Everything else (X11, Wayland, ALSA, Vulkan,
# GL) is opened with dlopen() and degrades when it is missing.
ALLOWED = {"libc.so.6", "libm.so.6", "libdl.so.2", "libpthread.so.0", "librt.so.1", "ld-linux-x86-64.so.2",
           "ld-linux-aarch64.so.1"}
RUNTIME = {"libstdc++.so.6", "libgcc_s.so.1"}
# Older binutils print the version without parentheses.
VERSIONED = re.compile(r"\(?(GLIBC|GLIBCXX|CXXABI|GCC)_([0-9.]+)\)?\s+(\S+)")


def version(text):
    return tuple(int(part) for part in text.split("."))


def inspect(path):
    needed = re.findall(r"\(NEEDED\)\s+Shared library: \[(.+?)\]",
                        subprocess.check_output(["readelf", "-d", path], universal_newlines=True))
    required = {}
    for line in subprocess.check_output(["objdump", "-T", path], universal_newlines=True).splitlines():
        if "*UND*" not in line:
            continue
        match = VERSIONED.search(line)
        if match:
            family, ver, symbol = match.groups()
            if family not in required or version(ver) > version(required[family][0]):
                required[family] = (ver, symbol)
    return needed, required


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("binaries", nargs="+", type=Path)
    parser.add_argument("--glibc", default="2.27", help="highest GLIBC_ version a binary may require")
    args = parser.parse_args()

    failed = False
    for path in args.binaries:
        needed, required = inspect(path)
        problems = []
        runtime = [library for library in needed if library in RUNTIME]
        if runtime:
            problems.append(f"links {' '.join(runtime)}"
                            f" (needs {', '.join(f'{family}_{v[0]}' for family, v in sorted(required.items()) if family != 'GLIBC')})"
                            f"; link the C++ runtime statically (AURA_STATIC_RUNTIME)")
        for library in needed:
            if library not in ALLOWED and library not in RUNTIME:
                problems.append(f"links {library}; open it with dlopen() instead")
        glibc = required.get("GLIBC")
        if "libc.so.6" in needed and not glibc:
            problems.append("no GLIBC symbol versions could be read; the check would pass without checking anything")
        if glibc and version(glibc[0]) > version(args.glibc):
            problems.append(f"needs GLIBC_{glibc[0]} ({glibc[1]}); the floor is {args.glibc}")
        status = "FAIL" if problems else "ok  "
        print(f"{status} {path}  (glibc {glibc[0] if glibc else '-'}, NEEDED {' '.join(needed)})")
        for problem in problems:
            print(f"       {problem}")
        failed = failed or bool(problems)
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
