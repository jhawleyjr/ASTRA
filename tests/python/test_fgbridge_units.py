"""astra.fgbridge on the bound core/util/units conversions (M6 PR 4; R21, D3, D4).

Plan: docs/plans/m6-pr4-units.md.

test_fgbridge.py, the packet tests, is unchanged by this PR. This file adds three checks:
- fgbridge holds no conversion constant (a scan of its source);
- its converted fields go through astra._core (dispatch);
- its packets are what the pre-PR arithmetic gave. A reference packer, independent of fgbridge,
  packs each field on its own with this test's own constants: x / 0.3048 and
  x / (1852.0 / 3600.0), one Python float division each, as fgbridge did before this PR. Outcomes
  are compared, not only bytes: for every input, both give the same bytes or both raise the same
  exception type. struct raises OverflowError packing a finite value beyond float32's range, and
  pack() does not catch it.
"""

from __future__ import annotations

import ast
import importlib.util
import math
import random
import struct
from pathlib import Path
from types import ModuleType
from typing import Any

import pytest

from astra import _core, fgbridge

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "python/astra/fgbridge.py"
TABLE_PATH = ROOT / "tests/golden/flightgear/fgnetfdm_v24.py"

# This test's own constants: 1 ft = 0.3048 m, 1 kt = 1852 m per 3600 s, both exact by definition.
FOOT = 0.3048
KNOT = 1852.0 / 3600.0


def _load_table() -> ModuleType:
    spec = importlib.util.spec_from_file_location("fgnetfdm_v24", TABLE_PATH)
    assert spec is not None
    assert spec.loader is not None
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


TABLE: Any = _load_table()
_CODES = {"uint32_t": ">I", "int32_t": ">i", "float": ">f", "double": ">d"}
_LAYOUT: dict[str, tuple[str, int]] = {f.name: (_CODES[f.ctype], f.offset) for f in TABLE.FIELDS}
_WIDTH = {">I": 4, ">i": 4, ">f": 4, ">d": 8}

# Packet field -> (derived key, divisor or None). Only the fields fgbridge fills from its source;
# every other field is zero, apart from version.
SOURCED: dict[str, tuple[str, float | None]] = {
    "longitude": ("lon", None),
    "latitude": ("lat", None),
    "altitude": ("altitude", None),
    "agl": ("altitude", None),
    "phi": ("phi", None),
    "theta": ("theta", None),
    "psi": ("psi", None),
    "alpha": ("alpha", None),
    "beta": ("beta", None),
    "vcas": ("cas", KNOT),
    "climb_rate": ("vertical_speed", FOOT),
    "v_north": ("v_n_0", FOOT),
    "v_east": ("v_n_1", FOOT),
    "v_down": ("v_n_2", FOOT),
    "v_body_u": ("u", FOOT),
    "v_body_v": ("v", FOOT),
    "v_body_w": ("w", FOOT),
    "A_X_pilot": ("ax", FOOT),
    "A_Y_pilot": ("ay", FOOT),
    "A_Z_pilot": ("az", FOOT),
}
ACCELS = ("A_X_pilot", "A_Y_pilot", "A_Z_pilot")
FEET_FIELDS = [name for name, (_, divisor) in SOURCED.items() if divisor is FOOT]


def _reference(d: dict[str, float]) -> dict[str, bytes]:
    """Each sourced field's bytes, packed on its own at the golden table's type."""
    accel_invalid = int(d["status_flags"]) & fgbridge.STATUS_ACCEL_INVALID
    out: dict[str, bytes] = {}
    for name, (key, divisor) in SOURCED.items():
        value = d[key] if divisor is None else d[key] / divisor
        if accel_invalid and name in ACCELS:
            value = 0.0
        out[name] = struct.pack(_LAYOUT[name][0], value)
    return out


def _outcome(function: Any, *args: Any) -> tuple[str, Any]:
    try:
        return ("value", function(*args))
    except Exception as error:
        return ("raised", type(error))


def _check(d: dict[str, float]) -> None:
    got_kind, got = _outcome(fgbridge.pack, d)
    want_kind, want = _outcome(_reference, d)
    assert got_kind == want_kind, (d, got, want)
    if got_kind == "raised":
        assert got is want, (d, got, want)  # the same exception type
        return
    assert len(got) == TABLE.SIZE
    mismatched = [
        name
        for name, (code, offset) in _LAYOUT.items()
        if name in want and got[offset : offset + _WIDTH[code]] != want[name]
    ]
    assert not mismatched, (mismatched, d)
    code, offset = _LAYOUT["version"]
    assert got[offset : offset + 4] == struct.pack(code, fgbridge.FG_NET_FDM_VERSION)


