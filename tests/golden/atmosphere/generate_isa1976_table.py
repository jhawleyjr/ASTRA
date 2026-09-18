"""Independent generator for tests/golden/atmosphere/isa1976_table.hpp.

Human-owned, like the header it writes (tests/golden/**, guard-oracle.sh): re-run after any change
to the constants below, never hand-edited output. Deliberately uses no ASTRA code -- this is a
second, from-scratch implementation of the equations plugins/physical/atmosphere/isa1976/isa1976.cpp
also implements, in a different language, so a wrong exponent or a flipped lapse-rate sign in the
plugin fails this generator's own kComputedRows cross-check in the C++ test, not just a comparison
against itself.

Source: U.S. Standard Atmosphere, 1976 (NOAA-S/T 76-1562 / NASA-TM-X-74335, NTRS 19770009539),
Washington, D.C., October 1976. All constants below were read directly off that document (not from
memory, not from this script's own output) and cross-checked before being trusted -- see
check_transcribed_base_pressures() and BASE_PRESSURE_CROSS_CHECK_TOLERANCE's comment, and, for the
same idea applied to every transcribed Table I row's pressure/density (not just the seven
layer-boundary base pressures), check_transcribed_row_tolerances() and
transcribed_pressure_tolerance_pa()'s comment.

Altitude convention: the document tabulates BOTH geopotential H and geometric Z, on facing pairs of
pages ("Table I", one titled "Geopotential Altitude", the other "Geometric Altitude", both metric),
because the two differ by a few tens of metres at these altitudes (H = r0*Z/(r0+Z), eq 18) and
conflating them is the single easiest transcription error here (the same class of mistake as
confusing MSL and ellipsoid height). Every altitude transcribed below states which column it came
from. AtmosphereModel::at() takes a GEOMETRIC altitude (matching EarthModel::altitude), so this
generator's output rows are indexed by geometric Z, converted to geopotential H internally exactly
as the plugin does.

Run: .venv/bin/python tests/golden/atmosphere/generate_isa1976_table.py > \
     tests/golden/atmosphere/isa1976_table.hpp && clang-format-18 -i \
     tests/golden/atmosphere/isa1976_table.hpp
(the clang-format pass is what `just format`/`just check` require of every .hpp; this script does
not shell out to it itself, so it stays usable in an environment without clang-format installed).
"""

from __future__ import annotations

import math
import sys
from dataclasses import dataclass

# ---------------------------------------------------------------------------------------------
# Defining constants, transcribed from Table 2 ("Adopted constants", doc p.2) and the prose
# restating g0'/T0/P0 (doc p.3-4). doc p.N below always means the printed page number in the
# document itself (top corner), not the PDF's own page count.
# ---------------------------------------------------------------------------------------------
G0_PRIME = 9.80665  # m^2/(s^2.m'); the standard geopotential meter's defining constant
EFFECTIVE_RADIUS_M = 6_356_766.0  # r0 (doc p.8: "the value of r0 is taken as 6,356,766 m")
GAS_CONSTANT = 8314.32  # R*, N.m/(kmol.K) (doc p.2-3: 8.31432e3)
MEAN_MOLECULAR_WEIGHT = 28.9644  # M0, kg/kmol (doc p.9, eq 20/21)
SPECIFIC_HEAT_RATIO = 1.40  # gamma (doc p.2, Table 2)
SEA_LEVEL_TEMPERATURE_K = 288.15  # T0 (doc p.2, Table 2)
SEA_LEVEL_PRESSURE_PA = 101_325.0  # P0 (doc p.2: 1.013250e5 N/m^2 exactly)

# Table 4 ("The defined reference levels and gradients ... from the surface to 86 geometric
# kilometers", doc p.3): base GEOPOTENTIAL altitude H_b (km' -> m) and the molecular-scale
# temperature gradient L_{M,b} (K/km' -> K/m), b = 0..6. H_7 = 84.8520 km' = 84852 m is the upper
# edge of the last layer (doc p.3's own note), not an eighth layer.
LAYER_BASE_GEOPOTENTIAL_M = [0.0, 11000.0, 20000.0, 32000.0, 47000.0, 51000.0, 71000.0, 84852.0]
LAYER_GRADIENT_K_PER_M = [-0.0065, 0.0, 0.001, 0.0028, 0.0, -0.0028, -0.002]
LAYER_COUNT = len(LAYER_GRADIENT_K_PER_M)

