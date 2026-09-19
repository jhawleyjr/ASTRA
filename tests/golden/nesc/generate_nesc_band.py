"""Independent generator for tests/golden/nesc/case{01..10}_<slug>_band.hpp (ADR-0013,
ADR-0014, PLAN.md M2 PR 3b). Human-owned, like the headers it writes (tests/golden/**,
guard-oracle.sh): re-run after any change to the constants or exclusion rules below, never
hand-edit the output. Follows tests/golden/atmosphere/generate_isa1976_table.py's shape: pure
stdlib Python, every constant cited, self-checks that raise SystemExit on their own detected
inconsistency before ever emitting output.

Reads NASA/TM-2015-218675's raw check-case CSVs (docs/refs/Atmospheric_checkcases/, ~82 MB,
gitignored, never committed) and reduces them to ten small committed headers -- the band, not
the ensemble (ADR-0013's own framing). This script is never run by `just check`;
`tests/golden/nesc/` is guarded by guard-oracle.sh and needs `touch-goldens` armed to write
anything, including this file itself.

Run (from a worktree, refs read from the main checkout -- gitignored files do not exist inside
a git worktree, docs/refs/README.md's own note):
    .venv/bin/python tests/golden/nesc/generate_nesc_band.py [--refs PATH]
    clang-format-18 -i tests/golden/nesc/case*_band.hpp
--refs defaults to the main checkout's docs/refs/Atmospheric_checkcases, resolved via
`git rev-parse --git-common-dir` the same way guard-oracle.sh resolves the touch-goldens marker.

Oracle formula (ADR-0013, elaborated by ADR-0014 and this session's yaw-wrap fix -- see
docs/decisions/0013-nesc-check-case-oracle.md and 0014-nesc-per-channel-tolerance-floor.md):
    reference_t = mean across participating sims, PER SAMPLE TIME t
    tol_abs     = k * max over t of (max - min across sims)     -- ONE scalar per channel/case
    tol_rel     = k * max over t of ((max - min) / |mean|)      -- ONE scalar, omitted where
                  |mean| is small relative to that channel's own tol_abs (see
                  omit_relative_term()'s comment)
    tol_abs     = max(tol_abs, ADR-0014's per-channel-kind floor)
    tol_rel     = max(tol_rel, ADR-0014's floor) for atmosphere channels only (their floor is
                  on tol_rel, not tol_abs -- see ADR-0014's Decision table)
    pass        : |astra - reference_t| <= tol_abs + tol_rel * |reference_t|
    k = 3

Yaw is the one channel this data set's trajectories actually cross the +/-180 deg wrap
boundary on (cases 2, 4, 5 -- Joe's spec review, confirmed against the raw CSVs this session).
A raw (non-circular) spread at a wrap instant is ~359 deg, which would make ADR-0013's
max_t(spread) formula vacuous for the whole case. Fix: yaw's per-sample mean and spread are
computed on the UNWRAPPED difference between each tool's raw value and a fixed reference tool
(mod 2*pi, mapped to the shortest signed angular distance via wrap_to_pi()), never on the raw
degree values directly -- see compute_channel()'s own is_yaw branch. Separately (Codex plan
critique): yaw NEVER carries a relative term (tol_rel is always 0.0), at every sample, not just
near a wrap instant -- because tol_rel is ONE scalar taking the max over every sample time, a
relative-spread computed at a single bad wrap instant using the small unwrapped residual mean
as its denominator (rather than the reconstructed ~180 deg absolute reference) could blow up
arbitrarily and poison the whole case's yaw tolerance for all 301 samples, not just that one
instant. Dropping the relative term for yaw removes this failure mode's entire mechanism
instead of trying to reconstruct a correct denominator for it.

Vol II Tables 77/78 (doc p.591-592) participant exclusions, honored here, not re-litigated:
  - SIM 2's Euler angles (footnote a: "large integration errors") and aero moments (footnote
    d: "reported at the MRC, not the CM") are excluded in EVERY case.
  - SIM 2's X-body force is excluded in cases 4 and 5 only (footnote b: "unexplained X-body
    force and local gravity differences").
  - SIM 3's Y-body force is excluded in cases 6, 7, and 8 only (footnote c: "unexplained
    non-zero values in Y-body force").
Plus a t=0-only exclusion (PLAN.md's own addition, this session): SIM 6 is excluded at t=0
from every aero force/moment channel in BOTH wind cases (7 and 8); SIM 3 is excluded at t=0
from the same channels in case 8 only. Both publish a physically-wrong zero relative airspeed
at their own first sample in a wind case -- case 8's Fy band alone would widen 1.7x if that one
bad instant were left in, since tol_abs is a single scalar taking the max over every sample.
"""

