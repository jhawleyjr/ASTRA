#!/usr/bin/env python3
"""By-hand cross-check of tests/golden/flightgear/fgnetfdm_v24.py against FlightGear's header
(docs/plans/m5-tooling.md Q21). Never run by `just check` (the ADR-0013 generator pattern): it needs
the gitignored reference under docs/refs/ and a C++ compiler, and its output is recorded in the PR
that changes the table.

It checks both reference files' SHA-256 against the table, compiles net_fdm.hxx with g++ and a
one-line stub for <simgear/misc/stdint.hxx>, prints sizeof, alignof and every member's offsetof and
sizeof, and compares each with the table's offset, ctype and length. Exit 0 only when all agree.

    python3 scripts/fgnetfdm_layout_check.py [--refs DIR] [--cxx g++] [--table FILE]

--refs defaults to the main checkout's docs/refs (docs/refs is gitignored, so a worktree has no
copy; docs/lessons.md 2026-09-17), found through `git rev-parse --git-common-dir`. --table defaults
to the golden table; another path lets a deliberately wrong copy show that the check fails.
"""

from __future__ import annotations

import argparse
import hashlib
import importlib.util
import subprocess
import sys
import tempfile
from pathlib import Path
from types import ModuleType

REPO = Path(__file__).resolve().parent.parent
TABLE = REPO / "tests/golden/flightgear/fgnetfdm_v24.py"
CTYPE_SIZE = {"uint32_t": 4, "int32_t": 4, "float": 4, "double": 8}


def load_table(path: Path) -> ModuleType:
    spec = importlib.util.spec_from_file_location("fgnetfdm_v24", path)
    if spec is None or spec.loader is None:
        raise SystemExit(f"cannot load {path}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def default_refs() -> Path:
    common = subprocess.run(
        ["git", "-C", str(REPO), "rev-parse", "--path-format=absolute", "--git-common-dir"],
        check=True,
        capture_output=True,
        text=True,
    ).stdout.strip()
    return Path(common).parent / "docs/refs"


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def program(table: ModuleType) -> str:
    lines = [
        "#include <cstddef>",
        "#include <cstdio>",
        '#include "net_fdm.hxx"',
        "int main() {",
        '  std::printf("sizeof %zu\\n", sizeof(FGNetFDM));',
        '  std::printf("alignof %zu\\n", alignof(FGNetFDM));',
    ]
    for field in table.FIELDS:
        lines.append(
            f'  std::printf("{field.name} %zu %zu\\n", offsetof(FGNetFDM, {field.name}), '
            f"sizeof(FGNetFDM::{field.name}));"
        )
    lines += ["  return 0;", "}", ""]
    return "\n".join(lines)


def main() -> int:
    parser = argparse.ArgumentParser(description=(__doc__ or "").splitlines()[0])
    parser.add_argument("--refs", type=Path, default=None)
    parser.add_argument("--cxx", default="g++")
    parser.add_argument("--table", type=Path, default=TABLE)
    args = parser.parse_args()
    refs: Path = args.refs if args.refs is not None else default_refs()
    table_path: Path = args.table
    table = load_table(table_path)
    header = refs / "net_fdm.hxx"
    structs = refs / "native_structs.cxx"
    failures: list[str] = []

    print(f"table: {table_path} ({table.RELEASE})")
    for path, expected in (
        (header, table.NET_FDM_HXX_SHA256),
        (structs, table.NATIVE_STRUCTS_CXX_SHA256),
    ):
        got = sha256(path)
        status = "ok" if got == expected else "MISMATCH"
        print(f"sha256 {path}: {got} {status}")
        if got != expected:
            failures.append(f"{path.name}: sha256 {got}, table says {expected}")

    with tempfile.TemporaryDirectory(prefix="fgnetfdm_layout_") as directory:
        tmp = Path(directory)
        stub = tmp / "simgear/misc/stdint.hxx"
        stub.parent.mkdir(parents=True)
        stub.write_text("#include <stdint.h>\n")
        source = tmp / "layout.cpp"
        source.write_text(program(table))
        binary = tmp / "layout"
        compile_cmd = [
            args.cxx,
            "-std=c++17",
            f"-I{tmp}",
            f"-I{refs}",
            str(source),
            "-o",
            str(binary),
        ]
        version = subprocess.run(
            [args.cxx, "--version"], check=True, capture_output=True, text=True
        )
        print(f"compiler: {version.stdout.splitlines()[0]}")
        subprocess.run(compile_cmd, check=True)
        output = subprocess.run([str(binary)], check=True, capture_output=True, text=True).stdout

    measured: dict[str, tuple[int, ...]] = {}
    for line in output.splitlines():
        name, *numbers = line.split()
        measured[name] = tuple(int(n) for n in numbers)

    def compare(label: str, got: int, expected: int) -> None:
        status = "ok" if got == expected else "MISMATCH"
        print(f"{label}: compiler {got}, table {expected} {status}")
        if got != expected:
            failures.append(f"{label}: compiler {got}, table {expected}")

    compare("sizeof(FGNetFDM)", measured["sizeof"][0], table.SIZE)
    compare("alignof(FGNetFDM)", measured["alignof"][0], table.ALIGNMENT)
    end = 0
    for field in table.FIELDS:
        offset, size = measured[field.name]
        table_size = CTYPE_SIZE[field.ctype] * field.length
        compare(f"{field.name} offset", offset, field.offset)
        compare(f"{field.name} size", size, table_size)
        if field.offset != end:
            print(f"implicit padding: {field.offset - end} byte(s) before {field.name}")
        end = field.offset + table_size
    if end != table.SIZE:
        print(f"implicit padding: {table.SIZE - end} byte(s) at the end")
    members = sum(CTYPE_SIZE[f.ctype] * f.length for f in table.FIELDS)
    print(f"implicit padding total: {table.SIZE - members} byte(s)")

    if failures:
        print(f"FAILED: {len(failures)} mismatch(es)")
        for failure in failures:
            print(f"  - {failure}")
        return 1
    print(f"OK: {len(table.FIELDS)} members, sizeof {table.SIZE}, every offset and size agrees")
    return 0


if __name__ == "__main__":
    sys.exit(main())
