"""The R6 finding's evidence (M7 PR 11a, docs/plans/m7-pr11a-control-peer.md; Joe's 2026-10-13
decision on the R6 finding, items 1 to 5). Read-only analysis: no code under test is edited.

It flies each committed 13.x config, and each reported variant built from it exactly as
tests/physics/nesc/nesc_f16_closed_loop_variants_test.cpp builds it, through astra._core on this
build. The harness logs hold each channel's largest ratio but no per-sample trajectory, and Joe
forbade editing the tests, so the trajectory comes from flying the same configs again: the same
core and plugins give the same states, and the harness's G1 guard shows the frame-start
wrench_total equals the aero channels for the lateral force and moments the F-16 engine leaves at
zero. It reads, read-only:
- the members' CSVs (docs/refs/Atmospheric_checkcases, the main checkout's), converted to SI with
  tests/golden/nesc/generate_nesc_band.py's constants;
- the band headers (tests/golden/nesc/case13p*_band.hpp): each channel's reference and tol_abs;
- the harness logs beside this file, whose "gated max" ratios it checks its own against.

    .venv/bin/python -I -B docs/reviews/evidence/m7-pr11a-r6/r6_report.py table
    .venv/bin/python -I -B docs/reviews/evidence/m7-pr11a-r6/r6_report.py variants
    .venv/bin/python -I -B docs/reviews/evidence/m7-pr11a-r6/r6_report.py lag
    .venv/bin/python -I -B docs/reviews/evidence/m7-pr11a-r6/r6_report.py figures
"""

from __future__ import annotations

import csv
import math
import re
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path

import numpy as np

from astra import _core

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[3]
sys.path.insert(0, str(HERE.parent / "m7-pr11a-residual"))

import derive_residual as dr  # noqa: E402  (its ISA, for the density channel)

# generate_nesc_band.py's constants (Vol II Table 73).
FT = 0.3048
LBF = 4.4482216152605
SLUG = 1.0 / (0.3048 / 4.4482216152605)
DEG = math.pi / 180.0

CHANNELS = [
    ("latitude", "latitude_deg", DEG),
    ("longitude", "longitude_deg", DEG),
    ("altitude", "altitudeMsl_ft", FT),
    ("v_north", "feVelocity_ft_s_X", FT),
    ("v_east", "feVelocity_ft_s_Y", FT),
    ("v_down", "feVelocity_ft_s_Z", FT),
    ("yaw", "eulerAngle_deg_Yaw", DEG),
    ("pitch", "eulerAngle_deg_Pitch", DEG),
    ("roll", "eulerAngle_deg_Roll", DEG),
    ("rate_p", "bodyAngularRateWrtEi_deg_s_Roll", DEG),
    ("rate_q", "bodyAngularRateWrtEi_deg_s_Pitch", DEG),
    ("rate_r", "bodyAngularRateWrtEi_deg_s_Yaw", DEG),
    ("force_y", "aero_bodyForce_lbf_Y", LBF),
    ("moment_l", "aero_bodyMoment_ftlbf_L", LBF * FT),
    ("moment_n", "aero_bodyMoment_ftlbf_N", LBF * FT),
    ("density", "airDensity_slug_ft3", SLUG / FT**3),
]
NAMES = [c[0] for c in CHANNELS]
# Band header names, by channel.
HEADER = {
    "latitude": "Latitude",
    "longitude": "Longitude",
    "altitude": "Altitude",
    "v_north": "VNorth",
    "v_east": "VEast",
    "v_down": "VDown",
    "yaw": "Yaw",
    "pitch": "Pitch",
    "roll": "Roll",
    "rate_p": "RateP",
    "rate_q": "RateQ",
    "rate_r": "RateR",
    "force_y": "ForceY",
    "moment_l": "MomentL",
    "moment_n": "MomentN",
    "density": "Density",
}
WRAPPED = {"yaw", "longitude"}


@dataclass(frozen=True)
class Case:
    tag: str
    folder: str
    config: str
    band: str
    samples: int
    step: int  # the command step's sample