# Layer base pressures P_b (b = 1..6), transcribed from the H-indexed ("Geopotential Altitude")
# Table I pages, read at H exactly equal to each boundary above -- doc pages 58 (H=11000), 60
# (H=20000), 62 (H=32000), 64 (H=47000 and H=51000), 66 (H=71000). P_0 is the defining sea-level
# value above, not a table lookup. Cross-checked against this script's own integrated pressures
# below before being trusted; see check_transcribed_base_pressures() and, for how the tolerance
# below was chosen, BASE_PRESSURE_MB_DECIMAL_PLACES's comment.
TRANSCRIBED_BASE_PRESSURES_PA = {
    0: SEA_LEVEL_PRESSURE_PA,
    1: 22_632.0,  # 2.2632e2 mb, doc p.58
    2: 5_474.8,  # 5.4748e1 mb, doc p.60
    3: 868.01,  # 8.6801e0 mb, doc p.62
    4: 110.90,  # 1.1090e0 mb, doc p.64
    5: 66.938,  # 6.6938e-1 mb, doc p.64
    6: 3.9564,  # 3.9564e-2 mb, doc p.66
}

# Decimal places each transcribed P_b above was printed to IN MB (not Pa) on its source page --
# 226.32 (2dp), 54.748 (3dp), 8.6801 (4dp), 1.1090 (4dp), 0.66938 (5dp), 0.039564 (6dp). Used below
# to derive each layer's own printed-precision bound; not itself a tolerance.
BASE_PRESSURE_MB_DECIMAL_PLACES = {1: 2, 2: 3, 3: 4, 4: 4, 5: 5, 6: 6}


def base_pressure_own_relative_ulp(b: int) -> float:
    """Half a unit in the last printed decimal place of P_b's transcribed MB reading (doc pages
    cited above), as a fraction of P_b itself -- the document's own printed precision for this one
    layer, nothing upstream."""
    half_ulp_mb = 0.5 * 10 ** (-BASE_PRESSURE_MB_DECIMAL_PLACES[b])
    return (half_ulp_mb * 100.0) / TRANSCRIBED_BASE_PRESSURES_PA[b]  # 1 mb = 100 Pa exactly


def base_pressure_cross_check_tolerance() -> dict[int, float]:
    """Per-layer relative tolerance for check_transcribed_base_pressures(): each layer's own
    printed-precision bound (base_pressure_own_relative_ulp) PLUS every upstream layer's own bound,
    since P_b = P_{b-1} * (a ratio computed from this layer's T/lapse), so a relative error already
    present in P_{b-1} carries straight through into P_b. Deliberately NOT a single flat bound
    (Joe's condition, this session): a real error introduced at layer 1 must not be diluted by
    later layers' looser absolute-but-tighter-relative precision, so each layer keeps its own
    accumulated ceiling instead of borrowing the loosest layer's."""
    cumulative = {0: 0.0}
    for b in range(1, LAYER_COUNT):
        cumulative[b] = cumulative[b - 1] + base_pressure_own_relative_ulp(b)
    return cumulative


# Findings from this session's diagnosis of the observed cross-check disagreement (+2.8e-6 to
# +5.7e-5 relative, non-monotone across layers) before this per-layer tolerance was adopted -- kept
# here so a future reader does not have to redo the work:
#   1. Non-monotone across layers -> not simple accumulating compounding.
#   2. Recomputing each layer from the DOCUMENT's own P_{b-1} (not this script's integrated chain)
#      still disagrees with the document's own P_b by ~1e-5..5e-5, not ~1e-7 -> the disagreement is
#      not an artifact of this script's integration chain; each individual layer step disagrees
#      with the document's own printed output by roughly this much even given the document's own
#      input.
#   3. Back-solving the effective exponent/constant each layer's own numbers imply: the two
#      isothermal layers (b=2, b=5) imply g0'*M0/R* = 0.0341635 and 0.0341602 respectively --
#      disagreeing with EACH OTHER by ~1e-4 relative, not just with the theoretical 0.0341632. If
#      the original computation had used one single rounded value of this constant, both isothermal
#      layers would imply the same number; they do not, ruling out a single alternate constant as
#      the explanation.
#   4. The two largest-disagreement transcriptions (H=47000, H=20000) were re-read against the PDF
#      at 600 DPI and confirmed correct.
# Conclusion: the residual is the document's own ~5-to-6-significant-figure publication rounding of
# each P_b independently (and whatever intermediate rounding its 1976 computation pipeline used,
# which it does not disclose), not a transcription, constant, or formula error in this script or in
# plugins/physical/atmosphere/isa1976/isa1976.cpp.
BASE_PRESSURE_CROSS_CHECK_TOLERANCE = base_pressure_cross_check_tolerance()


def half_ulp(decimal_places: int) -> float:
    """Half a unit in a value's last printed decimal place -- the printed-precision bound every
    tolerance in this file is built from (see BASE_PRESSURE_MB_DECIMAL_PLACES above for the same
    idea applied to the layer base pressures)."""
    return 0.5 * 10 ** (-decimal_places)


