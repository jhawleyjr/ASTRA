"""The bound core/util/units conversions (M6 PR 4, docs/plans/m6-pr4-units.md; R21, C-15; ADR-0002).

R21's "the bound functions equal the C++ ones". The C++ contract (units.hpp's pin) is one correctly
rounded multiply (to SI) or divide (from SI) by one named constant. This test computes that
independently, in exact rational arithmetic rounded once, with each constant derived here from its
definition: a 50-digit decimal pi over 180, 3048/10000, and 1852/3600. It then asserts each derived
constant equals the hex literal units_test.cpp pins on the C++ side. The oracle is never numpy
(np.rad2deg multiplies by 180/pi and rounds differently for about 12% of inputs), never math.pi,
and never the binding.
"""

from __future__ import annotations

import math
import random
import struct
import warnings
from collections.abc import Callable
from decimal import Decimal
from fractions import Fraction
from typing import Any

import numpy as np
import pytest

from astra import _core

PI = Fraction(Decimal("3.14159265358979323846264338327950288419716939937510"))

# Each constant rounded once to double from its definition; checked against units_test.cpp's pins.
RADIANS_PER_DEGREE = float(PI / 180)
METERS_PER_FOOT = float(Fraction(3048, 10000))
METERS_PER_SECOND_PER_KNOT = float(Fraction(1852, 3600))

# (name, constant, True when the function multiplies (to SI), False when it divides (from SI)).
CONVERSIONS: tuple[tuple[str, float, bool], ...] = (
    ("feet_to_meters", METERS_PER_FOOT, True),
    ("meters_to_feet", METERS_PER_FOOT, False),
    ("knots_to_meters_per_second", METERS_PER_SECOND_PER_KNOT, True),
    ("meters_per_second_to_knots", METERS_PER_SECOND_PER_KNOT, False),
    ("degrees_to_radians", RADIANS_PER_DEGREE, True),
    ("radians_to_degrees", RADIANS_PER_DEGREE, False),
)
NAMES = [name for name, _, _ in CONVERSIONS]

# The smallest magnitude that rounds to infinity: 2**1024 - 2**970, halfway past the largest double.
_OVERFLOW = Fraction(2**1024 - 2**970)


def _bits(x: float) -> int:
    return int(struct.unpack("<Q", struct.pack("<d", x))[0])


def _oracle(x: float, constant: float, multiply: bool) -> float:
    """x times or over the constant, exact, rounded once to double (round half to even)."""
    if math.isnan(x):
        return x
    if math.isinf(x):
        return x
    exact = Fraction(x) * Fraction(constant) if multiply else Fraction(x) / Fraction(constant)
    if abs(exact) >= _OVERFLOW:
        return math.copysign(math.inf, x)
    # The constants are positive, so the result has x's sign, which Fraction loses for -0.0 and
    # for an underflow to zero.
    return math.copysign(float(exact), x)


def _convert(name: str) -> Callable[[Any], Any]:
    function: Callable[[Any], Any] = getattr(_core, name)
    return function


def _sweep() -> list[float]:
    rng = random.Random(20260930)
    values = [
        rng.choice((-1.0, 1.0)) * rng.uniform(1.0, 2.0) * 2.0 ** rng.randint(-1074, 1023)
        for _ in range(6000)
    ]
    values += [rng.uniform(-1e4, 1e4) for _ in range(6000)]
    values += [float(d) for d in range(-720, 721)]
    return values


SWEEP = _sweep()
SPECIALS = (
    0.0,
    -0.0,
    math.inf,
    -math.inf,
    math.nan,
    5e-324,
    -5e-324,
    2.2250738585072014e-308,
    1.7976931348623157e308,
    -1e307,
)


def test_the_constants_derived_here_are_the_ones_units_test_cpp_pins() -> None:
    assert float.fromhex("0x1.1df46a2529d39p-6") == RADIANS_PER_DEGREE
    assert METERS_PER_FOOT == 0.3048
    assert METERS_PER_SECOND_PER_KNOT == 1852.0 / 3600.0
    assert float(PI) == math.pi  # the correctly rounded pi, as std::numbers::pi is