def _derived(rng: random.Random, scale: float) -> dict[str, float]:
    d = {field: rng.uniform(-scale, scale) for field in fgbridge.FIELDS}
    d["status_flags"] = float(rng.choice((0, 1)))
    return d


def test_the_packets_equal_the_reference_packers_for_seeded_frames() -> None:
    rng = random.Random(20260930)
    for scale in (1.0, 300.0, 1e5, 1e30):
        for _ in range(500):
            _check(_derived(rng, scale))


SPECIALS = (
    0.0,
    -0.0,
    math.nan,
    math.inf,
    -math.inf,
    5e-324,
    -2.2250738585072014e-308,
    1e-40,  # a float32 subnormal after the conversion
    # In feet, float32's largest finite value: packs as 7f7fffff.
    3.4028234663852886e38 * FOOT,
    # In feet, exactly float32's round-to-infinity threshold (2**128 - 2**103): OverflowError. The
    # one input here where a precomputed reciprocal (x * (1 / 0.3048)) differs after the float32
    # pack: it lands one ulp lower and packs as 7f7fffff.
    3.4028235677973366e38 * FOOT,
    3.5e38,  # overflows float32 once converted: OverflowError
    1e300,
)


@pytest.mark.parametrize("special", SPECIALS, ids=[repr(s) for s in SPECIALS])
@pytest.mark.parametrize("key", [*sorted({key for key, _ in SOURCED.values()})])
@pytest.mark.parametrize("flags", [0.0, 1.0])
def test_special_values_give_the_same_outcome(special: float, key: str, flags: float) -> None:
    d: dict[str, float] = dict.fromkeys(fgbridge.FIELDS, 1.5)
    d["status_flags"] = flags
    d[key] = special
    _check(d)


def test_an_overflowing_field_raises_in_both() -> None:
    d: dict[str, float] = dict.fromkeys(fgbridge.FIELDS, 1.5)
    d["status_flags"] = 0.0
    d["u"] = 3.5e38
    with pytest.raises(OverflowError):
        fgbridge.pack(d)
    with pytest.raises(OverflowError):
        _reference(d)


def test_converted_fields_go_through_the_bound_functions(monkeypatch: pytest.MonkeyPatch) -> None:
    d: dict[str, float] = dict.fromkeys(fgbridge.FIELDS, 2.0)
    d["status_flags"] = 0.0
    before = fgbridge.pack(d)
    monkeypatch.setattr(_core, "meters_to_feet", lambda _: 7.0)
    monkeypatch.setattr(_core, "meters_per_second_to_knots", lambda _: 9.0)
    after = fgbridge.pack(d)
    changed = []
    for name, (code, offset) in _LAYOUT.items():
        width = _WIDTH[code]
        if before[offset : offset + width] != after[offset : offset + width]:
            changed.append(name)
            (value,) = struct.unpack(code, after[offset : offset + width])
            assert value == (9.0 if name == "vcas" else 7.0), name
    assert sorted(changed) == sorted([*FEET_FIELDS, "vcas"])
    assert len(FEET_FIELDS) == 10


def _source_tree() -> ast.Module:
    return ast.parse(SOURCE.read_text(encoding="utf-8"))


def test_fgbridge_holds_no_conversion_constant() -> None:
    tree = _source_tree()
    floats = [n for n in ast.walk(tree) if isinstance(n, ast.Constant) and type(n.value) is float]
    assert [n.value for n in floats if n.value != 0.0] == []
    for node in ast.walk(tree):
        if isinstance(node, ast.BinOp) and isinstance(node.op, ast.Mult | ast.Div | ast.FloorDiv):
            operands = (node.left, node.right)
            numeric = [
                o for o in operands if isinstance(o, ast.Constant) and type(o.value) in (int, float)
            ]
            # The zero tail's tuple repetition, (0.0,) * 36, is the only literal multiplier allowed.
            repeats_a_tuple = isinstance(node.op, ast.Mult) and any(
                isinstance(o, ast.Tuple) for o in operands
            )
            assert not numeric or repeats_a_tuple, ast.unparse(node)
        if isinstance(node, ast.Attribute | ast.Name):
            name = node.attr if isinstance(node, ast.Attribute) else node.id
            assert name not in {"degrees", "radians", "deg2rad", "rad2deg", "FOOT", "KNOT"}, name
    assert not hasattr(fgbridge, "FOOT")
    assert not hasattr(fgbridge, "KNOT")