def geopotential_from_geometric(altitude_m: float) -> float:
    """eq 18 (doc p.8); Gamma = g0/g0' = 1 m'/m exactly, so it drops out. Moved up here (ahead of
    the rest of the pressure-equation machinery below) because building TRANSCRIBED_ROWS needs it
    to determine each row's own layer -- from the FRESH geopotential H, not the document's printed,
    rounded H column (using the printed H contaminated this session's first attempt at diagnosing
    the per-row tolerance below with a spurious extra noise source: two altitude representations
    of the same row, not the actual computation the plugin/test perform)."""
    return EFFECTIVE_RADIUS_M * altitude_m / (EFFECTIVE_RADIUS_M + altitude_m)


def layer_index(h_m: float) -> int:
    """The largest b with LAYER_BASE_GEOPOTENTIAL_M[b] <= h_m, clamped to 0 below H_0 -- eq 33a is
    stated to apply "for H from 0 to -5 km'" too (doc p.12)."""
    layer = 0
    for i in range(1, LAYER_COUNT):
        if h_m < LAYER_BASE_GEOPOTENTIAL_M[i]:
            break
        layer = i
    return layer


# Every transcribed row's pressure/density carries, in addition to its own half_ulp(), the SAME
# accumulated printed-precision margin BASE_PRESSURE_CROSS_CHECK_TOLERANCE gives that row's own
# layer -- the layer determined from the FRESH geopotential H (geopotential_from_geometric), not
# the document's printed, rounded H column. This mechanism was checked against all eight rows
# (this session) and holds, with real margin to spare, for seven of them: every transcribed
# pressure/density value except Z=11000's pressure falls comfortably inside its own layer's
# BASE_PRESSURE_CROSS_CHECK_TOLERANCE-scaled margin. (An earlier attempt at this same check, using
# the document's printed H instead of a fresh one, wrongly appeared to falsify this mechanism
# entirely -- the printed H's own rounding to the nearest metre was an extra noise source that
# does not exist in the actual computation path: the plugin and this generator alike compute H
# fresh from Z every time, never reading a printed H back in.)
def transcribed_pressure_tolerance_pa(decimal_places: int, pressure_pa: float, layer: int) -> float:
    return max(
        half_ulp(decimal_places), BASE_PRESSURE_CROSS_CHECK_TOLERANCE[layer] * abs(pressure_pa)
    )


def transcribed_density_tolerance_kg_m3(
    decimal_places: int, density_kg_m3: float, layer: int
) -> float:
    """Same mechanism as transcribed_pressure_tolerance_pa: density is directly proportional to
    pressure at fixed temperature (eq 42, rho = P*M0/(R*T_M)), so the same layer-scaled relative
    margin applies to it too."""
    return max(
        half_ulp(decimal_places), BASE_PRESSURE_CROSS_CHECK_TOLERANCE[layer] * abs(density_kg_m3)
    )


# Z=11000's pressure is the ONE exception: it disagrees with a fresh computation by 4.233e-5
# relative even though it is layer 0 (H = 10980.998 m', 19 m' below the H=11000 tropopause -- the
# next-closest transcribed row to a lapse-rate break is 63 m' away, at Z=51000/H=50594.086) and
# computed from the EXACT P_0 alone, so BASE_PRESSURE_CROSS_CHECK_TOLERANCE[0] = 0.0 predicts this
# row no margin at all. UNEXPLAINED (this session, re-verified at 600 DPI against the source PDF --
# not a transcription error): two mechanisms were proposed and falsified for the table's residuals
# in general -- (1) per-layer accumulated base-pressure rounding, which this very row refutes as a
# COMPLETE explanation, since it has no P_b to inherit rounding from yet still disagrees; (2) half
# a unit in the printed digit alone, refuted by the magnitude. This constant is a named, single-row
# allowance sized from the observed residual with a margin, not a third mechanism dressed up as
# established fact, and it is added ONLY to Z=11000's own pressure tolerance below -- its density
# needs no such allowance (observed residual there is 4.3e-6 relative, well inside half_ulp()
# alone).
# Discrimination: the error classes this allowance (and the layer-scaled margin above) must still
# catch -- a flipped lapse-rate sign, a wrong pressure exponent, a geopotential/geometric altitude
# swap -- produce relative errors of order 1e-3 or larger, an order of magnitude past either.
Z_11000_PRESSURE_RESIDUAL_ALLOWANCE_RELATIVE = 1e-4  # ~2.4x the observed 4.233e-5 residual


