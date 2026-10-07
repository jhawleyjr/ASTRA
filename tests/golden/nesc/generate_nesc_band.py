"""Independent generator for tests/golden/nesc/case{01..10}_<slug>_band.hpp (ADR-0013,
ADR-0014, ADR-0015, PLAN.md M2 PR 3b) and for the F-16 cases' case{11,12,13p1..13p4,15,16}_<slug>
_band.hpp (ADR-0028, M7 PR 8). Human-owned, like the headers it writes (tests/golden/**,
guard-oracle.sh): re-run after any change to the constants or exclusion rules below, never
hand-edit the output. Follows tests/golden/atmosphere/generate_isa1976_table.py's shape: pure
stdlib Python, every constant cited, self-checks that raise SystemExit on their own detected
inconsistency before ever emitting output.

Reads NASA/TM-2015-218675's raw check-case CSVs (docs/refs/Atmospheric_checkcases/, ~82 MB,
gitignored, never committed) and reduces them to eighteen committed headers -- the band, not
the ensemble (ADR-0013's own framing). This script is never run by `just check`;
`tests/golden/nesc/` is guarded by guard-oracle.sh and needs `touch-goldens` armed to write
anything, including this file itself.

Run (from a worktree, refs read from the main checkout -- gitignored files do not exist inside
a git worktree, docs/refs/README.md's own note):
    python3 tests/golden/nesc/generate_nesc_band.py [--refs PATH] 2> report.txt
    clang-format-18 -i tests/golden/nesc/case*_band.hpp
The report (stderr) is the record of each regeneration's floor ratios, zero-accepting channels,
matching offsets and, for cases 11 to 16, SIM 2's ratio (ADR-0028 item 8); it goes in the PR body.
--refs defaults to the main checkout's docs/refs/Atmospheric_checkcases, resolved via
`git rev-parse --git-common-dir` the same way guard-oracle.sh resolves the touch-goldens marker.

Oracle formula (ADR-0013, elaborated by ADR-0014's floor and ADR-0015's removal of the relative
term -- see docs/decisions/0013-nesc-check-case-oracle.md, 0014-nesc-per-channel-tolerance-floor.md,
0015-nesc-tol-rel-drop.md):
    reference_t = mean across participating sims, PER SAMPLE TIME t
    tol_abs     = k * max over t of (max - min across sims)     -- ONE scalar per channel/case
    tol_abs     = max(tol_abs, ADR-0014's per-channel-kind floor; ADR-0015 converts the
                  atmosphere-kind floor from a relative factor to an absolute one -- see
                  FLOOR_ABS's own comment)
    pass        : |astra - reference_t| <= tol_abs
    k = 3

ADR-0013 originally paired tol_abs with a tol_rel term (`k * max over t of ((max-min)/|mean|)`,
omitted where |mean| was small relative to tol_abs); ADR-0015 drops tol_rel entirely, for every
channel including yaw (which never carried one in the first place -- see below): tol_abs alone
already bounds the ensemble's worst absolute disagreement at every sample time, so a relative
term only ever added slack, and -- being a single case-wide scalar -- added the most slack on
channels whose reference passes near zero at some point in the run. See ADR-0015 for the
measured percentages and the two cases (force_y/force_z in cases 4/5) where that slack was not
merely loose but genuinely non-discriminating (traced there to SIM 2 contamination, not a
property of the pass criterion itself -- see the cases-4/5 exclusion below).

Yaw is the one channel this data set's trajectories actually cross the +/-180 deg wrap
boundary on (cases 2, 4, 5 -- Joe's spec review, confirmed against the raw CSVs this session).
A raw (non-circular) spread at a wrap instant is ~359 deg, which would make ADR-0013's
max_t(spread) formula vacuous for the whole case. Fix: yaw's per-sample mean and spread are
computed on the UNWRAPPED difference between each tool's raw value and a fixed reference tool
(mod 2*pi, mapped to the shortest signed angular distance via wrap_to_pi()), never on the raw
degree values directly -- see compute_channel()'s own wrapped branch.

Vol II Tables 77/78 (doc p.591-592) participant exclusions, honored here, not re-litigated:
  - SIM 2's Euler angles (footnote a: "large integration errors") and aero moments (footnote
    d: "reported at the MRC, not the CM") are excluded in EVERY case.
  - SIM 2 is excluded ENTIRELY (every channel) in cases 4 and 5 (ADR-0015, this session's own
    data-driven finding -- not itself a NASA footnote, though it extends and is consistent with
    footnote b's "unexplained X-body force and local gravity differences": SIM 2 diverges on
    nearly every compared channel in these two round-Earth cases specifically, not only X-body
    force. See ADR-0015 for the full survey and the reasoning against excluding it channel by
    channel instead of case-wide.)
  - SIM 3's Y-body force is excluded in cases 6, 7, and 8 only (footnote c: "unexplained
    non-zero values in Y-body force").
Plus a t=0-only exclusion (PLAN.md's own addition, this session): SIM 6 is excluded at t=0
from every aero force/moment channel in BOTH wind cases (7 and 8); SIM 3 is excluded at t=0
from the same channels in case 8 only. Both publish a physically-wrong zero relative airspeed
at their own first sample in a wind case -- case 8's Fy band alone would widen 1.7x if that one
bad instant were left in, since tol_abs is a single scalar taking the max over every sample.

The F-16 cases 11, 12, 13.1 to 13.4, 15 and 16 follow ADR-0028, which supersedes ADR-0013 in part
for those cases only; cases 1 to 10 keep everything above, and regenerate byte-identical:
  - Members: SIM 4 and SIM 5. SIM 2 is excluded for every channel by one rule (Vol II Tables 79/80,
    footnote b), and its distance from the band is reported, never gated: the max over gated samples
    of |sim2 - reference| / tol_abs. Footnotes (c) to (f) exclude nothing; an unfootnoted "data
    provided but not included in calculation" cell is not an exclusion (Table 78's precedent).
  - Durations: Vol II's own (180 s for 11, 12, 15, 16; 20 s for 13.1, 13.2; 30 s for 13.3; 60 s
    for 13.4); a member's rows beyond the duration are ignored, a missing grid instant is an error.
  - Matching: a row matches t = k/10 s within 1e-5 s (sim_05's times are float32).
  - Window: tol_abs's max runs over the gated samples, t >= 8 s (footnote a), first gated sample 80.
  - Wrap: longitude takes yaw's circular mean and wrapped spread (cases 15 and 16 cross +/-180 deg).
  - Report: each channel's floor ratio, zero-accepting status (over gated samples) and SIM 2 ratio,
    and each file's largest matching offset and duplicate columns. The report is the record of
    the generated values (ADR-0028 item 8); it goes in the PR body.
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

# ADR-0028 (cases 11, 12, 13.1 to 13.4, 15 and 16 only; ADR-0013 keeps cases 1 to 10 exactly):
F16_MATCH_TOLERANCE_S = 1e-5  # item 4: float32's half-ulp bound below 256 s is 7.6e-6 s
F16_FIRST_GATED_SAMPLE = 80  # item 5: t >= 8 s (Vol II Tables 79/80, footnote a)
F16_FILE_SIMS = frozenset({2, 4, 5})  # Context: each F-16 case has exactly these three files
F16_MEMBERS = frozenset({4, 5})  # item 1: SIM 2 excluded by footnote (b), for every channel
F16_WRAPPED = frozenset({"yaw", "longitude"})  # item 6: cases 15 and 16 cross +/-180 deg

# ADR-0014's per-channel-kind floor on tol_abs, extended by ADR-0015 to density/pressure/
# temperature. ADR-0014 originally floored those three on tol_rel (a single relative factor,
# 1e-12, scaling naturally with each channel's own instantaneous |reference|); ADR-0015 drops
# tol_rel entirely, so that factor is converted to an absolute floor by multiplying it against
# each channel's own ISA-76 sea-level value instead (the fixed scale "the maximum these
# quantities reach in atmospheric flight" -- no altitude ceiling needed). Sources:
# tests/golden/atmosphere/generate_isa1976_table.py's own SEA_LEVEL_TEMPERATURE_K (288.15 K) /
# SEA_LEVEL_PRESSURE_PA (101325.0 Pa), and tests/golden/atmosphere/isa1976_table.hpp's
# transcribed sea-level density row (1.225 kg/m^3, cross-checked there against a freshly
# computed 1.2249991558877122 kg/m^3 within that file's own +/-0.0005 kg/m^3 tolerance).
# Density/pressure/temperature each need their own floor value (unlike the other kinds, which
# share one floor per kind), so each gets its own dedicated key rather than sharing "atmosphere".
# Verified (this session, against every currently-committed tol_abs, including cases 4/5's own
# post-SIM-2-exclusion values): none of these floors bind anywhere in this data -- the smallest
# margin is ~7.9e6x (case02 temperature). Speed of sound is not an emitted NESC channel (no
# Channel/ChannelBand entry exists for it) despite ADR-0014's Decision table listing a floor for
# it; that is a pre-existing, harmless inconsistency in that accepted table, not fixed here.
FLOOR_ABS = {
    "angle": 1e-12,  # rad
    "length": 1e-9,  # m
    "velocity": 1e-9,  # m/s
    "rate": 1e-12,  # rad/s
    "force": 1e-12,  # N or N.m
    "density": 1e-12 * 1.225,  # kg/m^3 (1e-12 x ISA-76 sea-level density)
    "pressure": 1e-12 * 101_325.0,  # Pa (1e-12 x ISA-76 sea-level pressure)
    "temperature": 1e-12 * 288.15,  # K (1e-12 x ISA-76 sea-level temperature)
}


@dataclass(frozen=True)
class Channel:
    key: str  # ChannelBand constant name suffix, e.g. "latitude" -> kLatitude
    column: str  # CSV header name
    scale: float  # multiply the raw CSV value by this to get SI
    kind: str  # FLOOR_ABS key


# Every channel's ChannelBand constant name in the generated headers.
CONST_NAMES = {
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

CHANNELS: list[Channel] = [
    Channel("latitude", "latitude_deg", DEG_TO_RAD, "angle"),
    Channel("longitude", "longitude_deg", DEG_TO_RAD, "angle"),
    Channel("altitude", "altitudeMsl_ft", FT_TO_M, "length"),
    Channel("v_north", "feVelocity_ft_s_X", FT_TO_M, "velocity"),
    Channel("v_east", "feVelocity_ft_s_Y", FT_TO_M, "velocity"),
    Channel("v_down", "feVelocity_ft_s_Z", FT_TO_M, "velocity"),
    Channel("yaw", "eulerAngle_deg_Yaw", DEG_TO_RAD, "angle"),
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
    Channel("density", "airDensity_slug_ft3", SLUG_FT3_TO_KG_M3, "density"),
    Channel("pressure", "ambientPressure_lbf_ft2", LBF_FT2_TO_PA, "pressure"),
    Channel("temperature", "ambientTemperature_dgR", RANKINE_TO_KELVIN, "temperature"),
]

EULER_KEYS = {"yaw", "pitch", "roll"}
MOMENT_KEYS = {"moment_l", "moment_m", "moment_n"}
AIRSPEED_DERIVED_KEYS = {"force_x", "force_y", "force_z", "moment_l", "moment_m", "moment_n"}


@dataclass(frozen=True)
class Case:
    """One check case. The defaults are cases 1 to 10's (ADR-0013 to ADR-0015): 30 s at 10 Hz,
    every sample gated, 1e-6 s matching, yaw the only wrapped channel, Tables 77/78's exclusions.
    Each F-16 case overrides them with ADR-0028's (f16_case below)."""

    num: int
    folder: str
    slug: str
    tag: str = ""  # file and namespace tag ("13p1"); empty means f"{num:02d}"
    label: str = ""  # report label ("13.1"); empty means str(num)
    duration_s: int = 30
    sample_count: int = SAMPLE_COUNT
    first_gated_sample: int = 0
    match_tolerance_s: float = NEAREST_SAMPLE_TOLERANCE_S
    wrapped: frozenset[str] = frozenset({"yaw"})
    f16: bool = False

    @property
    def file_tag(self) -> str:
        return self.tag or f"{self.num:02d}"

    @property
    def name(self) -> str:
        return self.label or str(self.num)


