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
check_transcribed_base_pressures() and BASE_PRESSURE_CROSS_CHECK_TOLERANCE's comment.

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


TRANSCRIBED_ROWS = [
    TranscribedRow(0.0, 0.0, 288.150, 101_325.0, 1.2250),
    TranscribedRow(11_000.0, 10_981.0, 216.774, 22_699.0, 0.36480),
    TranscribedRow(20_000.0, 19_937.0, 216.650, 5_529.3, 0.088910),
    TranscribedRow(32_000.0, 31_840.0, 228.490, 889.06, 0.013555),
    TranscribedRow(47_000.0, 46_655.0, 269.684, 115.85, 0.0014965),
    TranscribedRow(51_000.0, 50_594.0, 270.650, 70.458, 0.00090690),
    TranscribedRow(71_000.0, 70_216.0, 216.846, 4.4795, 0.000071966),
    TranscribedRow(86_000.0, 84_852.0, 186.95, 0.37338, 0.000006958),
]
# Half a unit in the last printed digit, worked out per row and per column before picking one
# flat bound for each (temperature absolute, pressure/density relative -- these span orders of
# magnitude, unlike the layer base pressures above which stay within one table's column). Every
# row's own bound, computed the same way as BASE_PRESSURE_MB_DECIMAL_PLACES above:
#   temperature_k (K, absolute half-ulp): 0.0005 at every row except 86000's 0.005 (2dp there,
#     3dp elsewhere) -- 0.01 covers both with margin, and T never crosses zero so one absolute
#     bound is meaningful across every row.
#   pressure_pa (relative half-ulp): 2.2e-5 (11000), 9.0e-6 (20000), 5.6e-6 (32000),
#     4.3e-5 (47000), 7.1e-6 (51000), 1.1e-5 (71000), 1.3e-5 (86000) -- worst is 47000's 4.3e-5.
#   density_kg_m3 (relative half-ulp): 4.1e-5 (0), 1.4e-5 (11000), 5.6e-6 (20000), 3.7e-5 (32000),
#     3.3e-5 (47000), 5.5e-6 (51000), 7.0e-6 (71000), 7.2e-5 (86000, only 4 significant figures
#     there) -- worst is 86000's 7.2e-5.
# 1e-4 covers both worst cases (pressure's 4.3e-5, density's 7.2e-5) with >1.3x margin at the
# tightest row while staying far tighter than a real formula error would produce. This tolerance is
# for the golden table points ONLY -- kept deliberately separate from
# BASE_PRESSURE_CROSS_CHECK_TOLERANCE above, which exists to sanity-check this script's own
# generator-internal integration, not to bound what the plugin is judged against.
TRANSCRIBED_TEMPERATURE_TOLERANCE_K = 0.01
TRANSCRIBED_RELATIVE_TOLERANCE = 1e-4

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


def geopotential_from_geometric(altitude_m: float) -> float:
    """eq 18 (doc p.8); Gamma = g0/g0' = 1 m'/m exactly, so it drops out."""
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


def computed_rows(base_pressures_pa: list[float]) -> list[ComputedRow]:
    rows = []
    steps = round((COMPUTED_ROW_MAX_M - COMPUTED_ROW_MIN_M) / COMPUTED_ROW_STEP_M)
    for i in range(steps + 1):
        altitude_m = COMPUTED_ROW_MIN_M + i * COMPUTED_ROW_STEP_M
        rows.append(compute_row(altitude_m, base_pressures_pa))
    return rows


def render_header(rows: list[ComputedRow]) -> str:
    transcribed_entries = ",\n".join(
        f"    {{{r.altitude_m}, {r.temperature_k}, {r.pressure_pa}, {r.density_kg_m3}}}"
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
//     check. Tolerance is half a unit in each value's last printed digit (see the generator's
//     TRANSCRIBED_TEMPERATURE_TOLERANCE_K / TRANSCRIBED_RELATIVE_TOLERANCE for the exact
//     reasoning).
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
#pragma once

namespace astra::test::isa1976 {{

inline constexpr double kTranscribedTemperatureToleranceK = {TRANSCRIBED_TEMPERATURE_TOLERANCE_K};
inline constexpr double kTranscribedRelativeTolerance = {TRANSCRIBED_RELATIVE_TOLERANCE};
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
    rows = computed_rows(base_pressures_pa)
    print(render_header(rows), end="")


if __name__ == "__main__":
    main()