# The eight "table points exact" rows (PLAN.md, docs/architecture.md §16), transcribed VERBATIM
# from the document's Z-indexed ("Geometric Altitude") Table I pages -- not computed, not derived
# from the constants above. altitude_m is the GEOMETRIC Z column; temperature_k is T_M (molecular-
# scale), which the document tabulates as a separate column from kinetic T and which is what
# plugins/physical/atmosphere/isa1976/isa1976.cpp computes and returns (see that file's header
# comment for why: below ~80 km the two are identical, and this model does not implement the
# M/M0 correction table that separates them in the narrow band right at the top of valid_range()).
# pressure_pa and density_kg_m3 are converted from the document's mb and kg/m^3 columns (1 mb =
# 100 Pa exactly). Source pages, each doc p.N, top-right corner: 0 -> 52, 11000 -> 59, 20000 -> 61,
# 32000 -> 63, 47000 & 51000 -> 65, 71000 -> 67, 86000 -> 68.
@dataclass(frozen=True)
class TranscribedRow:
    altitude_m: float  # geometric Z
    geopotential_h_m: float  # H, printed alongside Z on the same row -- recorded for the reader
    temperature_k: float  # T_M
    pressure_pa: float
    density_kg_m3: float
    # Per-value tolerance -- not a single flat bound (Joe's condition, this session, the same one
    # behind BASE_PRESSURE_CROSS_CHECK_TOLERANCE above): pressure and density span nine orders of
    # magnitude across these eight rows, so a flat bound sized for the loosest row is too tight for
    # the others and would misreport a future disagreement on a tighter row as a code bug instead
    # of a wrong tolerance. temperature_tol_k is pure half_ulp(). pressure_tol_pa and
    # density_tol_kg_m3 are half_ulp() OR this row's own layer's BASE_PRESSURE_CROSS_CHECK_TOLERANCE
    # margin, whichever is looser (transcribed_pressure_tolerance_pa's comment has the full
    # derivation), plus Z=11000's own named exception on pressure_tol_pa alone
    # (Z_11000_PRESSURE_RESIDUAL_ALLOWANCE_RELATIVE's comment).
    temperature_tol_k: float
    pressure_tol_pa: float
    density_tol_kg_m3: float


# altitude_m, geopotential_h_m, temperature_k, pressure_pa, density_kg_m3 -- see TranscribedRow's
# own comment for what each column is and where it came from.
_TRANSCRIBED_VALUES = [
    (0.0, 0.0, 288.150, 101_325.0, 1.2250),
    (11_000.0, 10_981.0, 216.774, 22_699.0, 0.36480),
    (20_000.0, 19_937.0, 216.650, 5_529.3, 0.088910),
    (32_000.0, 31_840.0, 228.490, 889.06, 0.013555),
    (47_000.0, 46_655.0, 269.684, 115.85, 0.0014965),
    (51_000.0, 50_594.0, 270.650, 70.458, 0.00090690),
    (71_000.0, 70_216.0, 216.846, 4.4795, 0.000071966),
    (86_000.0, 84_852.0, 186.95, 0.37338, 0.000006958),
]

# Decimal places each value above was printed to on its source page, in the units the value is
# stored in above (K; Pa, after the source's mb reading x 100; kg/m^3 direct) -- one entry per row
# in _TRANSCRIBED_VALUES, same method as BASE_PRESSURE_MB_DECIMAL_PLACES. A trailing printed zero
# (Z=0's rho = 1.2250 kg/m^3, not 1.225) counts as precision the document actually claims; a
# stored Python float can't tell "1.2250" from "1.225" apart, which is why this table exists
# instead of inferring precision from the numbers above:
#   T_M (K):        3dp every row except 86000's 2dp (288.150 ... 216.846, then 186.95).
#   P (Pa):         0dp (101325, 22699 -- both from a 2dp mb reading), 1dp (5529.3, 3dp mb), 2dp
#                   (889.06, 115.85 -- 4dp mb), 3dp (70.458, 5dp mb), 4dp (4.4795, 6dp mb), 5dp
#                   (0.37338, 7dp mb).
#   rho (kg/m^3):   4dp (1.2250), 5dp (0.36480), 6dp (0.088910, 0.013555), 7dp (0.0014965), 8dp
#                   (0.00090690), 9dp (0.000071966, 0.000006958 -- the last row prints only 4
#                   significant figures, but at one lower order of magnitude, so its decimal-place
#                   count coincides with the row above's 5-significant-figure reading).
_TRANSCRIBED_TEMPERATURE_DECIMAL_PLACES = [3, 3, 3, 3, 3, 3, 3, 2]
_TRANSCRIBED_PRESSURE_PA_DECIMAL_PLACES = [0, 0, 1, 2, 2, 3, 4, 5]
_TRANSCRIBED_DENSITY_DECIMAL_PLACES = [4, 5, 6, 6, 7, 8, 9, 9]