def f16_case(
    num: int, tag: str, label: str, folder: str, slug: str, duration_s: int, sample_count: int
) -> Case:
    """An F-16 case under ADR-0028: the window, matching, wrap and members above."""
    return Case(
        num=num,
        folder=folder,
        slug=slug,
        tag=tag,
        label=label,
        duration_s=duration_s,
        sample_count=sample_count,
        first_gated_sample=F16_FIRST_GATED_SAMPLE,
        match_tolerance_s=F16_MATCH_TOLERANCE_S,
        wrapped=F16_WRAPPED,
        f16=True,
    )


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

# ADR-0028 item 3: each case runs for its Vol II duration (Tables 35 to 40, 42 and 43); a member's
# rows beyond it are ignored. The sample counts are ADR-0028's own (1801, 201, 301, 601), stated
# here and checked against the duration by check_case_shape().
F16_CASES: list[Case] = [
    f16_case(11, "11", "11", "Atmos_11_TrimCheckSubsonicF16", "trim_check_subsonic_f16", 180, 1801),
    f16_case(
        12, "12", "12", "Atmos_12_TrimCheckSupersonicF16", "trim_check_supersonic_f16", 180, 1801
    ),
    f16_case(
        13,
        "13p1",
        "13.1",
        "Atmos_13p1_SubsonicAltitudeChangeF16",
        "subsonic_altitude_change_f16",
        20,
        201,
    ),
    f16_case(
        13,
        "13p2",
        "13.2",
        "Atmos_13p2_SubsonicAirspeedChangeF16",
        "subsonic_airspeed_change_f16",
        20,
        201,
    ),
    f16_case(
        13,
        "13p3",
        "13.3",
        "Atmos_13p3_SubsonicHeadingChangeF16",
        "subsonic_heading_change_f16",
        30,
        301,
    ),
    f16_case(
        13,
        "13p4",
        "13.4",
        "Atmos_13p4_SubsonicLateralSideStepF16",
        "subsonic_lateral_side_step_f16",
        60,
        601,
    ),
    f16_case(15, "15", "15", "Atmos_15_CircleNorthPole", "circle_north_pole", 180, 1801),
    f16_case(
        16, "16", "16", "Atmos_16_CircleEquatorDateLine", "circle_equator_date_line", 180, 1801
    ),
]