from __future__ import annotations

import argparse
import csv
import math
import re
import subprocess
import sys
from dataclasses import dataclass, field
from pathlib import Path

# ---------------------------------------------------------------------------------------------
# Constants, cited to Vol II Table 73 (doc p.93, "Atmospheric check-cases" block) and standard
# exact unit definitions. Table 73 itself does not tabulate a force or temperature conversion,
# but its own "kilogram to slug" formula (0.3048/4.4482216152605) already contains the exact
# pound-force-to-newton constant (4.4482216152605 N per lbf, the international standard
# definition) as its denominator -- used directly here, not re-derived.
# ---------------------------------------------------------------------------------------------
FT_TO_M = 0.3048  # exact (Table 73)
LBF_TO_N = (
    4.4482216152605  # exact (international standard; also Table 73's own kg-to-slug denominator)
)
KG_TO_SLUG = 0.3048 / 4.4482216152605  # exact (Table 73: "kilogram to slug")
SLUG_TO_KG = 1.0 / KG_TO_SLUG
DEG_TO_RAD = math.pi / 180.0
RANKINE_TO_KELVIN = 5.0 / 9.0  # exact: both are absolute scales, a pure ratio, no offset
OMEGA_IE_RAD_S = 7.292115e-5  # Table 73, atmospheric check-cases block

SLUG_FT3_TO_KG_M3 = SLUG_TO_KG / (FT_TO_M**3)
LBF_FT2_TO_PA = LBF_TO_N / (FT_TO_M**2)

K = 3  # ADR-0013's single judgment number

SAMPLE_COUNT = 301
SAMPLE_DT = 0.1
NEAREST_SAMPLE_TOLERANCE_S = 1e-6  # ADR-0013: "nearest sample within 1e-6 s"

# ADR-0014's per-channel-kind floor. "angle"/"length"/"velocity"/"rate"/"force" floor tol_abs;
# "atmosphere" floors tol_rel instead (see ADR-0014's Decision table and its own comment on why).
FLOOR_ABS = {
    "angle": 1e-12,  # rad
    "length": 1e-9,  # m
    "velocity": 1e-9,  # m/s
    "rate": 1e-12,  # rad/s
    "force": 1e-12,  # N or N.m
    "atmosphere": 0.0,  # no abs floor; these channels are never analytically zero in this data
}
FLOOR_REL = {
    "atmosphere": 1e-12,
}


@dataclass(frozen=True)
class Channel:
    key: str  # ChannelBand constant name suffix, e.g. "latitude" -> kLatitude
    column: str  # CSV header name
    scale: float  # multiply the raw CSV value by this to get SI
    kind: str  # FLOOR_ABS/FLOOR_REL key
    is_yaw: bool = False


