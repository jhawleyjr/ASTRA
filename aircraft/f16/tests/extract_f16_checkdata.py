"""Extract P1's checkData and mass constants as the F-16 tests' fixture (M7 PR 6b, R1 to R3).

Writes aircraft/f16/tests/f16_checkdata.hpp from NASA's F-16 DAVE-ML package
(docs/plans/m7-f16-inventory.md):
- F16_aero.dml's 16 staticShots: each shot's 9 checkInputs, every internalValues entry, and its 9
  checkOutputs with each output's own tol (inventory section 2);
- F16_prop.dml's 9 staticShots, the same way, internalValues where a shot has them (section 3);
- every F16_inertia.dml variableDef that carries an initialValue: the mass, the inertias and
  products, CBAR, CG_PCT_MAC and the CM offsets' constants (section 4).

This is the oracle side of the comparison. It shares no code with aircraft/f16/tools/
generate_f16_tables.py, which emits the tables the plugins interpolate (docs/lessons.md,
2026-09-25): it parses with xml.dom.minidom rather than ElementTree, finds shot lines by counting
`<staticShot` lines rather than searching for ids, and checks the files' SHA-256 itself. Each value
is the file's own text, checked to be a decimal literal, never reformatted through a Python float.

Run from a worktree; gitignored refs exist only in the main checkout (docs/refs/README.md):
    python3 -I -B aircraft/f16/tests/extract_f16_checkdata.py [--refs PATH] [--out DIR]
--refs defaults to the main checkout's docs/refs/Atmospheric_models/F16_package (found through
`git rev-parse --git-common-dir`). --out writes f16_checkdata.hpp into DIR instead of beside this
script (tests/python/test_f16_generated.py).
"""

from __future__ import annotations

import argparse
import hashlib
import re
import subprocess
import sys
from pathlib import Path
from xml.dom import minidom

# docs/plans/m7-f16-inventory.md section 1.1, transcribed here independently of the generator.
EXPECTED_SHA256 = {
    "F16_aero.dml": "82796577b989b5f3becb7d13430ff6c619b3ecd2e642a6427b743658fb177e51",
    "F16_prop.dml": "e5fd7772e5c05cdac666c94febbe4cb50139418b26045affd92dbd198f2215c6",
    "F16_inertia.dml": "e11d4eba5b158d24db7034bd85682ad0b896dd2f3172f007ca3d6d6d4e693c4a",
}
AERO_SHOTS, AERO_INPUTS, AERO_OUTPUTS = 16, 9, 9
PROP_SHOTS, PROP_INPUTS, PROP_OUTPUTS = 9, 3, 6
OUTPUT_NAME = "f16_checkdata.hpp"

NUMBER = re.compile(r"^[+-]?(?:[0-9]+\.?[0-9]*|\.[0-9]+)(?:[eE][+-]?[0-9]+)?$")


class FixtureError(Exception):
    """The source does not have the shape this reader expects."""


def main_checkout_root(here: Path) -> Path:
    common = subprocess.run(
        ["git", "-C", str(here), "rev-parse", "--git-common-dir"],
        capture_output=True,
        text=True,
        check=True,
    ).stdout.strip()
    common_path = Path(common)
    if not common_path.is_absolute():
        common_path = (here / common_path).resolve()
    return common_path.parent


def load(path: Path) -> tuple[minidom.Document, list[int]]:
    """The parsed document and the 1-based line of each `<staticShot` in file order."""
    raw = path.read_bytes()
    got = hashlib.sha256(raw).hexdigest()
    if got != EXPECTED_SHA256[path.name]:
        raise FixtureError(f"{path.name}: SHA-256 is {got}, not {EXPECTED_SHA256[path.name]}")
    shot_lines = [
        number
        for number, text in enumerate(raw.decode("utf-8").split("\n"), start=1)
        if "<staticShot" in text
    ]
    return minidom.parseString(raw), shot_lines


def text_of(node: minidom.Element, tag: str) -> str:
    children = [c for c in node.childNodes if isinstance(c, minidom.Element) and c.tagName == tag]
    if len(children) != 1:
        raise FixtureError(f"<{node.tagName}> has {len(children)} <{tag}> children, expected one")
    return "".join(t.data for t in children[0].childNodes if isinstance(t, minidom.Text)).strip()


def number(text: str, where: str) -> str:
    if not NUMBER.match(text):
        raise FixtureError(f"{where}: {text!r} is not a decimal literal")
    return text


def cxx_string(text: str) -> str:
    return '"' + text.replace("\\", "\\\\").replace('"', '\\"') + '"'


def child_elements(node: minidom.Element, tag: str) -> list[minidom.Element]:
    return [c for c in node.childNodes if isinstance(c, minidom.Element) and c.tagName == tag]