def check_case_shape(case: Case) -> None:
    """Self-check: the sample count is the 10 Hz grid over the duration, both ends included, and the
    first gated sample lies inside it."""
    if case.sample_count != case.duration_s * 10 + 1:
        raise SystemExit(
            f"case {case.name}: sample count {case.sample_count} is not "
            f"10 x {case.duration_s} s + 1"
        )
    if not 0 <= case.first_gated_sample < case.sample_count:
        raise SystemExit(
            f"case {case.name}: first gated sample {case.first_gated_sample} out of range"
        )


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
    duplicates: dict[str, int] = field(default_factory=dict)  # name -> copies, checked equal


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

    duplicates = {name: len(idxs) for name, idxs in indices_by_name.items() if len(idxs) > 1}
    return Participant(sim=sim, times=times, columns=columns, duplicates=duplicates)


def nearest_index(times: list[float], target: float, tolerance_s: float) -> int:
    """Index of the row whose time is closest to target, asserting the match is within
    tolerance_s (ADR-0013's 1e-6 s for cases 1 to 10; ADR-0028 item 4's 1e-5 s for 11 to 16). A
    member missing a grid instant is therefore a generator error. O(n) linear scan; match_rows()
    calls it once per participant and sample, not once per channel."""
    best_i = 0
    best_diff = abs(times[0] - target)
    for i, t in enumerate(times):
        diff = abs(t - target)
        if diff < best_diff:
            best_i, best_diff = i, diff
    if best_diff > tolerance_s:
        raise SystemExit(
            f"No sample within {tolerance_s} s of t={target}: nearest is {best_diff} s away"
        )
    return best_i