CHANNELS: list[Channel] = [
    Channel("latitude", "latitude_deg", DEG_TO_RAD, "angle"),
    Channel("longitude", "longitude_deg", DEG_TO_RAD, "angle"),
    Channel("altitude", "altitudeMsl_ft", FT_TO_M, "length"),
    Channel("v_north", "feVelocity_ft_s_X", FT_TO_M, "velocity"),
    Channel("v_east", "feVelocity_ft_s_Y", FT_TO_M, "velocity"),
    Channel("v_down", "feVelocity_ft_s_Z", FT_TO_M, "velocity"),
    Channel("yaw", "eulerAngle_deg_Yaw", DEG_TO_RAD, "angle", is_yaw=True),
    Channel("pitch", "eulerAngle_deg_Pitch", DEG_TO_RAD, "angle"),
    Channel("roll", "eulerAngle_deg_Roll", DEG_TO_RAD, "angle"),
    Channel("rate_p", "bodyAngularRateWrtEi_deg_s_Roll", DEG_TO_RAD, "rate"),
    Channel("rate_q", "bodyAngularRateWrtEi_deg_s_Pitch", DEG_TO_RAD, "rate"),
    Channel("rate_r", "bodyAngularRateWrtEi_deg_s_Yaw", DEG_TO_RAD, "rate"),
    Channel("force_x", "aero_bodyForce_lbf_X", LBF_TO_N, "force"),
    Channel("force_y", "aero_bodyForce_lbf_Y", LBF_TO_N, "force"),
    Channel("force_z", "aero_bodyForce_lbf_Z", LBF_TO_N, "force"),
    Channel("moment_l", "aero_bodyMoment_ftlbf_L", LBF_TO_N * FT_TO_M, "force"),
    Channel("moment_m", "aero_bodyMoment_ftlbf_M", LBF_TO_N * FT_TO_M, "force"),
    Channel("moment_n", "aero_bodyMoment_ftlbf_N", LBF_TO_N * FT_TO_M, "force"),
    Channel("density", "airDensity_slug_ft3", SLUG_FT3_TO_KG_M3, "atmosphere"),
    Channel("pressure", "ambientPressure_lbf_ft2", LBF_FT2_TO_PA, "atmosphere"),
    Channel("temperature", "ambientTemperature_dgR", RANKINE_TO_KELVIN, "atmosphere"),
]

EULER_KEYS = {"yaw", "pitch", "roll"}
MOMENT_KEYS = {"moment_l", "moment_m", "moment_n"}
AIRSPEED_DERIVED_KEYS = {"force_x", "force_y", "force_z", "moment_l", "moment_m", "moment_n"}


@dataclass(frozen=True)
class Case:
    num: int
    folder: str
    slug: str


CASES: list[Case] = [
    Case(1, "Atmos_01_DroppedSphere", "dropped_sphere"),
    Case(2, "Atmos_02_TumblingBrickNoDamping", "tumbling_brick_no_damping"),
    Case(3, "Atmos_03_TumblingBrickDamping", "tumbling_brick_damping"),
    Case(4, "Atmos_04_DroppedSphereRoundNonRotation", "dropped_sphere_round_non_rotation"),
    Case(5, "Atmos_05_DroppedSphereRoundRotation", "dropped_sphere_round_rotation"),
    Case(6, "Atmos_06_DroppedSphereEllipsoidalNoWind", "dropped_sphere_ellipsoidal_no_wind"),
    Case(7, "Atmos_07_DroppedSphereSteadyWind", "dropped_sphere_steady_wind"),
    Case(8, "Atmos_08_DroppedSphere2DWindShear", "dropped_sphere_2d_wind_shear"),
    Case(9, "Atmos_09_EastwardCannonball", "eastward_cannonball"),
    Case(10, "Atmos_10_NorthwardCannonball", "northward_cannonball"),
]

SIM_FILE_RE = re.compile(r"_sim_(\d+)\.csv$")


def resolve_main_checkout(start: Path) -> Path:
    """The main checkout's root, via `git rev-parse --git-common-dir` -- the same resolution
    guard-oracle.sh uses for the touch-goldens marker (docs/lessons.md's 2026-09-16 entry): relative
    (".git") from the main checkout itself, absolute from a worktree."""
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


@dataclass
class Participant:
    sim: int
    times: list[float]
    columns: dict[str, list[float]]  # raw CSV values, NOT yet unit-converted


