#!/usr/bin/env python3
"""The config-key reference (M7 PR 14, docs/plans/m7-pr14-config-key-reference.md; Q21 of
docs/plans/m7-f16-nesc-oracle.md): renders the key block in each plugin's header comment into
docs/reference/config-keys.md. Stdlib only, so `just docs-check` can run it with the system python3.

Usage: python3 -I -B scripts/config_keys.py [--check] [--root DIR]

Without --check it writes docs/reference/config-keys.md and nothing else. With --check it writes
nothing, and exits 1 when that file is missing or differs from what the blocks give
(scripts/docs_check.py runs it this way). Every problem is printed as `file:line: reason`, exit 1.

The parsing rule:
- The plugins are every directory under plugins/ or aircraft/ whose CMakeLists.txt calls
  astra_add_plugin(; the plugin path is that directory, relative to the root (ADR-0009).
- A plugin's block sits in the leading comment of the first file its SOURCES names: the lines
  from the top of the file that start with `//`. Exactly one block per plugin, so the opener is
  reserved: a second `// Config keys:` line anywhere in that file, in the leading comment or
  below it, is refused as a second block. That is the intended rule, not a bug: a block written
  below the leading comment is refused, never silently skipped.
- The block opens at the exact line `// Config keys:` and ends at the first empty `//` line or at
  the end of the leading comment. Each line's text is what follows `//`, stripped:
  - `none`: the plugin reads no key. It must be the block's only line.
  - a line containing `|` is a row, `key | type | unit | default | meaning`, split on `|` at
    most four times; all five cells non-empty;
  - a line with no `|` continues the previous row's meaning.
- key: a path relative to the plugin's block, segments [a-z][a-z0-9_]* joined by `.`; a segment
  that is a sequence takes `[]`, once per level. Unique within a block.
- type: number, integer, boolean or text (the Config getter that reads it).
- unit: the unit string as ADR-0002 writes it in the SignalRegistry (`1` for a dimensionless
  number or a count), the config's own boundary unit where it has one, or `-` for a boolean or
  text key.
- default: `required`; the value an omitted key takes, as YAML writes it; or `-`, when leaving
  the key out means something the meaning states.
- normalize() maps the core's leaf path (numeric index segments, as Config::unknown_keys names
  them) to this notation: tests/python/test_config_keys.py uses this same function.
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path
from typing import NamedTuple

PLUGIN_ROOTS = ("plugins", "aircraft")
REFERENCE = "docs/reference/config-keys.md"
OPENER = "// Config keys:"
TYPES = ("number", "integer", "boolean", "text")
REGENERATE = "python3 -I -B scripts/config_keys.py"

_SEGMENT = r"[a-z][a-z0-9_]*(?:\[\])*"
KEY_RE = re.compile(rf"{_SEGMENT}(?:\.{_SEGMENT})*")
INDEX_RE = re.compile(r"[0-9]+")
ADD_PLUGIN_RE = re.compile(r"\bastra_add_plugin\s*\(([^)]*)\)")
CODE_SPAN_RE = re.compile(r"(`[^`]*`)")


class Row(NamedTuple):
    key: str
    type: str
    unit: str
    default: str
    meaning: str
    line: int


class Block(NamedTuple):
    plugin: str  # "plugins/physical/mass/constant"
    source: str  # "plugins/physical/mass/constant/constant.cpp"
    rows: tuple[Row, ...]  # empty for a `none` block


def normalize(path: str) -> str:
    """'peer.schedule.0.throttle.1' -> 'peer.schedule[].throttle[]': each all-digit segment (a
    sequence index, as the core names a leaf) is dropped and `[]` appended to the segment before
    it."""
    out: list[str] = []
    for segment in path.split("."):
        if INDEX_RE.fullmatch(segment):
            if not out:
                raise ValueError(f"{path!r} starts with a sequence index")
            out[-1] += "[]"
        else:
            out.append(segment)
    return ".".join(out)


def discover_plugins(root: Path, problems: list[str]) -> list[tuple[str, Path]]:
    """(plugin path, first SOURCES file) for every plugin under PLUGIN_ROOTS, sorted by path."""
    found: list[tuple[str, Path]] = []
    for top in PLUGIN_ROOTS:
        for cmakelists in sorted((root / top).rglob("CMakeLists.txt")):
            text = "\n".join(
                line.split("#", 1)[0]
                for line in cmakelists.read_text(encoding="utf-8").splitlines()
            )
            call = ADD_PLUGIN_RE.search(text)
            if call is None:
                continue
            rel = cmakelists.relative_to(root).as_posix()
            args = call.group(1).split()
            if "SOURCES" not in args or args.index("SOURCES") + 1 >= len(args):
                problems.append(f"{rel}: astra_add_plugin( names no SOURCES file")
                continue
            source = cmakelists.parent / args[args.index("SOURCES") + 1]
            if not source.is_file():
                problems.append(f"{rel}: its first SOURCES file {source.name} does not exist")
                continue
            found.append((cmakelists.parent.relative_to(root).as_posix(), source))
    return sorted(found)


def parse_block(plugin: str, source: Path, root: Path, problems: list[str]) -> Block | None:
    """The one key block in `source`'s leading comment, or None (with each reason in `problems`)."""
    rel = source.relative_to(root).as_posix()
    lines = source.read_text(encoding="utf-8").splitlines()
    leading = 0
    while leading < len(lines) and lines[leading].startswith("//"):
        leading += 1
    openers = [i for i, line in enumerate(lines) if line.rstrip() == OPENER]
    if not openers:
        problems.append(f"{rel}:1: no key block (a line '{OPENER}' in the leading comment)")
        return None
    if len(openers) > 1:
        problems.append(f"{rel}:{openers[1] + 1}: a second key block")
        return None
    start = openers[0]
    if start >= leading:
        problems.append(
            f"{rel}:{start + 1}: the key block must sit in the file's leading comment "
            f"(lines 1 to {leading})"
        )
        return None

    rows: list[Row] = []
    none_line = 0
    bad = len(problems)
    for i in range(start + 1, leading):
        text = lines[i][2:].strip()
        where = f"{rel}:{i + 1}"
        if not text:
            break
        if none_line:
            problems.append(f"{where}: 'none' must be the key block's only line")
        elif text == "none":
            if rows:
                problems.append(f"{where}: 'none' must be the key block's only line")
            none_line = i + 1
        elif "|" in text:
            cells = [cell.strip() for cell in text.split("|", 4)]
            if len(cells) != 5 or not all(cells):
                problems.append(
                    f"{where}: a row needs five non-empty cells: "
                    "key | type | unit | default | meaning"
                )
                continue
            key, kind, unit, default, meaning = cells
            if not KEY_RE.fullmatch(key):
                problems.append(
                    f"{where}: key {key!r} is not dotted [a-z][a-z0-9_]* segments, each with "
                    "optional []"
                )
            if kind not in TYPES:
                problems.append(f"{where}: type {kind!r} is not one of {', '.join(TYPES)}")
            if any(row.key == key for row in rows):
                problems.append(f"{where}: duplicate key {key!r}")
            rows.append(Row(key, kind, unit, default, meaning, i + 1))
        elif not rows:
            problems.append(f"{where}: a continuation line before any row")
        else:
            last = rows[-1]
            rows[-1] = last._replace(meaning=f"{last.meaning} {text}")
    if not rows and not none_line and len(problems) == bad:
        problems.append(f"{rel}:{start + 1}: an empty key block (write 'none' if it reads no key)")
    if len(problems) > bad:
        return None
    return Block(plugin, rel, tuple(rows))