def match_rows(p: Participant, sample_times: list[float], tolerance_s: float) -> list[int]:
    """Each sample's matched row in p, computed once per case and shared by every channel. The
    sample times stop at the case's duration, so rows beyond it are never selected (ADR-0028
    item 3)."""
    return [nearest_index(p.times, t, tolerance_s) for t in sample_times]


def wrap_to_pi(angle_rad: float) -> float:
    """Shortest signed angular distance representation of angle_rad, in (-pi, pi]."""
    wrapped = math.fmod(angle_rad + math.pi, 2.0 * math.pi)
    if wrapped <= 0.0:
        wrapped += 2.0 * math.pi
    return wrapped - math.pi


def excluded(channel: Channel, case: Case, sim: int, sample_index: int) -> bool:
    """Vol II Tables 77/78's participant exclusions plus the t=0 airspeed exclusion and ADR-0015's
    cases-4/5 exclusion (module header comment has the full citation and reasoning for each rule
    below). Cases 11 to 16 follow ADR-0028 instead; none of Tables 77/78's rules apply to them."""
    if case.f16:
        # ADR-0028 item 1: SIM 2 is excluded for every channel by one participant rule (Vol II
        # Tables 79/80, footnote b, "large trim differences"). Footnotes (c) to (f) exclude nothing,
        # and an unfootnoted "data provided but not included" cell is not an exclusion (item 2).
        return sim == 2
    if sim == 2 and case.num in (4, 5):
        return True  # ADR-0015: this session's own data-driven finding -- SIM 2 diverges broadly
        # across nearly every channel in these two round-Earth cases (altitude, v_down, rates,
        # position, atmosphere, and the aero force channels already known from footnote b), not
        # only X-body force. Not a NASA footnote itself, but consistent with and extending Table
        # 78 footnote b's own "local gravity differences" language. Subsumes and replaces the
        # previous footnote-b force_x-only rule for these two cases (that rule applied to cases
        # 4/5 only, so nothing outside this blanket exclusion is lost by deleting it): "SIM 2's
        # X-body force is excluded in cases 4 and 5 only (footnote b: 'unexplained X-body force
        # and local gravity differences')".
    if sim == 2 and (channel.key in EULER_KEYS or channel.key in MOMENT_KEYS):
        return True  # Table 78 footnotes a, d: every case
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
    sim2_ratio: float | None = None  # cases 11 to 16 only (ADR-0028 item 1); reported, never gated