def load_participant(path: Path) -> Participant:
    """Reads one Atmos_NN_sim_MM.csv, raw (no unit conversion, no channel filtering). Detects a
    duplicate column name (only sim_05's feVelocity_ft_s_Z, per this file's own header comment) and
    asserts every duplicate's values agree exactly -- ADR-0013's "sim_05 duplicate-column assert" --
    rather than silently keeping whichever csv.DictReader would overwrite to."""
    match = SIM_FILE_RE.search(path.name)
    if match is None:
        raise SystemExit(f"{path}: filename does not match *_sim_NN.csv")
    sim = int(match.group(1))

    with path.open(newline="", encoding="utf-8") as f:
        reader = csv.reader(f)
        header = next(reader)
        # Map each column name to every index it appears at (sim_05 duplicates feVelocity_ft_s_Z).
        indices_by_name: dict[str, list[int]] = {}
        for i, name in enumerate(header):
            indices_by_name.setdefault(name, []).append(i)

        times: list[float] = []
        columns: dict[str, list[float]] = {name: [] for name in indices_by_name}
        for row in reader:
            times.append(float(row[0]))
            for name, idxs in indices_by_name.items():
                values = [float(row[i]) for i in idxs]
                if len(values) > 1 and max(values) - min(values) != 0.0:
                    raise SystemExit(
                        f"{path}: duplicate column {name!r} disagrees at time {row[0]} "
                        f"({values}) -- not silently picking one"
                    )
                columns[name].append(values[0])

    return Participant(sim=sim, times=times, columns=columns)


def nearest_index(times: list[float], target: float) -> int:
    """Index of the row whose time is closest to target, asserting the match is within
    NEAREST_SAMPLE_TOLERANCE_S (ADR-0013). O(n) linear scan -- these files are at most 3001
    rows, and this runs at most 301 * 6 * 10 times total, trivially fast even unoptimized."""
    best_i = 0
    best_diff = abs(times[0] - target)
    for i, t in enumerate(times):
        diff = abs(t - target)
        if diff < best_diff:
            best_i, best_diff = i, diff
    if best_diff > NEAREST_SAMPLE_TOLERANCE_S:
        raise SystemExit(
            f"No sample within {NEAREST_SAMPLE_TOLERANCE_S} s of t={target}: nearest is "
            f"{best_diff} s away"
        )
    return best_i


def wrap_to_pi(angle_rad: float) -> float:
    """Shortest signed angular distance representation of angle_rad, in (-pi, pi]."""
    wrapped = math.fmod(angle_rad + math.pi, 2.0 * math.pi)
    if wrapped <= 0.0:
        wrapped += 2.0 * math.pi
    return wrapped - math.pi


def excluded(channel: Channel, case: Case, sim: int, sample_index: int) -> bool:
    """Vol II Tables 77/78's participant exclusions plus the t=0 airspeed exclusion (module header
    comment has the full citation and reasoning for each rule below)."""
    if sim == 2 and (channel.key in EULER_KEYS or channel.key in MOMENT_KEYS):
        return True  # Table 78 footnotes a, d: every case
    if sim == 2 and channel.key == "force_x" and case.num in (4, 5):
        return True  # Table 78 footnote b
    if sim == 3 and channel.key == "force_y" and case.num in (6, 7, 8):
        return True  # Table 78 footnote c
    if sample_index == 0 and channel.key in AIRSPEED_DERIVED_KEYS:
        if sim == 6 and case.num in (7, 8):
            return True  # PLAN.md's own t=0 addition: both wind cases
        if sim == 3 and case.num == 8:
            return True  # PLAN.md's own t=0 addition: case 8 only
    return False


@dataclass
class ChannelResult:
    reference: list[float] = field(default_factory=list)  # per sample, SI
    tol_abs: float = 0.0
    tol_rel: float = 0.0


def omit_relative_term(mean_t: float, tol_abs: float) -> bool:
    """A sample's relative-spread contribution is excluded from tol_rel's max-over-t when the
    channel's own mean at that instant is small relative to its already-computed absolute
    tolerance -- the general form of the yaw-wrap fix's own reasoning (module header comment),
    applied to every channel: a near-zero crossing (a force changing sign, a rate passing
    through zero) would otherwise let one instant's tiny denominator produce an arbitrarily
    large relative spread that, because tol_rel is a single scalar taking the max over every
    sample, would poison the whole case's tol_rel for every other sample too. tol_abs itself
    (not an arbitrary epsilon) is the natural scale to compare against: below it, the absolute
    term already dominates the pass criterion regardless of what the relative term would
    contribute."""
    return abs(mean_t) <= tol_abs