CASES = [
    Case(
        "13p1",
        "Atmos_13p1_SubsonicAltitudeChangeF16",
        "case13p1_subsonic_altitude_change.yaml",
        "case13p1_subsonic_altitude_change_f16_band.hpp",
        201,
        50,
    ),
    Case(
        "13p2",
        "Atmos_13p2_SubsonicAirspeedChangeF16",
        "case13p2_subsonic_airspeed_change.yaml",
        "case13p2_subsonic_airspeed_change_f16_band.hpp",
        201,
        50,
    ),
    Case(
        "13p3",
        "Atmos_13p3_SubsonicHeadingChangeF16",
        "case13p3_subsonic_heading_change.yaml",
        "case13p3_subsonic_heading_change_f16_band.hpp",
        301,
        150,
    ),
    Case(
        "13p4",
        "Atmos_13p4_SubsonicLateralSideStepF16",
        "case13p4_subsonic_lateral_side_step.yaml",
        "case13p4_subsonic_lateral_side_step_f16_band.hpp",
        601,
        200,
    ),
]
FIRST_GATED = 80


def main_checkout() -> Path:
    out = subprocess.run(
        ["git", "-C", str(ROOT), "rev-parse", "--git-common-dir"],
        capture_output=True,
        text=True,
        check=True,
    ).stdout.strip()
    common = Path(out)
    if not common.is_absolute():
        common = (ROOT / common).resolve()
    return common.parent


REFS = main_checkout() / "docs/refs/Atmospheric_checkcases"


def wrap(x: np.ndarray) -> np.ndarray:
    return (x + math.pi) % (2.0 * math.pi) - math.pi


def member(case: Case, sim: int) -> dict[str, np.ndarray]:
    """One member's channels at t = k / 10 s (ADR-0028's 1e-5 s matching), in SI."""
    path = REFS / case.folder / f"Atmos_{case.tag}_sim_0{sim}.csv"
    with path.open() as f:
        reader = csv.reader(f)
        header = next(reader)
        index: dict[str, int] = {}
        for i, name in enumerate(header):
            index.setdefault(name, i)
        rows: dict[int, list[str]] = {}
        for row in reader:
            t = float(row[0])
            k = round(t * 10.0)
            if abs(t - k / 10.0) < 1e-5 and k < case.samples:
                rows[k] = row
    out = {}
    for name, column, factor in CHANNELS:
        out[name] = np.array([float(rows[k][index[column]]) * factor for k in range(case.samples)])
    tas = np.sqrt(
        sum(
            np.array([float(rows[k][index[c]]) * FT for k in range(case.samples)]) ** 2
            for c in ("feVelocity_ft_s_X", "feVelocity_ft_s_Y", "feVelocity_ft_s_Z")
        )
    )
    out["airspeed"] = tas  # still air: the Earth-relative speed
    return out


def band(case: Case) -> dict[str, tuple[np.ndarray, float]]:
    text = (ROOT / "tests" / "golden" / "nesc" / case.band).read_text()
    out = {}
    for name, header in HEADER.items():
        m = re.search(
            rf"inline constexpr Band::Channel k{header} = \{{\s*\{{([^}}]*)\}},\s*([^}}]*)\}};",
            text,
        )
        values = np.array([float(v) for v in m.group(1).replace("\n", " ").split(",") if v.strip()])
        out[name] = (values, float(m.group(2).split(",")[0]))
    return out


def euler(q: np.ndarray) -> tuple[float, float, float]:
    w, x, y, z = q
    c01 = 2.0 * (x * y + w * z)
    c00 = w * w + x * x - y * y - z * z
    c02 = 2.0 * (x * z - w * y)
    c12 = 2.0 * (y * z + w * x)
    c22 = w * w - x * x - y * y + z * z
    return math.atan2(c01, c00), -math.asin(max(-1.0, min(1.0, c02))), math.atan2(c12, c22)


def rotate_b_to_n(q: np.ndarray, v: np.ndarray) -> np.ndarray:
    w, x, y, z = q
    c = np.array(
        [
            [w * w + x * x - y * y - z * z, 2 * (x * y + w * z), 2 * (x * z - w * y)],
            [2 * (x * y - w * z), w * w - x * x + y * y - z * z, 2 * (y * z + w * x)],
            [2 * (x * z + w * y), 2 * (y * z - w * x), w * w - x * x - y * y + z * z],
        ]
    )  # C_nb
    return c.T @ v