def compute_channel(
    channel: Channel,
    case: Case,
    participants: list[Participant],
    rows: dict[int, list[int]],
) -> ChannelResult:
    result = ChannelResult()
    # sample_index -> list of (sim, converted_value) for every contributing participant.
    contributions: list[list[tuple[int, float]]] = []
    for sample_index in range(case.sample_count):
        contributing: list[tuple[int, float]] = []
        for p in participants:
            if channel.column not in p.columns:
                continue
            if excluded(channel, case, p.sim, sample_index):
                continue
            row = rows[p.sim][sample_index]
            contributing.append((p.sim, p.columns[channel.column][row] * channel.scale))
        if len(contributing) < 2:
            raise SystemExit(
                f"case {case.name} channel {channel.key} sample {sample_index}: fewer than 2 "
                f"contributing participants ({contributing})"
            )
        if case.f16 and {sim for sim, _ in contributing} != F16_MEMBERS:
            raise SystemExit(
                f"case {case.name} channel {channel.key} sample {sample_index}: contributors "
                f"{sorted(sim for sim, _ in contributing)} are not ADR-0028's SIM 4 and SIM 5"
            )
        contributions.append(contributing)

    # ADR-0013's max over t runs over every sample in cases 1 to 10 (first gated sample 0) and over
    # the gated window in 11 to 16 (ADR-0028 item 5); the reference covers every sample either way.
    gated = case.first_gated_sample
    if channel.key in case.wrapped:
        spreads: list[float] = []
        for contributing in contributions:
            ref0 = contributing[0][1]
            diffs = [wrap_to_pi(value - ref0) for _, value in contributing]
            mean_diff = sum(diffs) / len(diffs)
            result.reference.append(wrap_to_pi(ref0 + mean_diff))
            spreads.append(max(diffs) - min(diffs))
        tol_abs_raw = K * max(spreads[gated:])
        result.tol_abs = max(tol_abs_raw, FLOOR_ABS[channel.kind])
        return result

    means: list[float] = []
    spreads: list[float] = []
    for contributing in contributions:
        values = [value for _, value in contributing]
        mean_t = sum(values) / len(values)
        means.append(mean_t)
        spreads.append(max(values) - min(values))
    result.reference = means

    tol_abs_raw = K * max(spreads[gated:])
    result.tol_abs = max(tol_abs_raw, FLOOR_ABS[channel.kind])
    return result


def sim2_ratio(
    channel: Channel, case: Case, sim2: Participant, rows: list[int], result: ChannelResult
) -> float | None:
    """ADR-0028 item 1: SIM 2's distance from the band, the max over gated samples of
    |sim2 - reference| / tol_abs, on the wrapped difference for yaw and longitude. None when SIM 2
    does not publish the channel."""
    if channel.column not in sim2.columns:
        return None
    worst = 0.0
    for sample_index in range(case.first_gated_sample, case.sample_count):
        value = sim2.columns[channel.column][rows[sample_index]] * channel.scale
        diff = value - result.reference[sample_index]
        if channel.key in case.wrapped:
            diff = wrap_to_pi(diff)
        worst = max(worst, abs(diff))
    return worst / result.tol_abs