def compute_channel(
    channel: Channel, case: Case, participants: list[Participant], sample_times: list[float]
) -> ChannelResult:
    result = ChannelResult()
    # sample_index -> list of (sim, converted_value) for every contributing participant.
    contributions: list[list[tuple[int, float]]] = []
    for sample_index, t in enumerate(sample_times):
        contributing: list[tuple[int, float]] = []
        for p in participants:
            if channel.column not in p.columns:
                continue
            if excluded(channel, case, p.sim, sample_index):
                continue
            row = nearest_index(p.times, t)
            contributing.append((p.sim, p.columns[channel.column][row] * channel.scale))
        if len(contributing) < 2:
            raise SystemExit(
                f"case {case.num} channel {channel.key} sample {sample_index}: fewer than 2 "
                f"contributing participants ({contributing})"
            )
        contributions.append(contributing)

    if channel.is_yaw:
        spreads: list[float] = []
        for contributing in contributions:
            ref0 = contributing[0][1]
            diffs = [wrap_to_pi(value - ref0) for _, value in contributing]
            mean_diff = sum(diffs) / len(diffs)
            result.reference.append(wrap_to_pi(ref0 + mean_diff))
            spreads.append(max(diffs) - min(diffs))
        tol_abs_raw = K * max(spreads)
        result.tol_abs = max(tol_abs_raw, FLOOR_ABS[channel.kind])
        result.tol_rel = 0.0  # never for yaw -- module header comment has the full reasoning
        return result

    means: list[float] = []
    spreads: list[float] = []
    for contributing in contributions:
        values = [value for _, value in contributing]
        mean_t = sum(values) / len(values)
        means.append(mean_t)
        spreads.append(max(values) - min(values))
    result.reference = means

    tol_abs_raw = K * max(spreads)
    result.tol_abs = max(tol_abs_raw, FLOOR_ABS[channel.kind])

    # Second pass, now that tol_abs (the threshold omit_relative_term compares against) is
    # final: a sample whose mean is small relative to tol_abs contributes nothing to tol_rel
    # (omit_relative_term's own comment has the full reasoning) -- this also naturally covers
    # the exactly-zero-mean case (e.g. a channel with zero spread AND zero mean throughout,
    # like case 1's latitude), which would otherwise divide by zero.
    relative_spreads = [
        spread_t / abs(mean_t)
        for mean_t, spread_t in zip(means, spreads, strict=True)
        if not omit_relative_term(mean_t, result.tol_abs)
    ]
    tol_rel_raw = K * max(relative_spreads) if relative_spreads else 0.0
    result.tol_rel = max(tol_rel_raw, FLOOR_REL.get(channel.kind, 0.0))
    return result


def process_case(case: Case, refs_root: Path) -> dict[str, ChannelResult]:
    folder = refs_root / case.folder
    paths = sorted(folder.glob(f"Atmos_{case.num:02d}_sim_*.csv"))
    if not paths:
        raise SystemExit(f"No sim files found under {folder}")
    participants = [load_participant(p) for p in paths]
    sample_times = [i * SAMPLE_DT for i in range(SAMPLE_COUNT)]

    results: dict[str, ChannelResult] = {}
    for channel in CHANNELS:
        results[channel.key] = compute_channel(channel, case, participants, sample_times)
    return results


def check_angle_bands(case: Case, results: dict[str, ChannelResult]) -> None:
    """A generator-side sanity gate (spec R7): no angle channel's tol_abs may exceed pi rad --
    a band that wide is non-discriminating (ADR-0013's own concern) and, for yaw specifically,
    would mean the wrap fix above has a defect, since yaw's tol_rel is always 0 and there is
    nothing else that could inflate its effective tolerance past tol_abs alone."""
    for key in ("latitude", "longitude", "yaw", "pitch", "roll"):
        tol_abs = results[key].tol_abs
        if tol_abs > math.pi:
            raise SystemExit(
                f"case {case.num} channel {key}: tol_abs {tol_abs} rad exceeds pi -- "
                "non-discriminating band, investigate before trusting it"
            )