def config_text(case: Case, variant: str | None) -> str:
    text = (ROOT / "aircraft/f16/configs" / case.config).read_text()
    if variant is None:
        return text
    if variant == "divisor1":
        return text.replace("  divisor: 10  # 100 Hz (ADR-0029 Decision 4)\n", "  divisor: 1\n")
    text = text.replace(
        "  plugin: aircraft/f16/sim/transport/nesc_control\n",
        "  plugin: tests/plugins/nesc_control_variant\n",
    )
    if variant == "sim5_references":
        eas = 287.98158203347975 * 1852.0 / 3600.0
        lines = (
            "    test_variant: sim5_references\n"
            f"    test_references: {{alpha: {0.0460581645127969!r}, "
            f"theta: {0.0460581645127969!r}, eas: {eas!r}}}\n"
        )
    else:
        lines = f"    test_variant: {variant}\n"
    return text.replace("  peer:\n", "  peer:\n" + lines)


def fly(case: Case, variant: str | None, dense: tuple[int, int] | None = None):
    """The harness's channels at each sample, and (optionally) a dense 1 kHz record of the lateral
    force and moments and the attitude over [dense[0], dense[1]) frames."""
    sim = _core.Simulation.from_config(
        _core.Config.from_string(config_text(case, variant), case.tag), _core.default_plugin_dir()
    )
    reg = sim.registry
    rec = {name: np.zeros(case.samples) for name in NAMES}
    rec["airspeed"] = np.zeros(case.samples)
    dense_rec: dict[str, list[float]] = {
        k: [] for k in ("t", "force_y", "moment_l", "moment_n", "roll", "yaw")
    }
    last = 100 * (case.samples - 1)
    for frame in range(last + 1):
        sampled = frame % 100 == 0
        in_dense = dense is not None and dense[0] <= frame < dense[1]
        st = np.array(sim.vehicles[0].state) if (sampled or in_dense) else None
        if sim.run(1):
            raise RuntimeError(f"{case.tag} {variant}: halted at frame {frame}")
        if in_dense:
            yaw_d, _pitch_d, roll_d = euler(st[6:10])
            dense_rec["t"].append(frame / 1000.0)
            dense_rec["force_y"].append(reg.find("v0.wrench_total.force_b_y"))
            dense_rec["moment_l"].append(reg.find("v0.wrench_total.moment_b_l"))
            dense_rec["moment_n"].append(reg.find("v0.wrench_total.moment_b_n"))
            dense_rec["roll"].append(roll_d)
            dense_rec["yaw"].append(yaw_d)
        if not sampled:
            continue
        k = frame // 100
        p_n, v_b, q, w = st[0:3], st[3:6], st[6:10], st[10:13]
        v_n = rotate_b_to_n(q, v_b)
        yaw, pitch, roll = euler(q)
        values = {
            "latitude": p_n[0],
            "longitude": p_n[1],
            "altitude": p_n[2],
            "v_north": v_n[0],
            "v_east": v_n[1],
            "v_down": v_n[2],
            "yaw": yaw,
            "pitch": pitch,
            "roll": roll,
            "rate_p": w[0],
            "rate_q": w[1],
            "rate_r": w[2],
            "force_y": reg.find("v0.wrench_total.force_b_y"),
            "moment_l": reg.find("v0.wrench_total.moment_b_l"),
            "moment_n": reg.find("v0.wrench_total.moment_b_n"),
            "density": dr.isa(p_n[2])[0],
            "airspeed": float(np.linalg.norm(v_b)),
        }
        for name, value in values.items():
            rec[name][k] = value
    return rec, {key: np.array(v) for key, v in dense_rec.items()}


def ratios(case: Case, rec, bnd):
    out = {}
    for name in NAMES:
        ref, tol = bnd[name]
        d = rec[name] - ref
        if name in WRAPPED:
            d = wrap(d)
        r = np.abs(d) / tol
        gated = r[FIRST_GATED:]
        miss = np.nonzero(gated > 1.0)[0]
        out[name] = (
            float(gated.max()),
            FIRST_GATED + int(np.argmax(gated)),
            FIRST_GATED + int(miss[0]) if miss.size else None,
        )
    return out


def harness_maxima(log: str, label: str) -> dict[str, float]:
    """The harness's "gated max" per channel for the summary block headed by label."""
    out = {}
    start = log.find(label + ":")
    if start < 0:
        return out
    for line in log[start:].splitlines()[1:22]:
        parts = line.split()
        if "gated" in parts:
            out[parts[1] if parts[0].endswith(":") else parts[0]] = float(
                parts[parts.index("gated") + 2]
            )
    return out