@dataclass
class CaseInputs:
    participants: list[Participant]
    rows: dict[int, list[int]]  # sim -> matched row per sample
    offsets: dict[int, float]  # sim -> largest |row time - k/10| over the case's samples


def process_case(case: Case, refs_root: Path) -> tuple[dict[str, ChannelResult], CaseInputs]:
    check_case_shape(case)
    folder = refs_root / case.folder
    paths = sorted(folder.glob(f"Atmos_{case.file_tag}_sim_*.csv"))
    if not paths:
        raise SystemExit(f"No sim files found under {folder}")
    participants = [load_participant(p) for p in paths]
    if case.f16 and {p.sim for p in participants} != F16_FILE_SIMS:
        raise SystemExit(
            f"case {case.name}: files for sims {sorted(p.sim for p in participants)}, expected "
            f"{sorted(F16_FILE_SIMS)} (ADR-0028's Context)"
        )
    sample_times = [i * SAMPLE_DT for i in range(case.sample_count)]
    rows = {p.sim: match_rows(p, sample_times, case.match_tolerance_s) for p in participants}
    # Reported offsets are measured from the decimal instants k/10 (k / 10 is the double nearest
    # them), as ADR-0028's Context measured them, not from i * SAMPLE_DT, which carries its own
    # rounding (up to ~3e-14 s at 180 s) and would show an exact file as offset.
    offsets = {
        p.sim: max(abs(p.times[row] - k / 10) for k, row in enumerate(rows[p.sim]))
        for p in participants
    }

    results: dict[str, ChannelResult] = {}
    for channel in CHANNELS:
        results[channel.key] = compute_channel(channel, case, participants, rows)
    if case.f16:
        sim2 = next(p for p in participants if p.sim == 2)
        for channel in CHANNELS:
            results[channel.key].sim2_ratio = sim2_ratio(
                channel, case, sim2, rows[2], results[channel.key]
            )
    return results, CaseInputs(participants=participants, rows=rows, offsets=offsets)


def check_angle_bands(case: Case, results: dict[str, ChannelResult]) -> None:
    """A generator-side sanity gate (spec R7): no angle channel's tol_abs may exceed pi rad --
    a band that wide is non-discriminating (ADR-0013's own concern) and, for yaw specifically,
    would mean the wrap fix above has a defect, since ADR-0015 leaves no relative term that could
    inflate its effective tolerance past tol_abs alone."""
    for key in ("latitude", "longitude", "yaw", "pitch", "roll"):
        tol_abs = results[key].tol_abs
        if tol_abs > math.pi:
            raise SystemExit(
                f"case {case.name} channel {key}: tol_abs {tol_abs} rad exceeds pi -- "
                "non-discriminating band, investigate before trusting it"
            )


def check_non_discriminating(case: Case, results: dict[str, ChannelResult]) -> None:
    """ADR-0013: "A band wide enough to admit an obviously wrong answer is not a test; such
    channels are recorded as non-discriminating rather than silently counted as passes." A Codex
    adversarial review (2026-09-19) found a real instance by hand under the old tol_abs+tol_rel
    formula: case04/case05's force_y band accepted a comparison against a completely missing
    (all-zero) force signal at every one of its 301 samples, despite the channel's own reference
    reaching -24.8 N. ADR-0015 has since dropped tol_rel entirely (and separately excluded SIM 2
    from cases 4/5 entirely, resolving that specific force_y instance at its source) -- this check
    is retained per that ADR's own decision as a permanent structural-invariant guard, not because
    a live instance remains.

    A channel is flagged when its own tol_abs ALONE would accept a value of exactly zero at EVERY
    sample, despite the channel's peak |reference| magnitude clearing tol_abs by a clear margin
    (2x). With no relative term left to do any "rescuing," this is now a structural tautology: a
    peak exceeding 2*tol_abs already exceeds tol_abs at that same sample, so accepts_zero (which
    requires EVERY sample, including the peak, to satisfy abs(v) <= tol_abs) can never be true
    when the guard condition is met -- provable given tol_abs > 0 for every channel, which
    ADR-0015's atmosphere floor conversion makes a structural guarantee (no channel kind has a
    0.0 floor any more). Kept anyway: it continuously re-verifies that guarantee against whatever
    the generator actually computes, and would immediately flag a future change (a reintroduced
    relative term, a floor lowered to zero) that broke it, rather than relying on this docstring's
    own reasoning being re-derived by a future reader. Report-only, non-fatal, so it never blocks
    generation."""
    for channel in CHANNELS:
        r = results[channel.key]
        peak = max(abs(v) for v in r.reference)
        if peak <= r.tol_abs * 2.0:
            continue
        accepts_zero = all(abs(v) <= r.tol_abs for v in r.reference)
        if accepts_zero:
            print(
                f"# case {case.name:>4} {channel.key:<12} NON-DISCRIMINATING: peak |reference| "
                f"{peak:.3e} but tol_abs={r.tol_abs:.3e} would accept an all-zero signal at "
                "every sample -- ADR-0013's own concern, recorded not rejected",
                file=sys.stderr,
            )


