"""Emit the F-16's DAVE-ML tables as committed C++ (M7 PR 6b, Q14).

The plan is docs/plans/m7-pr6b-f16-aircraft.md.

Reads P1, NASA's F-16 DAVE-ML package (docs/plans/m7-f16-inventory.md section 1), and writes:
- aircraft/f16/physical/aero/aero_tables.hpp: F16_aero.dml's four breakpoint sets and 18 tables
  (744 values; inventory section 2);
- aircraft/f16/hardware/propulsion/engine/engine_tables.hpp: F16_prop.dml's two breakpoint sets and
  three thrust tables (108 values; inventory section 3).

Every value is the source's own text, copied verbatim after a check that it is a decimal literal; no
value passes through a Python float. The tables keep the source's units (deg, ft, lbf): the plugins'
math headers derive their SI arrays from these at compile time (ADR-0002). Each table is written in
the order the file states ("last breakpoint changes most rapidly"), the first breakpoint set as the
outer index.

Refusals, each before anything is written:
- a source file whose SHA-256 is not the inventory's (section 1.1);
- a table whose value count is not the product of its breakpoints, or totals other than 744
  and 108;
- an independentVarRef that is not extrapolate="neither", or whose min and max are not its
  breakpoints' ends;
- a table, breakpoint set or function other than the ones listed below.

This script shares no code with aircraft/f16/tests/extract_f16_checkdata.py, the checkData reader
that the plugins' tests compare against (docs/lessons.md, 2026-09-25).

Run from a worktree; gitignored refs exist only in the main checkout (docs/refs/README.md):
    python3 -I -B aircraft/f16/tools/generate_f16_tables.py [--refs PATH] [--out DIR]
--refs defaults to the main checkout's docs/refs/Atmospheric_models/F16_package, resolved through
`git rev-parse --git-common-dir` as tests/golden/nesc/generate_nesc_band.py does. --out writes both
headers flat into DIR instead of their committed paths (tests/python/test_f16_generated.py).
"""

from __future__ import annotations

import argparse
import hashlib
import re
import subprocess
import sys
import xml.etree.ElementTree as ET
from dataclasses import dataclass
from pathlib import Path

NS = "{http://daveml.org/2010/DAVEML}"

# docs/plans/m7-f16-inventory.md section 1.1.
SHA256 = {
    "F16_aero.dml": "82796577b989b5f3becb7d13430ff6c619b3ecd2e642a6427b743658fb177e51",
    "F16_prop.dml": "e5fd7772e5c05cdac666c94febbe4cb50139418b26045affd92dbd198f2215c6",
}
SOURCE_DIR = "F16_S119_source"

DECIMAL = re.compile(r"[+-]?(\d+\.?\d*|\.\d+)([eE][+-]?\d+)?")

# The expected contents, so a changed file is refused rather than emitted differently.
AERO_BREAKPOINTS = ("ALPHA1", "BETA1", "BETA2", "DE1")
# dependentVarRef varID -> gtID, in the file's order (inventory section 2).
AERO_TABLES = (
    ("cxt", "CX_table_def"),
    ("czt", "CZ0_table_def"),
    ("cmt", "Cm0_table_def"),
    ("absCl0", "Cl0_table_def"),
    ("absCn0", "Cn0_table_def"),
    ("cxq", "CXq_table_def"),
    ("cyr", "CYr_table_def"),
    ("cyp", "CYp_table_def"),
    ("czq", "CZq_table_def"),
    ("clr", "Clr_table_def"),
    ("clp", "Clp_table_def"),
    ("cmq", "Cmq_table_def"),
    ("cnr", "Cnr_table_def"),
    ("cnp", "Cnp_table_def"),
    ("dclda", "dlda_table_def"),
    ("dcldr", "dldr_table_def"),
    ("dcnda", "dnda_table_def"),
    ("dcndr", "dndr_table_def"),
)
AERO_TOTAL = 744
PROP_BREAKPOINTS = ("MACH_PTS", "ALT_PTS")
PROP_TABLES = (
    ("T_IDLE", "T_IDLE_table"),
    ("T_MIL", "T_MIL_table"),
    ("T_MAX", "T_MAX_table"),
)
PROP_TOTAL = 108

AERO_OUT = Path("aircraft/f16/physical/aero/aero_tables.hpp")
PROP_OUT = Path("aircraft/f16/hardware/propulsion/engine/engine_tables.hpp")