@pytest.mark.parametrize(("name", "constant", "multiply"), CONVERSIONS)
def test_each_bound_function_is_one_rounding_by_its_constant(
    name: str, constant: float, multiply: bool
) -> None:
    convert = _convert(name)
    for x in (*SWEEP, *SPECIALS):
        got = convert(x)
        want = _oracle(x, constant, multiply)
        assert isinstance(got, float)
        if math.isnan(want):
            assert math.isnan(got), (name, x)
        else:
            assert _bits(got) == _bits(want), (name, x.hex(), got.hex(), want.hex())


def test_known_values_match_the_cpp_suite() -> None:
    pi = math.pi
    assert _core.degrees_to_radians(180.0) == pi
    assert _core.degrees_to_radians(-180.0) == -pi
    assert _core.degrees_to_radians(90.0) == pi / 2.0
    assert _core.degrees_to_radians(45.0) == pi / 4.0
    assert _core.degrees_to_radians(360.0) == 2.0 * pi
    assert _core.degrees_to_radians(3.0) == float.fromhex("0x1.acee9f37bebd6p-5")
    assert _core.radians_to_degrees(pi) == 180.0
    assert _core.radians_to_degrees(pi / 2.0) == 90.0
    assert _core.radians_to_degrees(2.0 * pi) == 360.0
    assert _core.radians_to_degrees(1.0) == float.fromhex("0x1.ca5dc1a63c1f8p+5")
    assert _core.radians_to_degrees(1.125) == float.fromhex("0x1.01d4bced81d1bp+6")
    assert _core.radians_to_degrees(RADIANS_PER_DEGREE) == 1.0
    # Near only: one ulp short of 60 and 30.
    assert _core.radians_to_degrees(pi / 3.0) == math.nextafter(60.0, 0.0)
    assert _core.radians_to_degrees(pi / 6.0) == math.nextafter(30.0, 0.0)
    assert _core.feet_to_meters(10000.0) == 3048.0
    assert _core.meters_to_feet(0.3048) == 1.0
    assert _core.meters_per_second_to_knots(3.0) == float.fromhex("0x1.7537d830a8161p+2")


@pytest.mark.parametrize("name", NAMES)
def test_numeric_scalars_go_through_the_float_protocol(name: str) -> None:
    convert = _convert(name)
    for x in (
        180,
        -3,
        True,
        np.int64(7),
        np.float32(0.1),
        np.float64(2.5),
        np.array(1.25),
        np.array(4),
    ):
        got = convert(x)
        assert type(got) is float, (name, x)
        assert _bits(got) == _bits(convert(float(x))), (name, x)


def _arrays() -> dict[str, np.ndarray[Any, np.dtype[np.float64]]]:
    payload_nan = np.array([0x7FF8000000000123, 0xFFF8000000000456], dtype=np.uint64).view(
        np.float64
    )
    base = np.concatenate([np.array([*SWEEP[:500], *SPECIALS]), payload_nan])
    matrix = np.arange(60.0).reshape(20, 3) * 1.37
    read_only = base.copy()
    read_only.setflags(write=False)
    return {
        "contiguous": base,
        "strided": base[::3],
        "reversed": base[::-1],
        "column": matrix[:, 1],
        "read_only": read_only,
        "empty": np.array([], dtype=np.float64),
    }