def report_case(case: Case, inputs: CaseInputs) -> None:
    """Per-case provenance (stderr): the rule family, the samples and window, the members, each
    file's largest matching offset and its duplicate columns (each checked equal in every row read,
    load_participant)."""
    if case.f16:
        rule = "ADR-0028"
        members = "sim 4, sim 5; sim 2 excluded for every channel by rule (ADR-0028 item 1)"
    else:
        rule = "ADR-0013 to ADR-0015, Vol II Tables 77/78"
        members = ", ".join(f"sim {p.sim}" for p in inputs.participants)
    first_gated_t = case.first_gated_sample * SAMPLE_DT
    print(
        f"# case {case.name:>4} ({case.folder}): rule {rule}; samples {case.sample_count} "
        f"(0 to {case.duration_s} s); first gated sample {case.first_gated_sample} "
        f"(t >= {first_gated_t:.1f} s); match tolerance {case.match_tolerance_s:g} s; "
        f"wrapped {sorted(case.wrapped)}; members {members}",
        file=sys.stderr,
    )
    for p in inputs.participants:
        duplicates = (
            ", ".join(
                f"{name} x{copies} equal in all {len(p.times)} rows"
                for name, copies in sorted(p.duplicates.items())
            )
            or "none"
        )
        print(
            f"# case {case.name:>4} sim_{p.sim:02d} rows {len(p.times)} "
            f"largest_offset={inputs.offsets[p.sim]:.3e} s duplicate_columns: {duplicates}",
            file=sys.stderr,
        )


def report(case: Case, results: dict[str, ChannelResult]) -> None:
    """Per-channel band width / floor-margin report (stderr; stdout is reserved for --print-header),
    mirroring generate_isa1976_table.py's check_transcribed_row_tolerances -- a future regeneration
    that moves a channel toward its own floor is a visible signal, not silently absorbed.

    ADR-0028 item 8: every regeneration also reports whether each channel is zero-accepting
    (ADR-0015's accepts_zero over the gated samples: tol_abs >= |reference| at every gated sample,
    so an all-zero trajectory would pass) and, for cases 11 to 16, SIM 2's ratio. A zero-accepting
    channel is never cited as evidence of correctness. This sits beside check_non_discriminating(),
    which stays as ADR-0015's structural-invariant check."""
    for channel in CHANNELS:
        r = results[channel.key]
        floor = FLOOR_ABS[channel.kind]
        ratio = r.tol_abs / floor if floor > 0 else float("inf")
        gated = r.reference[case.first_gated_sample :]
        zero_accepting = all(abs(v) <= r.tol_abs for v in gated)
        line = (
            f"# case {case.name:>4} {channel.key:<12} tol_abs={r.tol_abs:.6e} "
            f"floor_ratio={ratio:.3e} zero_accepting={'yes' if zero_accepting else 'no'}"
        )
        if case.f16:
            sim2 = "not published" if r.sim2_ratio is None else f"{r.sim2_ratio:.3e}"
            line += f" sim2_ratio={sim2}"
        print(line, file=sys.stderr)


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
            f"}};"
        )
    constants_block = "\n\n".join(constants)

    band_fields = ",\n".join(
        f"    .{band_field[channel.key]} = {const_names[channel.key]}" for channel in CHANNELS
    )

    return f"""\
// NASA/TM-2015-218675 atmospheric check-case {case.num} ({case.folder}) oracle band (ADR-0013,
// ADR-0014, ADR-0015, PLAN.md M2 PR 3b). Human-owned: lives under tests/golden/ so guard-oracle.sh
// blocks agent edits outside a session Joe armed. Generated by
// tests/golden/nesc/generate_nesc_band.py -- do not hand-edit; re-run that script after any change
// to its constants or exclusion rules and commit its output. reference is the per-sample mean
// across the case's own contributing NESC simulation tools (Vol II Tables 77/78's exclusions
// applied, plus ADR-0015's cases-4/5 SIM 2 exclusion where applicable); tol_abs is a single scalar
// per channel, the worst case over every sample time (ADR-0013), floored per ADR-0014/ADR-0015 --
// there is no relative term (ADR-0015). See tests/golden/nesc/nesc_band_common.hpp for the
// ChannelBand/CaseBand schema this data fills in.
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


def render_f16_header(case: Case, results: dict[str, ChannelResult]) -> str:
    """An F-16 case's header (ADR-0028). Cases 1 to 10 keep render_case_header() exactly, so their
    headers regenerate byte-identical. The case's sample count and first gated sample are named
    constants in its namespace and the template arguments of its band type (items 3 and 5)."""
    constants = []
    for channel in CHANNELS:
        r = results[channel.key]
        reference = ", ".join(repr(v) for v in r.reference)
        constants.append(
            f"inline constexpr Band::Channel {CONST_NAMES[channel.key]} = {{\n"
            f"    {{{reference}}},\n"
            f"    {r.tol_abs!r},\n"
            f"}};"
        )
    constants_block = "\n\n".join(constants)

    band_fields = ",\n".join(
        f"    .{channel.key} = {CONST_NAMES[channel.key]}" for channel in CHANNELS
    )
    first_gated_t = case.first_gated_sample * SAMPLE_DT

    return f"""\