class SourceError(Exception):
    """The source is not the file this generator was written against."""


@dataclass(frozen=True)
class Breakpoints:
    bp_id: str
    units: str
    tokens: tuple[str, ...]
    line: int


@dataclass(frozen=True)
class Table:
    var_id: str
    gt_id: str
    function: str
    bp_ids: tuple[str, ...]
    tokens: tuple[str, ...]
    line: int


def resolve_main_checkout(start: Path) -> Path:
    """The main checkout's root, via `git rev-parse --git-common-dir` (generate_nesc_band.py's
    resolution): relative (".git") from the main checkout itself, absolute from a worktree."""
    result = subprocess.run(
        ["git", "-C", str(start), "rev-parse", "--git-common-dir"],
        capture_output=True,
        text=True,
        check=True,
    )
    common_dir = Path(result.stdout.strip())
    if not common_dir.is_absolute():
        common_dir = (start / common_dir).resolve()
    return common_dir.parent


def read_checked(path: Path) -> bytes:
    data = path.read_bytes()
    digest = hashlib.sha256(data).hexdigest()
    expected = SHA256[path.name]
    if digest != expected:
        raise SourceError(f"{path}: SHA-256 {digest}, expected {expected} (inventory section 1.1)")
    return data


def tokens_of(text: str | None, where: str) -> tuple[str, ...]:
    tokens = tuple(t for t in re.split(r"[\s,]+", text or "") if t)
    for token in tokens:
        if not DECIMAL.fullmatch(token):
            raise SourceError(f"{where}: {token!r} is not a decimal literal")
    return tokens


def line_of(lines: list[str], needle: str) -> int:
    """1-based line of the one source line containing needle."""
    hits = [i + 1 for i, line in enumerate(lines) if needle in line]
    if len(hits) != 1:
        raise SourceError(f"{needle!r} occurs on {len(hits)} lines, expected exactly one")
    return hits[0]


def parse(data: bytes, name: str) -> tuple[ET.Element, list[str]]:
    root = ET.fromstring(data)
    if root.tag != f"{NS}DAVEfunc":
        raise SourceError(f"{name}: root element {root.tag}, expected DAVEfunc")
    # split on "\n" only: str.splitlines() also splits on form feeds (docs/lessons.md, 2026-10-08).
    return root, data.decode("utf-8").split("\n")


def read_breakpoints(
    root: ET.Element, lines: list[str], name: str, expected: tuple[str, ...]
) -> dict[str, Breakpoints]:
    found: dict[str, Breakpoints] = {}
    for bp in root.iter(f"{NS}breakpointDef"):
        bp_id = bp.get("bpID", "")
        if bp_id in found:
            raise SourceError(f"{name}: breakpoint set {bp_id} defined twice")
        tokens = tokens_of(bp.findtext(f"{NS}bpVals"), f"{name} {bp_id}")
        values = [float(t) for t in tokens]
        if values != sorted(values) or len(set(values)) != len(values):
            raise SourceError(f"{name}: breakpoint set {bp_id} is not strictly increasing")
        found[bp_id] = Breakpoints(
            bp_id, bp.get("units", ""), tokens, line_of(lines, f'bpID="{bp_id}" units=')
        )
    if tuple(sorted(found)) != tuple(sorted(expected)):
        raise SourceError(f"{name}: breakpoint sets {sorted(found)}, expected {sorted(expected)}")
    return found