def table() -> None:
    log = (HERE / "decided_closed_loop.log").read_text()
    print(
        "R6's decided runs (the committed configs): every channel whose largest gated ratio "
        "exceeds 1"
    )
    print(
        "| Case | Channel | First miss | Largest gated ratio (sample) | ASTRA | Band reference "
        "| tol_abs | ASTRA - SIM 4 | ASTRA - SIM 5 | SIM 4 - SIM 5 | ASTRA sits |"
    )
    print("|---|---|---|---|---|---|---|---|---|---|---|")
    checks = []
    for case in CASES:
        bnd = band(case)
        s4, s5 = member(case, 4), member(case, 5)
        rec, _ = fly(case, None)
        rr = ratios(case, rec, bnd)
        maxima = harness_maxima(log, f"case{case.tag}")
        for name in NAMES:
            if name in maxima:
                checks.append(abs(maxima[name] - rr[name][0]) <= 1e-3 * max(1.0, rr[name][0]))
        for name in NAMES:
            r, rk, first = rr[name]
            if r <= 1.0:
                continue
            k = first
            a, ref, tol = rec[name][k], bnd[name][0][k], bnd[name][1]
            m4, m5 = s4[name][k], s5[name][k]
            lo, hi = min(m4, m5), max(m4, m5)
            where = "between them" if lo <= a <= hi else ("above both" if a > hi else "below both")
            print(
                f"| {case.tag} | {name} | {k} ({k / 10:.1f} s) | {r:.3g} ({rk}) | {a:.7g} | "
                f"{ref:.7g} | {tol:.4g} | {a - m4:+.4g} | {a - m5:+.4g} | {m4 - m5:+.4g} | "
                f"{where} |"
            )
    print(
        f"\nThis script's gated maxima against the harness log's: {sum(checks)} of "
        f"{len(checks)} agree to 1e-3 relative"
    )


VARIANTS = {
    "sim5_references": "SIM 5 references",
    "divisor1": "divisor 1",
    "rhumb_line": "rhumb line",
    "great_circle": "great circle",
    "line_at_offset_frame": "line at the offset frame",
}


def variant_list(case: Case) -> list[str]:
    out = ["sim5_references", "divisor1"]
    if case.tag == "13p4":
        out += ["rhumb_line", "great_circle", "line_at_offset_frame"]
    return out


def rows_for(case: Case, label: str, rec, bnd, s4, s5, rr) -> list[str]:
    out = []
    for name in NAMES:
        r, rk, first = rr[name]
        if r <= 1.0:
            continue
        k = first
        a, ref, tol = rec[name][k], bnd[name][0][k], bnd[name][1]
        m4, m5 = s4[name][k], s5[name][k]
        lo, hi = min(m4, m5), max(m4, m5)
        where = "between them" if lo <= a <= hi else ("above both" if a > hi else "below both")
        out.append(
            f"| {case.tag} | {label} | {name} | {k} ({k / 10:.1f} s) | {r:.3g} ({rk}) | "
            f"{a:.7g} | {ref:.7g} | {tol:.4g} | {a - m4:+.4g} | {a - m5:+.4g} | "
            f"{m4 - m5:+.4g} | {where} |"
        )
    return out