def _build_transcribed_rows() -> list[TranscribedRow]:
    rows = []
    for i, values in enumerate(_TRANSCRIBED_VALUES):
        altitude_m, _, _, pressure_pa, density_kg_m3 = values
        # The row's own layer, from a FRESH geopotential H -- see
        # transcribed_pressure_tolerance_pa's comment for why not the printed H column.
        layer = layer_index(geopotential_from_geometric(altitude_m))
        pressure_tol_pa = transcribed_pressure_tolerance_pa(
            _TRANSCRIBED_PRESSURE_PA_DECIMAL_PLACES[i], pressure_pa, layer
        )
        if altitude_m == 11_000.0:
            pressure_tol_pa += Z_11000_PRESSURE_RESIDUAL_ALLOWANCE_RELATIVE * abs(pressure_pa)
        density_tol_kg_m3 = transcribed_density_tolerance_kg_m3(
            _TRANSCRIBED_DENSITY_DECIMAL_PLACES[i], density_kg_m3, layer
        )
        rows.append(
            TranscribedRow(
                *values,
                half_ulp(_TRANSCRIBED_TEMPERATURE_DECIMAL_PLACES[i]),
                pressure_tol_pa,
                density_tol_kg_m3,
            )
        )
    return rows


TRANSCRIBED_ROWS = _build_transcribed_rows()

# kComputedRows: dense geometric-altitude sampling, generated (not transcribed) from the
# document-verified constants above by literally re-deriving eq 18/23/33a/33b/42/50 in Python.
# 1000 m spacing comfortably straddles every layer boundary (the narrowest gap, 47-51 km, is still
# 4000 m) so continuity across each one is exercised on both sides.
COMPUTED_ROW_STEP_M = 1000.0
COMPUTED_ROW_MIN_M = -5000.0
COMPUTED_ROW_MAX_M = 86_000.0
# Set from this script's and the C++ plugin's shared inputs (the transcribed constants above) and
# otherwise-independent arithmetic (Python's math.pow/math.exp vs C++'s std::pow/std::exp): the two
# are not expected to agree to the last bit, but a real formula error (wrong exponent, flipped
# lapse-rate sign, wrong layer picked) is many orders of magnitude larger than this.
COMPUTED_RELATIVE_TOLERANCE = 1e-6


def layer_base_temperatures() -> list[float]:
    """T_{M,b} at each layer's base (eq 23, doc p.10, applied recursively from T_{M,0} = T0)."""
    temperatures = [SEA_LEVEL_TEMPERATURE_K]
    for b in range(1, LAYER_COUNT):
        span = LAYER_BASE_GEOPOTENTIAL_M[b] - LAYER_BASE_GEOPOTENTIAL_M[b - 1]
        temperatures.append(temperatures[b - 1] + LAYER_GRADIENT_K_PER_M[b - 1] * span)
    return temperatures


LAYER_BASE_TEMPERATURE_K = layer_base_temperatures()


def pressure_within_layer(layer: int, h_m: float, base_pressures_pa: list[float]) -> float:
    """eq 33a for a nonzero lapse rate, eq 33b for zero (doc p.12)."""
    lapse = LAYER_GRADIENT_K_PER_M[layer]
    base_h = LAYER_BASE_GEOPOTENTIAL_M[layer]
    base_t = LAYER_BASE_TEMPERATURE_K[layer]
    base_p = base_pressures_pa[layer]
    if lapse == 0.0:
        return base_p * math.exp(
            -G0_PRIME * MEAN_MOLECULAR_WEIGHT * (h_m - base_h) / (GAS_CONSTANT * base_t)
        )
    t = base_t + lapse * (h_m - base_h)
    return base_p * (base_t / t) ** (G0_PRIME * MEAN_MOLECULAR_WEIGHT / (GAS_CONSTANT * lapse))


def integrated_base_pressures() -> list[float]:
    """P_b for b = 1..6, integrating upward from P_0 exactly as the document constructs them (doc
    p.12: P_b comes from applying eq 33a/33b for the *previous* layer, evaluated at H = H_b)."""
    pressures = [SEA_LEVEL_PRESSURE_PA]
    for b in range(1, LAYER_COUNT):
        pressures.append(pressure_within_layer(b - 1, LAYER_BASE_GEOPOTENTIAL_M[b], pressures))
    return pressures