def read_tables(
    root: ET.Element,
    lines: list[str],
    name: str,
    breakpoints: dict[str, Breakpoints],
    expected: tuple[tuple[str, str], ...],
) -> list[Table]:
    gridded = {g.get("gtID", ""): g for g in root.iter(f"{NS}griddedTableDef")}
    if sorted(gridded) != sorted(gt for _, gt in expected):
        raise SourceError(f"{name}: tables {sorted(gridded)} are not the expected set")

    tables: dict[str, Table] = {}
    for function in root.iter(f"{NS}function"):
        dependent = function.find(f"{NS}dependentVarRef")
        defn = function.find(f"{NS}functionDefn")
        if dependent is None or defn is None:
            continue
        inline = defn.find(f"{NS}griddedTableDef")
        ref = defn.find(f"{NS}griddedTableRef")
        # F16_aero.dml defines each table inside its function; F16_prop.dml defines them first and
        # refers to them by gtID.
        holder = inline if inline is not None else ref
        gt_id = holder.get("gtID", "") if holder is not None else ""
        if gt_id not in gridded:
            raise SourceError(f"{name}: function {function.get('name')} names no known table")
        table = gridded[gt_id]
        bp_ids = tuple(b.get("bpID", "") for b in table.iter(f"{NS}bpRef"))
        independents = function.findall(f"{NS}independentVarRef")
        if len(independents) != len(bp_ids):
            raise SourceError(
                f"{name}: {gt_id} has {len(bp_ids)} breakpoint sets and "
                f"{len(independents)} independent variables"
            )
        for independent, bp_id in zip(independents, bp_ids, strict=True):
            ends = breakpoints[bp_id].tokens
            if independent.get("extrapolate") != "neither":
                raise SourceError(
                    f"{name}: {gt_id}'s {independent.get('varID')} is "
                    f"extrapolate={independent.get('extrapolate')!r}, not 'neither'"
                )
            if (float(independent.get("min", "nan")), float(independent.get("max", "nan"))) != (
                float(ends[0]),
                float(ends[-1]),
            ):
                raise SourceError(
                    f"{name}: {gt_id}'s {independent.get('varID')} min/max are not {bp_id}'s ends"
                )
        tokens = tokens_of(table.findtext(f"{NS}dataTable"), f"{name} {gt_id}")
        size = 1
        for bp_id in bp_ids:
            size *= len(breakpoints[bp_id].tokens)
        if len(tokens) != size:
            raise SourceError(f"{name}: {gt_id} has {len(tokens)} values, its breakpoints {size}")
        var_id = dependent.get("varID", "")
        if var_id in tables:
            raise SourceError(f"{name}: {var_id} is the output of two functions")
        tables[var_id] = Table(
            var_id,
            gt_id,
            function.get("name", ""),
            bp_ids,
            tokens,
            line_of(lines, f'gtID="{gt_id}">'),
        )

    ordered = []
    for var_id, gt_id in expected:
        if var_id not in tables or tables[var_id].gt_id != gt_id:
            raise SourceError(f"{name}: {var_id} is not computed by {gt_id}")
        ordered.append(tables[var_id])
    return ordered


def identifier(source_id: str) -> str:
    """k + CamelCase of a varID or bpID: cxt -> kCxt, absCl0 -> kAbsCl0, T_IDLE -> kTIdle,
    ALPHA1 -> kAlpha1, MACH_PTS -> kMachPts."""
    parts = [p for p in re.split(r"_", source_id) if p]
    camel = ""
    for part in parts:
        camel += part[0].upper() + (part[1:].lower() if part.isupper() else part[1:])
    return "k" + camel


def emit(
    source: str,
    digest: str,
    namespace: str,
    breakpoints: dict[str, Breakpoints],
    bp_order: tuple[str, ...],
    tables: list[Table],
    total: int,
) -> str:
    out = [
        f"// GENERATED by aircraft/f16/tools/generate_f16_tables.py from {source} -- do not edit.",
        "// Regenerate with that script; tests/python/test_f16_generated.py compares this file",
        "// with a fresh run byte for byte.",
        "//",
        f"// Source: docs/refs/Atmospheric_models/F16_package/{SOURCE_DIR}/{source}",
        "// (main checkout),",
        f"// SHA-256 {digest}",
        "// (docs/plans/m7-f16-inventory.md section 1.1).",
        "//",
        "// Each value is the source's own text, in the source's units. A table's first breakpoint",
        '// set is the outer index, as the file states ("last breakpoint changes most rapidly").',
        '// Every table is extrapolate="neither" in the file, read as clip (inventory Q15).',
        f"// {total} table values.",
        "#pragma once",
        "",
        "#include <array>",
        "",
        f"namespace astra::{namespace} {{",
        "",
        "// NOLINTBEGIN(readability-magic-numbers,modernize-use-std-numbers): every literal is a",
        f"// value transcribed by the generator from {source} (the header comment above names it",
        "// and its SHA-256); a value near a std::numbers constant is still the source's value.",
        "// clang-format off",
    ]
    for bp_id in bp_order:
        bp = breakpoints[bp_id]
        out.append(f"// {bp_id} ({source}:{bp.line}), {bp.units}.")
        out.append(
            f"inline constexpr std::array<double, {len(bp.tokens)}> {identifier(bp_id)}{{"
            + ", ".join(bp.tokens)
            + "};"
        )
    for table in tables:
        shape = " x ".join(table.bp_ids)
        out.append("")
        out.append(
            f'// {table.var_id}: {table.gt_id} (function "{table.function}", '
            f"{source}:{table.line}), {shape}."
        )
        if len(table.bp_ids) == 1:
            out.append(
                f"inline constexpr std::array<double, {len(table.tokens)}> "
                f"{identifier(table.var_id)}{{" + ", ".join(table.tokens) + "};"
            )
            continue
        if len(table.bp_ids) != 2:
            raise SourceError(f"{table.gt_id}: only 1-D and 2-D tables are expected")
        rows = len(breakpoints[table.bp_ids[0]].tokens)
        cols = len(breakpoints[table.bp_ids[1]].tokens)
        out.append(
            f"inline constexpr std::array<std::array<double, {cols}>, {rows}> "
            f"{identifier(table.var_id)}{{{{"
        )
        for r in range(rows):
            row = table.tokens[r * cols : (r + 1) * cols]
            out.append("    {" + ", ".join(row) + "},")
        out.append("}};")
    out += [
        "// clang-format on",
        "// NOLINTEND(readability-magic-numbers,modernize-use-std-numbers)",
        "",
        f"}}  // namespace astra::{namespace}",
        "",
    ]
    return "\n".join(out)


