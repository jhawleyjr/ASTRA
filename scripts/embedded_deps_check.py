#!/usr/bin/env python3
"""`just embedded-standalone`'s dependency check (ADR-0008: `embedded/` builds with a C++17 compiler
alone): fails when a configure of `embedded/` pulled in Eigen, yaml-cpp, GoogleTest or Python.

It reads the dependency entries, never the whole cache, so a checkout whose path contains one of
those words (a worktree named `m6-pr11-yaml-route`) passes:

- a CMakeCache entry whose name matches (`yaml-cpp_DIR`, `Eigen3_DIR`, `GTest_DIR`,
  `Python3_EXECUTABLE`: every `find_package` result is named after its package);
- a `PATH` or `FILEPATH` entry whose value matches (`find_library`, `find_path`, `find_program`
  results under any name), with the checkout root removed from the value first;
- the generated compile and link lines (`flags.make`, `link.txt`, `build.ninja`), with the
  checkout root removed first, so an include directory or a library on a link line is caught.

Only the checkout root itself is removed, as given and as its real path (`os.path.realpath`):
`--root "$(pwd)"` is the logical path and CMake records the physical one, so a symlinked checkout
is covered either way. A dependency vendored inside the checkout still matches. Generated files
are read with undecodable bytes replaced, so a stray byte in a path cannot stop the check.
Usage: embedded_deps_check.py <build_dir> --root <checkout root>; exit 1 names each hit.
"""

from __future__ import annotations

import argparse
import os
import re
import sys
from pathlib import Path

PATTERN = re.compile(r"eigen|yaml|gtest|python", re.IGNORECASE)
_ENTRY = re.compile(r"^(?P<name>[^/#:][^:]*):(?P<type>[A-Z]+)=(?P<value>.*)$")
GENERATED = ("flags.make", "link.txt", "build.ninja")


def _strip(text: str, roots: tuple[str, ...]) -> str:
    for root in roots:
        text = text.replace(root, "<root>")
    return text


def _roots(root: str) -> tuple[str, ...]:
    """The checkout root as given and as its real path, longest first, so neither is left
    half-replaced when one contains the other."""
    paths = {root.rstrip("/") or "/", os.path.realpath(root)}
    return tuple(sorted(paths, key=len, reverse=True))


def _lines(path: Path) -> list[str]:
    return path.read_text(encoding="utf-8", errors="replace").splitlines()


def findings(build_dir: Path, root: str) -> list[str]:
    """Every dependency entry or generated line in ``build_dir`` that matches ``PATTERN``."""
    roots = _roots(root)
    hits: list[str] = []
    cache = build_dir / "CMakeCache.txt"
    for number, line in enumerate(_lines(cache), 1):
        entry = _ENTRY.match(line)
        if entry is None:
            continue
        name, kind, value = entry["name"], entry["type"], entry["value"]
        if PATTERN.search(name):
            hits.append(f"{cache}:{number}: entry {name}")
        elif kind in ("PATH", "FILEPATH") and PATTERN.search(_strip(value, roots)):
            hits.append(f"{cache}:{number}: {name} = {value}")
    for path in sorted(p for p in build_dir.rglob("*") if p.name in GENERATED and p.is_file()):
        for number, line in enumerate(_lines(path), 1):
            if PATTERN.search(_strip(line, roots)):
                hits.append(f"{path}:{number}: {line.strip()[:200]}")
    return hits


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="embedded/ configure dependency check (ADR-0008)")
    parser.add_argument("build_dir", type=Path)
    parser.add_argument("--root", required=True, help="the checkout root, removed from values")
    args = parser.parse_args(argv)
    if not (args.build_dir / "CMakeCache.txt").is_file():
        print(f"embedded_deps_check: no CMakeCache.txt in {args.build_dir}", file=sys.stderr)
        return 1
    hits = findings(args.build_dir, args.root)
    for hit in hits:
        print(f"embedded_deps_check: {hit}", file=sys.stderr)
    return 1 if hits else 0


if __name__ == "__main__":
    sys.exit(main())