def check_transcribed_base_pressures(computed: list[float]) -> None:
    """Joe's cross-check: the transcribed base pressures must match this script's own integration,
    each layer within ITS OWN accumulated printed-precision bound (BASE_PRESSURE_CROSS_CHECK_
    TOLERANCE's comment). A disagreement past that bound means the constants (not just the code)
    are suspect -- report it, don't reconcile it by adjusting either side."""
    mismatches = []
    for b, transcribed in TRANSCRIBED_BASE_PRESSURES_PA.items():
        tolerance = BASE_PRESSURE_CROSS_CHECK_TOLERANCE[b]
        rel_diff = abs(computed[b] - transcribed) / transcribed
        if rel_diff > tolerance:
            mismatches.append(
                f"  P_{b}: transcribed {transcribed} Pa, integrated {computed[b]!r} Pa, "
                f"rel diff {rel_diff:.3e} > tolerance {tolerance:.3e}"
            )
    if mismatches:
        raise SystemExit(
            "Base pressure cross-check FAILED -- constants are suspect, not reconciling:\n"
            + "\n".join(mismatches)
        )


@dataclass(frozen=True)
class ComputedRow:
    altitude_m: float
    temperature_k: float
    pressure_pa: float
    density_kg_m3: float
    speed_of_sound_m_s: float


def compute_row(altitude_m: float, base_pressures_pa: list[float]) -> ComputedRow:
    h_m = geopotential_from_geometric(altitude_m)
    layer = layer_index(h_m)
    temperature_k = LAYER_BASE_TEMPERATURE_K[layer] + LAYER_GRADIENT_K_PER_M[layer] * (
        h_m - LAYER_BASE_GEOPOTENTIAL_M[layer]
    )
    pressure_pa = pressure_within_layer(layer, h_m, base_pressures_pa)
    density_kg_m3 = pressure_pa * MEAN_MOLECULAR_WEIGHT / (GAS_CONSTANT * temperature_k)
    speed_of_sound_m_s = math.sqrt(
        SPECIFIC_HEAT_RATIO * GAS_CONSTANT * temperature_k / MEAN_MOLECULAR_WEIGHT
    )
    return ComputedRow(altitude_m, temperature_k, pressure_pa, density_kg_m3, speed_of_sound_m_s)


def check_transcribed_row_tolerances(base_pressures_pa: list[float]) -> None:
    """Prints (to stderr; stdout is the generated header) each transcribed row's actual
    disagreement with a fresh computation as a fraction of THAT row's own stored pressure/density
    tolerance, so a future regeneration that moves any row toward its own limit is a visible signal
    (Joe's condition, this session), not silently-absorbed noise. Also a hard gate: a row
    disagreeing with a fresh computation by more than its own stored tolerance would make the
    golden fail its own C++ test by construction -- caught here first, at generation time."""
    worst_ratio = 0.0
    worst_label = ""
    for row in TRANSCRIBED_ROWS:
        computed = compute_row(row.altitude_m, base_pressures_pa)
        for label, transcribed_value, computed_value, tolerance in (
            ("pressure_pa", row.pressure_pa, computed.pressure_pa, row.pressure_tol_pa),
            ("density_kg_m3", row.density_kg_m3, computed.density_kg_m3, row.density_tol_kg_m3),
        ):
            diff = abs(computed_value - transcribed_value)
            ratio = diff / tolerance
            print(
                f"# Z={row.altitude_m:>8.0f} {label:<14} diff={diff:.3e} "
                f"tolerance={tolerance:.3e} ratio={ratio:.3f}",
                file=sys.stderr,
            )
            if ratio > worst_ratio:
                worst_ratio, worst_label = ratio, f"{label} at Z={row.altitude_m:.0f}"
    if worst_ratio >= 1.0:
        raise SystemExit(
            f"Transcribed-row tolerance EXCEEDED at {worst_label} ({worst_ratio:.3f}x) -- the "
            "golden would fail its own C++ test; investigate before trusting it."
        )