@pytest.mark.parametrize("name", NAMES)
@pytest.mark.parametrize(
    "layout", ["contiguous", "strided", "reversed", "column", "read_only", "empty"]
)
def test_the_array_form_is_the_float_form_element_by_element(name: str, layout: str) -> None:
    convert = _convert(name)
    values = _arrays()[layout]
    before = values.copy()
    got = convert(values)
    assert isinstance(got, np.ndarray)
    assert got.dtype == np.float64
    assert got.shape == values.shape
    assert got.flags.c_contiguous
    assert not np.shares_memory(got, values)
    want = np.array([convert(float(v)) for v in values], dtype=np.float64)
    assert np.array_equal(got.view(np.uint64), want.view(np.uint64)), (name, layout)
    assert np.array_equal(values.view(np.uint64), before.view(np.uint64))  # the input is untouched


PACKED = np.dtype([("tag", "u1"), ("angle", "f8")])  # a float64 field at a 9-byte stride


@pytest.mark.parametrize("name", NAMES)
@pytest.mark.parametrize(
    "refused",
    [
        np.array([1, 2], dtype=np.int64),
        # Size 1: the float overload's float() fallback must not take any of these (R-001).
        np.array([3], dtype=np.int64),
        np.array([True]),
        np.array([1.5], dtype=np.float32),
        np.array([1.5], dtype=">f8"),
        np.ones((1, 1)),
        np.array([True, False]),
        np.array([1.0, 2.0], dtype=np.float32),
        np.array([1.0, 2.0], dtype=">f8"),  # big-endian: refused, never byte-swapped
        np.ones((2, 2)),
        [1.0, 2.0],
        # A stride that is not a whole number of elements, forward and reversed (R-002, D7).
        np.array([(1, 1.125), (2, -3.0)], dtype=PACKED)["angle"],
        np.array([(1, 1.125), (2, -3.0)], dtype=PACKED)["angle"][::-1],
    ],
    ids=[
        "int64",
        "int64_size_1",
        "bool_size_1",
        "float32_size_1",
        "big_endian_size_1",
        "two_d_1x1",
        "bool",
        "float32",
        "big_endian_float64",
        "two_d",
        "list",
        "packed_field_stride_9",
        "packed_field_stride_minus_9",
    ],
)
def test_other_arrays_are_refused(name: str, refused: object) -> None:
    # DeprecationWarning ignored: numpy before 2.4 (pyproject allows numpy>=2) converts a size-1
    # array to a float with only that warning, so the refusal must not depend on it being an error.
    result: object = None
    with warnings.catch_warnings():
        warnings.simplefilter("ignore", DeprecationWarning)
        with pytest.raises(TypeError):
            result = _convert(name)(refused)
    assert result is None


class _ArrayShaped:
    """What numpy < 2.4 presents to the float overload for a size-1 array: an ndim, and a
    __float__ that succeeds. Independent of the installed numpy's version (R-001)."""

    def __init__(self, ndim: object) -> None:
        self.ndim = ndim

    def __float__(self) -> float:
        return 1.0


@pytest.mark.parametrize("name", NAMES)
@pytest.mark.parametrize("ndim", [1, 2, "1", 1.0])
def test_an_object_with_a_nonzero_ndim_never_reaches_the_float_overload(
    name: str, ndim: object
) -> None:
    with pytest.raises(TypeError):
        _convert(name)(_ArrayShaped(ndim))


@pytest.mark.parametrize("name", NAMES)
def test_an_object_with_ndim_zero_converts_like_a_float(name: str) -> None:
    convert = _convert(name)
    assert _bits(convert(_ArrayShaped(0))) == _bits(convert(1.0))


@pytest.mark.parametrize("name", NAMES)
def test_a_size_one_packed_field_is_accepted_because_its_stride_is_never_used(name: str) -> None:
    field = np.array([(1, 1.125)], dtype=PACKED)["angle"]
    convert = _convert(name)
    assert _bits(float(convert(field)[0])) == _bits(convert(1.125))


def test_the_constants_are_not_bound() -> None:
    constants = {RADIANS_PER_DEGREE, METERS_PER_FOOT, METERS_PER_SECOND_PER_KNOT}
    bound = [name for name in dir(_core) if isinstance(getattr(_core, name), float)]
    assert not [name for name in bound if getattr(_core, name) in constants]