def generate(refs: Path) -> dict[Path, str]:
    source_dir = refs / SOURCE_DIR
    outputs: dict[Path, str] = {}

    aero_data = read_checked(source_dir / "F16_aero.dml")
    aero_root, aero_lines = parse(aero_data, "F16_aero.dml")
    aero_bps = read_breakpoints(aero_root, aero_lines, "F16_aero.dml", AERO_BREAKPOINTS)
    aero_tables = read_tables(aero_root, aero_lines, "F16_aero.dml", aero_bps, AERO_TABLES)
    aero_total = sum(len(t.tokens) for t in aero_tables)
    if aero_total != AERO_TOTAL:
        raise SourceError(f"F16_aero.dml: {aero_total} table values, expected {AERO_TOTAL}")
    outputs[AERO_OUT] = emit(
        "F16_aero.dml",
        SHA256["F16_aero.dml"],
        "aircraft_f16_aero_tables",
        aero_bps,
        AERO_BREAKPOINTS,
        aero_tables,
        aero_total,
    )

    prop_data = read_checked(source_dir / "F16_prop.dml")
    prop_root, prop_lines = parse(prop_data, "F16_prop.dml")
    prop_bps = read_breakpoints(prop_root, prop_lines, "F16_prop.dml", PROP_BREAKPOINTS)
    prop_tables = read_tables(prop_root, prop_lines, "F16_prop.dml", prop_bps, PROP_TABLES)
    prop_total = sum(len(t.tokens) for t in prop_tables)
    if prop_total != PROP_TOTAL:
        raise SourceError(f"F16_prop.dml: {prop_total} table values, expected {PROP_TOTAL}")
    outputs[PROP_OUT] = emit(
        "F16_prop.dml",
        SHA256["F16_prop.dml"],
        "aircraft_f16_engine_tables",
        prop_bps,
        PROP_BREAKPOINTS,
        prop_tables,
        prop_total,
    )
    return outputs


def main() -> int:
    parser = argparse.ArgumentParser(description="Emit the F-16's DAVE-ML tables as committed C++.")
    parser.add_argument("--refs", type=Path, default=None, help="the F16_package directory")
    parser.add_argument("--out", type=Path, default=None, help="write both headers flat here")
    args = parser.parse_args()

    script_dir = Path(__file__).resolve().parent
    repo_root = script_dir.parents[2]
    if args.refs is not None:
        refs = args.refs
    else:
        refs = (
            resolve_main_checkout(script_dir)
            / "docs"
            / "refs"
            / "Atmospheric_models"
            / "F16_package"
        )
    if not (refs / SOURCE_DIR).is_dir():
        print(
            f"{refs / SOURCE_DIR} does not exist -- gitignored refs are not present in a worktree "
            "(docs/refs/README.md); pass --refs pointing at the main checkout's F16_package",
            file=sys.stderr,
        )
        return 2
    try:
        outputs = generate(refs)
    except SourceError as error:
        print(f"generate_f16_tables: {error}", file=sys.stderr)
        return 1

    for relative, text in outputs.items():
        target = args.out / relative.name if args.out is not None else repo_root / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text(text, encoding="utf-8", newline="\n")
        print(f"wrote {target}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