def collect_blocks(root: Path) -> tuple[list[Block], list[str]]:
    problems: list[str] = []
    blocks = []
    for plugin, source in discover_plugins(root, problems):
        block = parse_block(plugin, source, root, problems)
        if block is not None:
            blocks.append(block)
    return blocks, problems


def escape(text: str) -> str:
    """Markdown-safe table cell text: `|` escaped everywhere (a GitHub table splits on it even
    inside a code span), and `\\ * _ <` escaped outside backtick spans, so a formula renders as
    written."""
    out = []
    for part in CODE_SPAN_RE.split(text):
        if not (part.startswith("`") and part.endswith("`") and len(part) >= 2):
            for char in "\\*_<":
                part = part.replace(char, "\\" + char)
        out.append(part.replace("|", "\\|"))
    return "".join(out)


HEADER = f"""# Config-key reference

Generated by `scripts/config_keys.py` from the key block in each plugin's header comment (M7
PR 14). Do not edit it by hand: change the block, then run `{REGENERATE}`.
`just docs-check` fails when this file is stale, and `tests/python/test_config_keys.py` checks
every block against the keys the core reports consumed by every tracked config.

Scope: the keys each plugin under `plugins/` and `aircraft/` reads from its own block of a
config, the map that names it with `plugin:`. The core's own keys are not listed here: `sim`,
`log`, `initial`, `vehicle` outside the model blocks, the `plugin` and `divisor` of every block,
and the `transport` block's keys outside its plugin's own (docs/architecture.md §13 describes the
config's shape). Nor are the test plugins under `tests/plugins/`.

A key is a path relative to its block: `a[].b` is the key `b` in each entry of the sequence `a`,
and `a[]` is each entry of a sequence of scalars. A key whose default is `-` is optional, and its
meaning says what leaving it out does.
"""