def one_child(node: minidom.Element, tag: str) -> minidom.Element | None:
    found = child_elements(node, tag)
    if len(found) > 1:
        raise FixtureError(f"<{node.tagName}> has {len(found)} <{tag}> children")
    return found[0] if found else None


def read_shots(
    path: Path, expected_shots: int, expected_inputs: int, expected_outputs: int
) -> list[dict[str, object]]:
    document, lines = load(path)
    shots = document.getElementsByTagName("staticShot")
    if len(shots) != expected_shots or len(lines) != expected_shots:
        raise FixtureError(f"{path.name}: {len(shots)} shots, expected {expected_shots}")
    result: list[dict[str, object]] = []
    for shot, line in zip(shots, lines, strict=True):
        name = shot.getAttribute("name")
        where = f"{path.name} shot {name!r}"

        inputs_node = one_child(shot, "checkInputs")
        outputs_node = one_child(shot, "checkOutputs")
        if inputs_node is None or outputs_node is None:
            raise FixtureError(f"{where}: no checkInputs or checkOutputs")
        inputs = [
            (
                text_of(s, "signalName"),
                text_of(s, "signalUnits"),
                number(text_of(s, "signalValue"), where),
            )
            for s in child_elements(inputs_node, "signal")
        ]
        outputs = [
            (
                text_of(s, "signalName"),
                text_of(s, "signalUnits"),
                number(text_of(s, "signalValue"), where),
                number(text_of(s, "tol"), where),
            )
            for s in child_elements(outputs_node, "signal")
        ]
        if len(inputs) != expected_inputs or len(outputs) != expected_outputs:
            raise FixtureError(f"{where}: {len(inputs)} inputs and {len(outputs)} outputs")

        internal_node = one_child(shot, "internalValues")
        internal = (
            []
            if internal_node is None
            else [
                (text_of(s, "varID"), number(text_of(s, "signalValue"), where))
                for s in child_elements(internal_node, "signal")
            ]
        )
        result.append(
            {
                "name": name,
                "line": line,
                "inputs": inputs,
                "internal": internal,
                "outputs": outputs,
            }
        )
    return result


def read_constants(path: Path) -> list[tuple[str, str, str, int]]:
    document, _ = load(path)
    lines = path.read_bytes().decode("utf-8").split("\n")
    constants: list[tuple[str, str, str, int]] = []
    for variable in document.getElementsByTagName("variableDef"):
        if not variable.hasAttribute("initialValue"):
            continue
        var_id = variable.getAttribute("varID")
        value = number(variable.getAttribute("initialValue"), f"{path.name} {var_id}")
        hits = [n for n, text in enumerate(lines, start=1) if f'varID="{var_id}"' in text]
        if len(hits) != 1:
            raise FixtureError(f"{path.name}: varID {var_id} on {len(hits)} lines")
        constants.append((var_id, variable.getAttribute("units"), value, hits[0]))
    return constants


def render_arrays(prefix: str, source: str, shots: list[dict[str, object]]) -> list[str]:
    """Each shot's inputs, internal values and outputs, as arrays in namespace detail."""
    out: list[str] = []
    for index, shot in enumerate(shots):
        tag = f"k{prefix}{index:02d}"
        inputs = shot["inputs"]
        internal = shot["internal"]
        outputs = shot["outputs"]
        assert isinstance(inputs, list)
        assert isinstance(internal, list)
        assert isinstance(outputs, list)
        out.append(f"// {source}:{shot['line']}, {shot['name']!s}.")
        out.append(f"inline constexpr std::array<Signal, {len(inputs)}> {tag}Inputs{{{{")
        out += [f"    {{{cxx_string(n)}, {cxx_string(u)}, {v}}}," for n, u, v in inputs]
        out.append("}};")
        if internal:
            out.append(f"inline constexpr std::array<Internal, {len(internal)}> {tag}Internal{{{{")
            out += [f"    {{{cxx_string(n)}, {v}}}," for n, v in internal]
            out.append("}};")
        out.append(f"inline constexpr std::array<Output, {len(outputs)}> {tag}Outputs{{{{")
        out += [f"    {{{cxx_string(n)}, {cxx_string(u)}, {v}, {t}}}," for n, u, v, t in outputs]
        out.append("}};")
        out.append("")
    return out


def render_table(prefix: str, source: str, shots: list[dict[str, object]]) -> list[str]:
    """The shot table that names the detail arrays, in file order."""
    out = [
        f"// {source}'s checkData, in file order.",
        f"inline constexpr std::array<Shot, {len(shots)}> k{prefix}Shots{{{{",
    ]
    for index, shot in enumerate(shots):
        tag = f"detail::k{prefix}{index:02d}"
        internal_ref = f"{tag}Internal" if shot["internal"] else "std::span<const Internal>{}"
        out.append(
            f"    {{{cxx_string(str(shot['name']))}, {shot['line']}, {tag}Inputs, {internal_ref}, "
            f"{tag}Outputs}},"
        )
    out += ["}};", ""]
    return out