// NASA/TM-2015-218675 atmospheric check-case {case.name} ({case.folder}) oracle band (ADR-0028,
// which supersedes ADR-0013 in part for this case and elaborates ADR-0014 and ADR-0015; M7 PR 8).
// Human-owned: lives under tests/golden/ so guard-oracle.sh blocks agent edits outside a session
// Joe armed. Generated by tests/golden/nesc/generate_nesc_band.py -- do not hand-edit; re-run
// that script after any change to its constants or rules and commit its output.
// - Members: SIM 4 and SIM 5 only. SIM 2 is excluded for every channel by one rule (ADR-0028 item
//   1, Vol II Tables 79/80 footnote b); the generator reports its distance from the band, never
//   gated.
// - reference is the members' per-sample midpoint; yaw and longitude take the circular mean (item
//   6). Each member's row matches t = k/10 s within {case.match_tolerance_s:g} s (item 4).
// - tol_abs is max(3 * max over gated samples of |sim4 - sim5|, floor): ADR-0013's scalar with
//   ADR-0014's floors and no relative term (ADR-0015). The pass test applies at gated samples only;
//   the earlier samples are compared and logged, never asserted (item 5).
// - kSampleCount: t = 0 to {case.duration_s} s at 10 Hz, the Vol II duration (item 3).
//   kFirstGatedSample: t >= {first_gated_t:.0f} s (item 5).
// See tests/golden/nesc/nesc_band_common.hpp for the CaseBandOf/ChannelBandOf schema.
// NOLINTBEGIN(modernize-use-std-numbers): a data table; a real ensemble mean can land within
// clang-tidy's tolerance of a named math constant purely by coincidence, the same reasoning
// tests/golden/atmosphere/isa1976_table.hpp already gives for its own generated table.
#pragma once

#include "golden/nesc/nesc_band_common.hpp"

namespace astra::test::nesc::case{case.file_tag} {{

inline constexpr int kSampleCount = {case.sample_count};
inline constexpr int kFirstGatedSample = {case.first_gated_sample};
using Band = CaseBandOf<kSampleCount, kFirstGatedSample>;

{constants_block}

inline constexpr Band kBand = {{
{band_fields},
}};

}}  // namespace astra::test::nesc::case{case.file_tag}
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

    for case in CASES + F16_CASES:
        results, inputs = process_case(case, refs_root)
        check_angle_bands(case, results)
        check_non_discriminating(case, results)
        report_case(case, inputs)
        report(case, results)
        out_path = script_dir / f"case{case.file_tag}_{case.slug}_band.hpp"
        render = render_f16_header if case.f16 else render_case_header
        out_path.write_text(render(case, results), encoding="utf-8")
        print(f"# wrote {out_path}", file=sys.stderr)


if __name__ == "__main__":
    main()