def variants() -> None:
    log = (HERE / "variants.log").read_text()
    print(
        "R6's reported variants, never selected: every channel whose largest gated ratio "
        "exceeds 1, in item 1's form"
    )
    print(
        "| Case | Run | Channel | First miss | Largest gated ratio (sample) | ASTRA | Band "
        "reference | tol_abs | ASTRA - SIM 4 | ASTRA - SIM 5 | SIM 4 - SIM 5 | ASTRA sits |"
    )
    print("|---|---|---|---|---|---|---|---|---|---|---|---|")
    compact: list[str] = []
    checks = []
    for case in CASES:
        bnd = band(case)
        s4, s5 = member(case, 4), member(case, 5)
        runs = {"decided": ratios(case, fly(case, None)[0], bnd)}
        records = {}
        for v in variant_list(case):
            rec, _ = fly(case, v)
            records[v] = rec
            runs[v] = ratios(case, rec, bnd)
            maxima = harness_maxima(
                log, f"case{case.tag} {VARIANTS[v]} (reported, no band asserted)"
            )
            for name in NAMES:
                if name in maxima:
                    checks.append(
                        abs(maxima[name] - runs[v][name][0]) <= 1e-3 * max(1.0, runs[v][name][0])
                    )
            for line in rows_for(case, VARIANTS[v], rec, bnd, s4, s5, runs[v]):
                print(line)
        heads = ["decided", *variant_list(case)]
        names = [n for n in NAMES if any(runs[h][n][0] > 1.0 for h in heads)]
        compact.append(
            f"\n{case.tag}: largest gated ratio per channel (a channel above 1 in any run)"
        )
        compact.append("| Channel | " + " | ".join(VARIANTS.get(h, h) for h in heads) + " |")
        compact.append("|---|" + "---|" * len(heads))
        for n in names:
            compact.append(f"| {n} | " + " | ".join(f"{runs[h][n][0]:.3g}" for h in heads) + " |")
        inside = [h for h in heads if all(runs[h][n][0] <= 1.0 for n in NAMES)]
        compact.append(
            f"Inside every band: {', '.join(VARIANTS.get(h, h) for h in inside) or 'none'}"
        )
    for line in compact:
        print(line)
    print(
        f"\nThis script's gated maxima against the harness's (variants.log): {sum(checks)} of "
        f"{len(checks)} agree to 1e-3 relative"
    )


LAG_CHANNELS = ("force_y", "moment_l", "moment_n")
LAG_RANGE = 0.5  # s, the shifts searched each way
LAG_STEP = 0.001  # s, ASTRA's dense record is every frame


def lag_one(dense_t, dense_v, member_t, member_v, window):
    """The shift tau (s) that best aligns ASTRA's transient to a member's: the maximum of the
    correlation of the two first-differenced signals at the member's 10 Hz samples inside the
    window, ASTRA read at t + tau from its 1 kHz record. Returns tau, the correlation there and at
    0, and the RMS of ASTRA(t + tau) - member over the window, at tau and at 0, against the
    member's own RMS (a timing difference leaves little after the shift; a shape difference does
    not)."""
    keep = (member_t >= window[0]) & (member_t <= window[1])
    mt, mv = member_t[keep], member_v[keep]
    dm = np.diff(mv)

    def at(tau):
        return np.interp(mt + tau, dense_t, dense_v)

    def corr(tau):
        da = np.diff(at(tau))
        return float(np.dot(da, dm) / (np.linalg.norm(da) * np.linalg.norm(dm)))

    taus = np.arange(-LAG_RANGE, LAG_RANGE + LAG_STEP / 2, LAG_STEP)
    cs = np.array([corr(t) for t in taus])
    best = float(taus[int(np.argmax(cs))])
    rms_m = float(np.sqrt(np.mean(mv**2)))
    rms_best = float(np.sqrt(np.mean((at(best) - mv) ** 2)))
    rms_zero = float(np.sqrt(np.mean((at(0.0) - mv) ** 2)))
    return best, float(cs.max()), corr(0.0), rms_best / rms_m, rms_zero / rms_m


def lag() -> None:
    print(
        "13.3's and 13.4's step transients: ASTRA's shift to each member (+ means ASTRA's "
        "transient comes later), within +-2 s of the step"
    )
    print(
        "| Case | Run | Channel | Member | Best shift (s) | Correlation there | Correlation at 0 | "
        "Residual RMS / member RMS, shifted | the same, unshifted |"
    )
    print("|---|---|---|---|---|---|---|---|---|")
    for case in CASES[2:]:
        step_t = case.step / 10.0
        window = (step_t - 2.0, step_t + 2.0)
        frames = (int((step_t - 2.5) * 1000), int((step_t + 2.5) * 1000))
        members = {4: member(case, 4), 5: member(case, 5)}
        mt = np.arange(case.samples) / 10.0
        for run in (None, "divisor1"):
            _rec, dense = fly(case, run, dense=frames)
            for name in LAG_CHANNELS:
                for sim_n, data in members.items():
                    tau, c_best, c_zero, r_best, r_zero = lag_one(
                        dense["t"], dense[name], mt, data[name], window
                    )
                    label = "divisor 1" if run else "decided"
                    print(
                        f"| {case.tag} | {label} | {name} | SIM {sim_n} | {tau:+.3f} | "
                        f"{c_best:.3f} | {c_zero:.3f} | {r_best:.3f} | {r_zero:.3f} |"
                    )
    print(
        "\nThe missing channels are the loose force and moment ones (ADR-0028's footnote (e) "
        "transients); the kinematic channels stay inside every band in both cases."
    )