def check_geopotential_consistency() -> None:
    """M2 PR 3b's own addition (PLAN.md): the same class of mistake as docs/lessons.md's
    2026-09-15 entry on NASA-TM-X-74335's dual-column Table I -- geopotential H and geometric Z
    are transcribed from two DIFFERENT columns of the source document (TranscribedRow's own
    comment), so a swap or a transcription slip in either would not be caught by
    check_transcribed_row_tolerances above (which only exercises pressure/density, not the
    altitude columns themselves). Cross-checks every transcribed row's own printed
    geopotential_h_m against eq 18 applied fresh to that row's own transcribed altitude_m
    (geometric Z): the two are computed by entirely different means (one printed by the 1976
    source, one derived here in Python from the same source's own equation), so they should
    agree to whatever precision the document's own rounding allows, not exactly. half_ulp(0)
    (0.5 m) is generous against the document's own metre-level rounding of H; a real Z/H swap or
    transcription slip is orders of magnitude past this (worst observed residual this session:
    0.281 m, well inside 0.5 m)."""
    tolerance_m = half_ulp(0)
    worst_residual = 0.0
    worst_altitude = 0.0
    for row in TRANSCRIBED_ROWS:
        residual = abs(geopotential_from_geometric(row.altitude_m) - row.geopotential_h_m)
        print(
            f"# Z={row.altitude_m:>8.0f} geopotential consistency residual={residual:.3e} m "
            f"tolerance={tolerance_m:.3e} m",
            file=sys.stderr,
        )
        if residual > worst_residual:
            worst_residual, worst_altitude = residual, row.altitude_m
    if worst_residual > tolerance_m:
        raise SystemExit(
            f"Geopotential consistency FAILED at Z={worst_altitude:.0f} "
            f"(residual {worst_residual:.3e} m > tolerance {tolerance_m:.3e} m) -- the "
            "transcribed geopotential_h_m and altitude_m columns disagree with eq 18 by more "
            "than the document's own printed-precision rounding; investigate before trusting "
            "this table."
        )


def computed_rows(base_pressures_pa: list[float]) -> list[ComputedRow]:
    rows = []
    steps = round((COMPUTED_ROW_MAX_M - COMPUTED_ROW_MIN_M) / COMPUTED_ROW_STEP_M)
    for i in range(steps + 1):
        altitude_m = COMPUTED_ROW_MIN_M + i * COMPUTED_ROW_STEP_M
        rows.append(compute_row(altitude_m, base_pressures_pa))
    return rows