def render(
    aero: list[dict[str, object]],
    prop: list[dict[str, object]],
    inertia: list[tuple[str, str, str, int]],
) -> str:
    out = [
        "// GENERATED by aircraft/f16/tests/extract_f16_checkdata.py -- do not edit. Regenerate",
        "// with that script; tests/python/test_f16_generated.py compares this file with a fresh",
        "// run byte for byte.",
        "//",
        "// P1's own checks, the oracle for R1 to R3: docs/refs/Atmospheric_models/F16_package/",
        "// F16_S119_source/ (main checkout), SHA-256 per docs/plans/m7-f16-inventory.md 1.1:",
    ]
    out += [f"//   {name} {digest}" for name, digest in EXPECTED_SHA256.items()]
    out += [
        "// Every value is the file's own text, in the file's units (ft, ft/s, deg, rad/s, lbf,",
        "// slug).",
        "// A shot passes when |y - value| < tol for each output (F16_aero_verify.m:686).",
        "#pragma once",
        "",
        "#include <array>",
        "#include <span>",
        "#include <string_view>",
        "",
        "namespace astra::aircraft_f16_checkdata {",
        "",
        "struct Signal {",
        "  std::string_view name;",
        "  std::string_view units;",
        "  double value;",
        "};",
        "",
        "struct Internal {",
        "  std::string_view var_id;",
        "  double value;",
        "};",
        "",
        "struct Output {",
        "  std::string_view name;",
        "  std::string_view units;",
        "  double value;",
        "  double tol;",
        "};",
        "",
        "struct Shot {",
        "  std::string_view name;",
        "  int line;  // the shot's line in its source file",
        "  std::span<const Signal> inputs;",
        "  std::span<const Internal> internal;  // empty when the shot lists none",
        "  std::span<const Output> outputs;",
        "};",
        "",
        "struct Constant {",
        "  std::string_view var_id;",
        "  std::string_view units;",
        "  double value;",
        "  int line;",
        "};",
        "",
        "// NOLINTBEGIN(readability-magic-numbers): every literal is a value read from P1 by the",
        "// extractor; the header comment above names the files and their SHA-256.",
        "// clang-format off",
        "namespace detail {",
        "",
    ]
    out += render_arrays("Aero", "F16_aero.dml", aero)
    out += render_arrays("Prop", "F16_prop.dml", prop)
    out += ["}  // namespace detail", ""]
    out += render_table("Aero", "F16_aero.dml", aero)
    out += render_table("Prop", "F16_prop.dml", prop)
    out.append(
        "// F16_inertia.dml's constants (every variableDef with an initialValue), file order."
    )
    out.append(f"inline constexpr std::array<Constant, {len(inertia)}> kInertiaConstants{{{{")
    out += [
        f"    {{{cxx_string(v)}, {cxx_string(u)}, {value}, {line}}},"
        for v, u, value, line in inertia
    ]
    out += [
        "}};",
        "// clang-format on",
        "// NOLINTEND(readability-magic-numbers)",
        "",
        "}  // namespace astra::aircraft_f16_checkdata",
        "",
    ]
    return "\n".join(out)


def main() -> int:
    parser = argparse.ArgumentParser(description="Extract P1's checkData as the F-16 fixture.")
    parser.add_argument("--refs", type=Path, default=None, help="the F16_package directory")
    parser.add_argument("--out", type=Path, default=None, help="write f16_checkdata.hpp here")
    args = parser.parse_args()

    here = Path(__file__).resolve().parent
    package = (
        args.refs
        if args.refs is not None
        else main_checkout_root(here) / "docs" / "refs" / "Atmospheric_models" / "F16_package"
    )
    source = package / "F16_S119_source"
    if not source.is_dir():
        print(
            f"{source} does not exist -- gitignored refs are not present in a worktree "
            "(docs/refs/README.md); pass --refs pointing at the main checkout's F16_package",
            file=sys.stderr,
        )
        return 2
    try:
        aero = read_shots(source / "F16_aero.dml", AERO_SHOTS, AERO_INPUTS, AERO_OUTPUTS)
        prop = read_shots(source / "F16_prop.dml", PROP_SHOTS, PROP_INPUTS, PROP_OUTPUTS)
        inertia = read_constants(source / "F16_inertia.dml")
    except FixtureError as error:
        print(f"extract_f16_checkdata: {error}", file=sys.stderr)
        return 1

    target = (args.out if args.out is not None else here) / OUTPUT_NAME
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_text(render(aero, prop, inertia), encoding="utf-8", newline="\n")
    print(f"wrote {target}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