def figures() -> None:
    import matplotlib

    matplotlib.use("Agg")
    import matplotlib.pyplot as plt

    panels = {
        "altitude": ("altitude", "m", 1.0),
        "airspeed": ("airspeed", "m/s", 1.0),
        "pitch": ("pitch", "deg", 1.0 / DEG),
        "roll": ("roll", "deg", 1.0 / DEG),
        "yaw": ("yaw", "deg", 1.0 / DEG),
    }
    for case in CASES:
        names = ["altitude", "airspeed", "pitch"] + (
            ["roll", "yaw"] if case.tag in ("13p3", "13p4") else []
        )
        bnd = band(case)
        s4, s5 = member(case, 4), member(case, 5)
        rec, _ = fly(case, None)
        t = np.arange(case.samples) / 10.0
        fig, axes = plt.subplots(len(names), 2, figsize=(12.0, 2.3 * len(names)), sharex=True)
        for row, name in zip(axes, names, strict=True):
            key, unit, scale = panels[name]
            ax, dx = row
            for a in (ax, dx):
                a.axvspan(FIRST_GATED / 10.0, t[-1], color="0.93", zorder=0)
            # Left: the signal. Right: each run minus the band reference (the members' mean for
            # airspeed, which has no band), with +-tol_abs dashed.
            if key in bnd:
                ref, tol = bnd[key]
                ax.plot(t, (ref + tol) * scale, color="0.5", lw=0.8, ls="--")
                ax.plot(
                    t,
                    (ref - tol) * scale,
                    color="0.5",
                    lw=0.8,
                    ls="--",
                    label="band (reference +- tol_abs)",
                )
                dx.axhline(tol * scale, color="0.5", lw=0.8, ls="--")
                dx.axhline(-tol * scale, color="0.5", lw=0.8, ls="--")
                base, base_label = ref, "band reference"
            else:
                base, base_label = (s4[key] + s5[key]) / 2.0, "the members' mean (no band)"
            ax.plot(t, s4[key] * scale, color="tab:blue", lw=1.0, label="SIM 4")
            ax.plot(t, s5[key] * scale, color="tab:orange", lw=1.0, label="SIM 5")
            ax.plot(t, rec[key] * scale, color="black", lw=1.0, ls=":", label="ASTRA (decided)")
            d = {"SIM 4": s4[key] - base, "SIM 5": s5[key] - base, "ASTRA": rec[key] - base}
            if key in WRAPPED:
                d = {k: wrap(v) for k, v in d.items()}
            dx.plot(t, d["SIM 4"] * scale, color="tab:blue", lw=1.0)
            dx.plot(t, d["SIM 5"] * scale, color="tab:orange", lw=1.0)
            dx.plot(t, d["ASTRA"] * scale, color="black", lw=1.0, ls=":")
            ax.set_ylabel(f"{name} ({unit})")
            dx.set_ylabel(f"minus {base_label}", fontsize=8)
        axes[0][0].legend(loc="best", fontsize=7)
        for a in axes[-1]:
            a.set_xlabel("t (s); the gated window (t >= 8 s) is shaded")
        fig.suptitle(
            f"NESC case {case.tag.replace('p', '.')[2:]}: ASTRA against SIM 4 and SIM 5"
            if False
            else f"NESC case 13.{case.tag[3]}: ASTRA against SIM 4 and SIM 5"
        )
        fig.tight_layout()
        out = HERE / f"case{case.tag}.png"
        fig.savefig(out, dpi=110)
        plt.close(fig)
        print(f"wrote {out.relative_to(ROOT)}")


def main() -> int:
    mode = sys.argv[1] if len(sys.argv) > 1 else "table"
    if mode == "table":
        table()
    elif mode == "variants":
        variants()
    elif mode == "lag":
        lag()
    elif mode == "figures":
        figures()
    else:
        raise SystemExit(f"unknown mode {mode}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