def check_non_discriminating(case: Case, results: dict[str, ChannelResult]) -> None:
    """ADR-0013: "A band wide enough to admit an obviously wrong answer is not a test; such
    channels are recorded as non-discriminating rather than silently counted as passes." This
    check was never actually implemented until a Codex adversarial review (2026-09-19) found a
    real instance by hand: case04/case05's force_y band, once tol_rel is added to tol_abs, accepts
    a comparison against a completely missing (all-zero) force signal at every one of its 301
    samples, despite the channel's own reference reaching -24.8 N -- exactly the "obviously wrong
    answer" ADR-0013 already anticipated, just never machine-checked for.

    A channel is flagged when its own combined tolerance (tol_abs + tol_rel*|reference|) would
    accept a value of exactly zero at EVERY sample, specifically because tol_rel is doing the
    rescuing: the channel's peak |reference| magnitude must itself exceed tol_abs ALONE by a clear
    margin (2x) -- i.e. tol_abs by itself would already have correctly rejected a zero comparison
    at that sample, and only adding the relative term saves it. This is deliberately narrower than
    "peak clears its own floor": a channel whose peak never even exceeds its own tol_abs (e.g.
    case04/case05's v_north, contaminated by a known participant artifact -- see docs/plans/
    nesc-harness.md, a different, already-disclosed failure mode) was never going to reject zero
    regardless of tol_rel, so flagging it here would conflate two distinct problems. force_y's own
    peak of 24.8 N against its own tol_abs of 9.9 N (~2.5x) is exactly the case this check exists
    for. Report-only (ADR-0013 says "recorded", not "rejected"): a currently-skipped case
    (cases 4/5) still needs its band generated so a future fix can be judged against it, so this
    must not block generation the way check_angle_bands' stricter, narrower guarantee does."""
    for channel in CHANNELS:
        r = results[channel.key]
        peak = max(abs(v) for v in r.reference)
        if peak <= r.tol_abs * 2.0:
            continue
        accepts_zero = all(abs(v) <= r.tol_abs + r.tol_rel * abs(v) for v in r.reference)
        if accepts_zero:
            print(
                f"# case {case.num:>2} {channel.key:<12} NON-DISCRIMINATING: peak |reference| "
                f"{peak:.3e} but tol_abs={r.tol_abs:.3e} tol_rel={r.tol_rel:.3e} would accept "
                "an all-zero signal at every sample -- ADR-0013's own concern, recorded not "
                "rejected (this channel/case may already be a disclosed gap; see docs/plans/"
                "nesc-harness.md)",
                file=sys.stderr,
            )


def report(case: Case, results: dict[str, ChannelResult]) -> None:
    """Per-channel band width / floor-margin report (stderr; stdout is reserved for --print-header),
    mirroring generate_isa1976_table.py's check_transcribed_row_tolerances -- a future regeneration
    that moves a channel toward its own floor is a visible signal, not silently absorbed."""
    for channel in CHANNELS:
        r = results[channel.key]
        floor = FLOOR_ABS[channel.kind] if channel.kind != "atmosphere" else FLOOR_REL[channel.kind]
        value = r.tol_abs if channel.kind != "atmosphere" else r.tol_rel
        ratio = value / floor if floor > 0 else float("inf")
        print(
            f"# case {case.num:>2} {channel.key:<12} tol_abs={r.tol_abs:.6e} "
            f"tol_rel={r.tol_rel:.6e} floor_ratio={ratio:.3f}",
            file=sys.stderr,
        )


