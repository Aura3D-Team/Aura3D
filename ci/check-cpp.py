#!/usr/bin/env python3
"""Check owned C++ changes; CLANG_FORMAT/CLANG_TIDY override the LLVM 21 defaults."""
import argparse
from concurrent.futures import ThreadPoolExecutor
import json
import os
from pathlib import Path
import re
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[1]
SOURCE_ROOTS = ("apps", "engine/include", "engine/src", "include", "src", "tests")
EXTENSIONS = {".h", ".hpp", ".hxx", ".inl", ".ipp", ".c", ".cc", ".cpp", ".cxx", ".mm"}
HEADERS = {".h", ".hpp", ".hxx", ".inl", ".ipp"}
CLANG_FORMAT = os.environ.get("CLANG_FORMAT", "clang-format-21")
CLANG_TIDY = os.environ.get("CLANG_TIDY", "clang-tidy-21")


def git(*args):
    return subprocess.check_output(["git", *args], cwd=ROOT, text=True)


def owned(path):
    return (path.is_file() and not path.is_symlink() and path.suffix in EXTENSIONS
            and any(path.is_relative_to(ROOT / part) for part in SOURCE_ROOTS))


def changes(args):
    files = args.files or git("ls-files", "-z", "--cached", "--others", "--exclude-standard").split("\0")
    base = args.base
    if not base or set(base) == {"0"}:
        base = git("hash-object", "-t", "tree", "/dev/null").strip()
    # A bad base must fail, rather than silently passing an empty check.
    git("rev-parse", "--verify", base + "^{tree}")
    untracked = set(git("ls-files", "-z", "--others", "--exclude-standard").split("\0"))
    result = {}
    for name in sorted(set(files)):
        path = ROOT / name
        if not owned(path):
            continue
        if args.all or args.files or name in untracked:
            ranges = [[1, max(1, len(path.read_text().splitlines()))]]
        else:
            diff = git("diff", "--no-ext-diff", "--no-color", "--unified=0", base, "--", name)
            ranges = []
            for start, count in re.findall(r"^@@ -\d+(?:,\d+)? \+(\d+)(?:,(\d+))? @@", diff, re.MULTILINE):
                first, size = int(start), int(count or 1)
                # Deletions can affect adjacent formatting and header semantics.
                ranges.append([max(1, first), max(1, first + max(1, size) - 1)])
        if ranges:
            result[path] = ranges
    return result


def format_cpp(selected, args):
    failed = False
    for path, ranges in selected.items():
        command = [CLANG_FORMAT, "--style=file", "--fallback-style=none"]
        command += ["-i"] if args.fix else ["--dry-run", "--Werror"]
        command += [f"--lines={start}:{end}" for start, end in ranges]
        failed |= subprocess.run([*command, str(path)], cwd=ROOT).returncode != 0
    return failed


def lint_cpp(selected, args):
    build = (ROOT / args.build).resolve()
    database = json.loads((build / "compile_commands.json").read_text())
    compiled = {(Path(entry["directory"]) / entry["file"]).resolve() for entry in database}
    compiled = {path for path in compiled if owned(path)}
    headers_changed = any(path.suffix in HEADERS for path in selected)
    sources = sorted(compiled if headers_changed else compiled.intersection(selected))
    skipped = [str(path.relative_to(ROOT)) for path in selected
               if path.suffix not in HEADERS and path not in compiled]
    if skipped:
        print("Outside this build's compilation database:", ", ".join(skipped), flush=True)
    if not sources:
        print("No changed translation units in this build.")
        return False
    header_filter = "^" + re.escape(str(ROOT)) + "/(" + "|".join(SOURCE_ROOTS) + ")/"
    line_filter = json.dumps([{"name": str(path), "lines": ranges} for path, ranges in selected.items()])
    command = [CLANG_TIDY, "-p", str(build), "--quiet",
               "--config-file=" + str(ROOT / ".clang-tidy"), "--header-filter=" + header_filter]
    if not args.all:
        command += ["--line-filter=" + line_filter]

    def check(path):
        run = subprocess.run([*command, str(path)], cwd=ROOT, text=True,
                             stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        print(f"clang-tidy: {path.relative_to(ROOT)} ({'FAIL' if run.returncode else 'OK'})", flush=True)
        if run.returncode:
            print(run.stdout, flush=True)
        return run.returncode != 0

    with ThreadPoolExecutor(max_workers=args.jobs) as pool:
        return any(list(pool.map(check, sources)))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("check", choices=("format", "lint"))
    parser.add_argument("files", nargs="*", help="Check entire named files instead of the diff")
    parser.add_argument("--base", default="HEAD", help="Diff base (includes unstaged and untracked files)")
    parser.add_argument("--all", action="store_true", help="Check all owned files, including existing debt")
    parser.add_argument("--fix", action="store_true", help="Apply clang-format to the selected lines")
    parser.add_argument("--build", default="build/linux/debug", help="Directory containing compile_commands.json")
    parser.add_argument("--jobs", type=int, default=2)
    args = parser.parse_args()
    if args.jobs < 1 or (args.fix and args.check != "format"):
        parser.error("--jobs must be positive; --fix is only supported for format")
    subprocess.run([CLANG_FORMAT if args.check == "format" else CLANG_TIDY, "--version"], check=True)
    if args.check == "format":
        subprocess.run([CLANG_FORMAT, "--style=file", "--dump-config"], cwd=ROOT,
                       stdout=subprocess.DEVNULL, check=True)
    else:
        subprocess.run([CLANG_TIDY, "--verify-config", "--config-file=" + str(ROOT / ".clang-tidy")],
                       cwd=ROOT, check=True)
    selected = changes(args)
    print(f"Checking {len(selected)} C++ files ({'all lines' if args.all or args.files else 'changed lines'}).", flush=True)
    if not selected:
        return 0
    return int(format_cpp(selected, args) if args.check == "format" else lint_cpp(selected, args))


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        print(f"C++ check failed: {error}", file=sys.stderr)
        sys.exit(1)