def render(blocks: list[Block]) -> str:
    out = [HEADER]
    for block in sorted(blocks):
        out.append(f"\n## `{block.plugin}`\n\nSource: `{block.source}`.\n\n")
        if not block.rows:
            out.append("Reads no config key: a config names it by its path alone.\n")
            continue
        out.append("| Key | Type | Unit | Default | Required | Meaning |\n")
        out.append("|---|---|---|---|---|---|\n")
        for row in block.rows:
            required = row.default == "required"
            default = "" if required else ("-" if row.default == "-" else f"`{row.default}`")
            unit = "-" if row.unit == "-" else f"`{row.unit}`"
            cells = [
                f"`{row.key}`",
                row.type,
                unit,
                default,
                "yes" if required else "no",
                escape(row.meaning),
            ]
            out.append("| " + " | ".join(cells) + " |\n")
    return "".join(out)


def main() -> int:
    parser = argparse.ArgumentParser(
        description=f"Render the plugins' key blocks into {REFERENCE}."
    )
    parser.add_argument(
        "--root",
        default=str(Path(__file__).resolve().parent.parent),
        help="repository root (default: this script's own repository)",
    )
    parser.add_argument(
        "--check", action="store_true", help=f"write nothing; exit 1 if {REFERENCE} is stale"
    )
    args = parser.parse_args()
    root = Path(args.root).resolve()

    blocks, problems = collect_blocks(root)
    if problems:
        for problem in problems:
            print(problem, file=sys.stderr)
        return 1
    text = render(blocks)
    keys = sum(len(block.rows) for block in blocks)
    summary = f"{len(blocks)} plugins, {keys} keys"
    reference = root / REFERENCE

    if args.check:
        if not reference.is_file():
            print(f"{REFERENCE}: missing; generate it with: {REGENERATE}", file=sys.stderr)
            return 1
        current = reference.read_text(encoding="utf-8")
        if current != text:
            have, want = current.splitlines(), text.splitlines()
            line = next(
                (i + 1 for i, (a, b) in enumerate(zip(have, want, strict=False)) if a != b),
                min(len(have), len(want)) + 1,
            )
            print(
                f"{REFERENCE}:{line}: stale (differs from the key blocks); regenerate it with: "
                f"{REGENERATE}",
                file=sys.stderr,
            )
            return 1
        print(f"config-keys: OK ({summary})")
        return 0

    reference.parent.mkdir(parents=True, exist_ok=True)
    reference.write_text(text, encoding="utf-8", newline="\n")
    print(f"config-keys: wrote {REFERENCE} ({summary})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