def render_case_header(case: Case, results: dict[str, ChannelResult]) -> str:
    const_names = {
        "latitude": "kLatitude",
        "longitude": "kLongitude",
        "altitude": "kAltitude",
        "v_north": "kVNorth",
        "v_east": "kVEast",
        "v_down": "kVDown",
        "yaw": "kYaw",
        "pitch": "kPitch",
        "roll": "kRoll",
        "rate_p": "kRateP",
        "rate_q": "kRateQ",
        "rate_r": "kRateR",
        "force_x": "kForceX",
        "force_y": "kForceY",
        "force_z": "kForceZ",
        "moment_l": "kMomentL",
        "moment_m": "kMomentM",
        "moment_n": "kMomentN",
        "density": "kDensity",
        "pressure": "kPressure",
        "temperature": "kTemperature",
    }
    band_field = {
        "latitude": "latitude",
        "longitude": "longitude",
        "altitude": "altitude",
        "v_north": "v_north",
        "v_east": "v_east",
        "v_down": "v_down",
        "yaw": "yaw",
        "pitch": "pitch",
        "roll": "roll",
        "rate_p": "rate_p",
        "rate_q": "rate_q",
        "rate_r": "rate_r",
        "force_x": "force_x",
        "force_y": "force_y",
        "force_z": "force_z",
        "moment_l": "moment_l",
        "moment_m": "moment_m",
        "moment_n": "moment_n",
        "density": "density",
        "pressure": "pressure",
        "temperature": "temperature",
    }

    constants = []
    for channel in CHANNELS:
        r = results[channel.key]
        reference = ", ".join(repr(v) for v in r.reference)
        constants.append(
            f"inline constexpr ChannelBand {const_names[channel.key]} = {{\n"
            f"    {{{reference}}},\n"
            f"    {r.tol_abs!r},\n"
            f"    {r.tol_rel!r},\n"
            f"}};"
        )
    constants_block = "\n\n".join(constants)

    band_fields = ",\n".join(
        f"    .{band_field[channel.key]} = {const_names[channel.key]}" for channel in CHANNELS
    )

    return f"""\
// NASA/TM-2015-218675 atmospheric check-case {case.num} ({case.folder}) oracle band (ADR-0013,
// ADR-0014, PLAN.md M2 PR 3b). Human-owned: lives under tests/golden/ so guard-oracle.sh blocks
// agent edits outside a session Joe armed. Generated by tests/golden/nesc/generate_nesc_band.py
// -- do not hand-edit; re-run that script after any change to its constants or exclusion rules
// and commit its output. reference is the per-sample mean across the case's own contributing
// NESC simulation tools (Vol II Tables 77/78's exclusions applied); tol_abs/tol_rel are each a
// single scalar per channel, the worst case over every sample time (ADR-0013), floored per
// ADR-0014. See tests/golden/nesc/nesc_band_common.hpp for the ChannelBand/CaseBand schema this
// data fills in.
// NOLINTBEGIN(modernize-use-std-numbers): a data table; a real six-tool ensemble mean can land
// within clang-tidy's tolerance of a named math constant (e.g. a value near std::numbers::sqrt2)
// purely by coincidence, the same reasoning tests/golden/atmosphere/isa1976_table.hpp already
// gives for its own generated table.
#pragma once

#include "golden/nesc/nesc_band_common.hpp"

namespace astra::test::nesc::case{case.num:02d} {{

{constants_block}

inline constexpr CaseBand kBand = {{
{band_fields},
}};

}}  // namespace astra::test::nesc::case{case.num:02d}
// NOLINTEND(modernize-use-std-numbers)
"""


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--refs", type=Path, default=None, help="Atmospheric_checkcases directory")
    args = parser.parse_args()

    script_dir = Path(__file__).resolve().parent
    if args.refs is not None:
        refs_root = args.refs
    else:
        main_checkout = resolve_main_checkout(script_dir)
        refs_root = main_checkout / "docs" / "refs" / "Atmospheric_checkcases"
    if not refs_root.is_dir():
        raise SystemExit(
            f"{refs_root} does not exist -- gitignored refs are not present in a worktree "
            "(docs/refs/README.md); pass --refs pointing at the main checkout"
        )

    for case in CASES:
        results = process_case(case, refs_root)
        check_angle_bands(case, results)
        check_non_discriminating(case, results)
        report(case, results)
        out_path = script_dir / f"case{case.num:02d}_{case.slug}_band.hpp"
        out_path.write_text(render_case_header(case, results), encoding="utf-8")
        print(f"# wrote {out_path}", file=sys.stderr)


if __name__ == "__main__":
    main()