def render_header(rows: list[ComputedRow]) -> str:
    transcribed_entries = ",\n".join(
        f"    {{{r.altitude_m}, {r.temperature_k}, {r.pressure_pa}, {r.density_kg_m3}, "
        f"{r.temperature_tol_k!r}, {r.pressure_tol_pa!r}, {r.density_tol_kg_m3!r}}}"
        for r in TRANSCRIBED_ROWS
    )
    computed_entries = ",\n".join(
        f"    {{{r.altitude_m}, {r.temperature_k!r}, {r.pressure_pa!r}, "
        f"{r.density_kg_m3!r}, {r.speed_of_sound_m_s!r}}}"
        for r in rows
    )
    return f"""\
// ISA-1976 golden (PLAN.md M2 PR 1b, docs/architecture.md §16: "ISA table points exact"). Human-
// owned: lives under tests/golden/ so guard-oracle.sh blocks agent edits. Generated by
// tests/golden/atmosphere/generate_isa1976_table.py -- do not hand-edit; re-run that script after
// any change to its transcribed constants and commit its output.
//
// Two independently-sourced tables:
//   kTranscribedRows -- {len(TRANSCRIBED_ROWS)} rows read VERBATIM off the document's own printed
//     tables (U.S. Standard Atmosphere, 1976, NOAA-S/T 76-1562 / NASA-TM-X-74335), not computed by
//     this script or by the plugin under test. This is what "table points exact" (§16) names: the
//     one comparison where the C++ plugin is judged against the document itself, not against
//     another implementation of the same formula -- two implementations sharing a wrong exponent
//     or a flipped lapse-rate sign would still agree with each other and pass a computed-only
//     check. Tolerance is per VALUE (each row's temperature_tol_k/pressure_tol_pa/
//     density_tol_kg_m3 fields): temperature is half a unit in its own last printed digit;
//     pressure and density are that OR their row's own layer's accumulated printed-precision
//     margin (the same BASE_PRESSURE_CROSS_CHECK_TOLERANCE below the layer base pressures use),
//     whichever is looser, plus a named exception on Z=11000's pressure alone -- see
//     transcribed_pressure_tolerance_pa's comment in the generator for the full derivation and
//     Z_11000_PRESSURE_RESIDUAL_ALLOWANCE_RELATIVE's comment for that one row.
//     altitude_m is the document's GEOMETRIC (Z) column; temperature_k is T_M (molecular-scale),
//     the column plugins/physical/atmosphere/isa1976/isa1976.cpp computes and returns -- see that
//     file's header comment for why kinetic T is not used. Source: doc pages 52, 59, 61, 63, 65
//     (x2), 67, 68 (top-right corner page numbers, not PDF page numbers).
//   kComputedRows -- {len(rows)} rows computed by generate_isa1976_table.py from the same
//     document-verified constants, independently of the C++ plugin (Python, not a build of
//     astra_core), at {COMPUTED_ROW_STEP_M:.0f} m geometric spacing from {COMPUTED_ROW_MIN_M:.0f}
//     to {COMPUTED_ROW_MAX_M:.0f} m -- dense enough to straddle every one of the seven layer
//     boundaries and check continuity, which the eight transcribed points alone cannot. Tolerance
//     is a relative bound loose enough for ordinary double-precision libm disagreement between
//     Python and C++, tight enough that a real formula error is orders of magnitude past it (see
//     COMPUTED_RELATIVE_TOLERANCE's comment).
//
// The seven layer base pressures this script integrated from P_0 = {SEA_LEVEL_PRESSURE_PA} Pa were
// cross-checked against the same document's own tabulated values (transcribed separately, at exact
// geopotential boundaries, from the document's OTHER Table I column pairing) before this file was
// generated -- see check_transcribed_base_pressures() in the generator. That check passed, but not
// trivially: it initially disagreed by +2.8e-6 to +5.7e-5 relative, non-monotone across layers.
// Four checks (this session, see BASE_PRESSURE_CROSS_CHECK_TOLERANCE's comment in the generator for
// the full account) found this consistent with the document's own ~5-to-6-significant-figure
// publication rounding of each P_b independently, not a transcription, constant, or formula error
// -- so the cross-check's tolerance is itself derived from that same per-layer printed precision,
// accumulated layer by layer, rather than one flat number loose enough to paper over a real defect.
//
// Separately: every transcribed row's pressure/density, not just the seven layer-boundary base
// pressures above, carries the SAME accumulated printed-precision margin its own layer's
// BASE_PRESSURE_CROSS_CHECK_TOLERANCE gives -- checked against all eight rows (this session,
// using each row's FRESH geopotential H, not the document's printed, rounded H column) and found
// to hold, with real margin to spare, for seven of them. The one exception is Z=11000's pressure:
// it disagrees with a fresh computation by 4.233e-5 relative even though it is layer 0 and
// computed from the exact P_0 alone, so the mechanism predicts it NO margin at all. UNEXPLAINED
// (re-verified at 600 DPI against the source PDF, not a transcription error): two OTHER mechanisms
// were proposed and FALSIFIED for the table's residuals in general -- per-layer accumulated
// base-pressure rounding as a COMPLETE explanation (this row itself refutes that, having no P_b to
// inherit rounding from) and half a unit in the printed digit alone (refuted by the magnitude).
// Z_11000_PRESSURE_RESIDUAL_ALLOWANCE_RELATIVE in the generator is a named, single-row allowance
// for this one unexplained residual, not a third mechanism dressed up as fact. The error classes
// either margin must still catch -- a flipped lapse-rate sign, a wrong pressure exponent, a
// geopotential/geometric altitude swap -- are of order 1e-3 relative or larger, an order of
// magnitude past either.
#pragma once

namespace astra::test::isa1976 {{

inline constexpr double kComputedRelativeTolerance = {COMPUTED_RELATIVE_TOLERANCE};

// NOLINTBEGIN(modernize-avoid-c-arrays,modernize-use-std-numbers): a data table; C arrays keep the
// braces one level deep, and a physically-meaningful computed value coincidentally landing within
// clang-tidy's tolerance of a named math constant (observed: a density row near std::numbers::phi)
// is not a reason to spell a table entry as a symbolic constant.
struct TranscribedRow {{
  double altitude_m;      // geometric
  double temperature_k;   // T_M
  double pressure_pa;
  double density_kg_m3;
  // temperature_tol_k: half a unit in this row's own last printed digit. pressure_tol_pa/
  // density_tol_kg_m3: that OR this row's own layer's accumulated printed-precision margin,
  // whichever is looser, plus a named exception on Z=11000's pressure alone -- see this file's
  // own provenance comment above for the full derivation.
  double temperature_tol_k;
  double pressure_tol_pa;
  double density_tol_kg_m3;
}};

inline constexpr TranscribedRow kTranscribedRows[] = {{
{transcribed_entries}}};

struct ComputedRow {{
  double altitude_m;      // geometric
  double temperature_k;   // T_M
  double pressure_pa;
  double density_kg_m3;
  double speed_of_sound_m_s;
}};

inline constexpr ComputedRow kComputedRows[] = {{
{computed_entries}}};
// NOLINTEND(modernize-avoid-c-arrays,modernize-use-std-numbers)

}}  // namespace astra::test::isa1976
"""


def main() -> None:
    base_pressures_pa = integrated_base_pressures()
    check_transcribed_base_pressures(base_pressures_pa)
    check_transcribed_row_tolerances(base_pressures_pa)
    check_geopotential_consistency()
    rows = computed_rows(base_pressures_pa)
    print(render_header(rows), end="")


if __name__ == "__main__":
    main()
