"""Q7's full expected trim residual for NESC cases 11 and 12, derived from the DML (P1) alone
(M7 PR 9, docs/plans/m7-pr9-open-loop.md, Q1 and Joe's amendments 1 to 3, 12 and 13; the M7
plan's R4 and Q7 with amendment 2).

Independence. This script imports nothing from astra, links no plugin and reads no generated table.
It reads NASA's DAVE-ML files directly, with xml.etree:
- F16_aero.dml, F16_prop.dml and F16_inertia.dml, under the main checkout's
  docs/refs/Atmospheric_models/F16_package/F16_S119_source/;
- their SHA-256 values are checked against docs/plans/m7-f16-inventory.md section 1.1.
It states its own physical constants, each with its source. ASTRA's values are read as text from
plugins/physical/atmosphere/isa1976/isa1976.cpp, core/include/astra/util/geodesy.hpp and
core/include/astra/util/units.hpp only to list them beside this script's (amendment 13); they are
never used in a computation.

What it computes:
1. Its transcription of the DML, checked against every checkData shot in F16_aero.dml (16 shots) and
   F16_prop.dml (9 shots), at each output's own tol (abs(y - y_good) < tol, the generated *_verify.m
   rule).
2. For each case, its own three-unknown trim (theta, elevator, PWR) at the config's setpoint,
   under the n-frame rate (the M7 plan's Q6, Joe's (b)): omega_b = C_bn (omega_ie + omega_en),
   the aero fed the Earth-relative rate omega_b - omega_ie^b (ADR-0032; addendum D3), and the
   moments moved from the MRC to the CM (addendum D4). Its agreement with the config is
   pre-registered at 1e-6 in theta (rad), the elevator (rad) and the throttle (amendment 1(b));
   outside that is a stop condition.
3. All six residuals by parts at the script's own trim (the plan's Q1 table), and at the config's
   own literals (the constants pasted into nesc_f16_open_loop_test.cpp, amendment 1(a)). The parts
   are kinematic -(omega_b + omega_ie^b) x v_b, gravity (J2 plus centrifugal), aero and engine for
   the velocity, and kinematic I^-1(-omega x I omega), aero and engine for the rates.

The config's scalars are read as text, never through a YAML library's typing (amendments 3 and 12):
each must match a strict decimal-or-exponent regex, and is converted with float(). A text that fails
it stops the script.

Run from the repository root:
    .venv/bin/python -I -B docs/reviews/evidence/m7-pr9-residual/derive_residual.py
It prints its log to stdout and exits 1 on any failed check.
"""

from __future__ import annotations

import ast
import hashlib
import math
import re
import subprocess
import sys

# NASA's own files, their SHA-256 checked first; expat fetches no external DTD.
import xml.etree.ElementTree as ET
from dataclasses import dataclass
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parents[4]

# --- Sources -------------------------------------------------------------------------------------

SHA256 = {  # docs/plans/m7-f16-inventory.md section 1.1
    "F16_aero.dml": "82796577b989b5f3becb7d13430ff6c619b3ecd2e642a6427b743658fb177e51",
    "F16_prop.dml": "e5fd7772e5c05cdac666c94febbe4cb50139418b26045affd92dbd198f2215c6",
    "F16_inertia.dml": "e11d4eba5b158d24db7034bd85682ad0b896dd2f3172f007ca3d6d6d4e693c4a",
}

CASES = [
    ("case 11", "aircraft/f16/configs/case11_trim_check_subsonic.yaml"),
    ("case 12", "aircraft/f16/configs/case12_trim_check_supersonic.yaml"),
]

# The plan's pre-registered absolute bounds (amendment 2), printed beside the derived values.
BUDGETS = {
    "case 11": (9.7888e-12, 9.8858e-18, 5.9955e-18),
    "case 12": (9.7701e-12, 2.2387e-17, 3.7702e-17),
}
TRIM_AGREEMENT = 1e-6  # amendment 1(b): theta (rad), elevator (rad), throttle

# --- This script's own constants, each with its source (amendment 13) ----------------------------

# U.S. Standard Atmosphere 1976 (NOAA-S/T 76-1562): Table 2 (defining constants), Table 4 (layer 0's
# gradient), eq 18 (geometric to geopotential altitude with r0), eqs 33a and 42 (pressure, density),
# eq 50 (speed of sound).
ISA_T0 = 288.15  # K
ISA_P0 = 101325.0  # Pa
ISA_L0 = -0.0065  # K/m', layer 0
ISA_G0 = 9.80665  # m/s^2 (g0')
ISA_RSTAR = 8.31432  # J/(mol K)
ISA_M0 = 0.0289644  # kg/mol
ISA_R0 = 6356766.0  # m, eq 18's effective Earth radius
ISA_GAMMA = 1.4
# WGS-84, NIMA TR8350.2: eqs 3-1 (a), 3-2 (1/f), 3-6 (omega), 3-3 (GM), as M7 PR 5's
# tests/python/test_trim_kinematics.py states them.
WGS_A = 6378137.0
WGS_F = 1.0 / 298.257223563
WGS_E2 = WGS_F * (2.0 - WGS_F)
WGS_OMEGA = 7.292115e-5
WGS_GM = 3.986004418e14
# J2: the gravity model's declared value (PLAN.md's unscheduled kJ2 item records that its source is
# unstated; M7 PR 5 used it the same way).
WGS_J2 = 1.082629989e-3
# Vol II Table 73 (doc p.93): the foot and the pound-force; the slug is lbf s^2/ft.
FT = 0.3048  # m
LBF = 4.4482216152605  # N
SLUG = LBF / FT  # kg
CG_PCT_MAC = 25.0  # README.html:1234 and Vol II Table 7: x-bar = 25 % MAC (addendum section 4)

# --- Reading the config's scalars as text (amendments 3 and 12) ----------------------------------

DECIMAL = re.compile(r"^[-+]?(?:\d+\.\d*|\.\d+)(?:[eE][-+]?\d+)?$|^[-+]?\d+[eE][-+]?\d+$")


class StopError(Exception):
    """A pre-registered stop condition: the work stops and goes to Joe."""


def scalar(text: str, where: str) -> float:
    text = text.strip()
    if not DECIMAL.match(text):
        raise StopError(f"{where}: {text!r} is not a plain decimal or exponent literal")
    return float(text)


def strip_comment(line: str) -> str:
    return line.split("#", 1)[0].rstrip()


def read_config(path: Path) -> tuple[dict[str, list[float]], dict[str, float], list[str]]:
    """The initial state's four vectors and the four controls, from the config's text. Each value's
    log line is its text beside its %.17g."""
    lines = [strip_comment(line) for line in path.read_text(encoding="utf-8").splitlines()]
    state: dict[str, list[float]] = {}
    controls: dict[str, float] = {}
    log: list[str] = []
    in_controls = False
    for line in lines:
        flow = re.match(r"^\s+(p_n|v_b|euler_nb|omega_b): \[(.*)\]$", line)
        if flow:
            key = flow.group(1)
            texts = [t.strip() for t in flow.group(2).split(",")]
            state[key] = [scalar(t, f"initial.state.{key}[{i}]") for i, t in enumerate(texts)]
            for i, t in enumerate(texts):
                log.append(f"  initial.state.{key}[{i}]  text {t:>24s}  %.17g {state[key][i]:.17g}")
            continue
        if re.match(r"^\s+controls:\s*$", line):
            in_controls = True
            continue
        if in_controls:
            item = re.match(r"^\s{6}(elevator|aileron|rudder|throttle): (\S+)$", line)
            if item:
                name = item.group(1)
                controls[name] = scalar(item.group(2), f"controls.{name}")
                log.append(
                    f"  controls.{name:9s}         text {item.group(2):>24s}  "
                    f"%.17g {controls[name]:.17g}"
                )
                continue
            if line.strip() and not line.startswith("      "):
                in_controls = False
    want_state = {"p_n", "v_b", "euler_nb", "omega_b"}
    want_controls = {"elevator", "aileron", "rudder", "throttle"}
    if set(state) != want_state or set(controls) != want_controls:
        raise StopError(f"{path}: found state {sorted(state)} and controls {sorted(controls)}")
    for key, values in state.items():
        if len(values) != 3:
            raise StopError(f"{path}: initial.state.{key} has {len(values)} entries")
    return state, controls, log


# --- The DML, read directly ----------------------------------------------------------------------


def main_checkout() -> Path:
    """The main checkout's root, through `git rev-parse --git-common-dir` (generate_nesc_band.py's
    resolution): docs/refs is gitignored and placed there by Joe."""
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


def local(tag: str) -> str:
    return tag.rsplit("}", 1)[-1]


def numbers(text: str) -> list[float]:
    return [float(t) for t in re.split(r"[,\s]+", text.strip()) if t]


@dataclass
class Table:
    axes: list[list[float]]  # slowest first
    values: list[float]


class Dml:
    def __init__(self, path: Path) -> None:
        self.root = ET.parse(path).getroot()
        self.breakpoints: dict[str, list[float]] = {}
        self.tables: dict[str, Table] = {}
        self.initial: dict[str, float] = {}
        for el in self.root.iter():
            tag = local(el.tag)
            if tag == "breakpointDef":
                vals = next(c for c in el if local(c.tag) == "bpVals")
                self.breakpoints[el.attrib["bpID"]] = numbers(vals.text or "")
            elif tag == "variableDef" and "initialValue" in el.attrib:
                self.initial[el.attrib["varID"]] = float(el.attrib["initialValue"])
        for el in self.root.iter():
            if local(el.tag) == "griddedTableDef":
                refs = [c for c in el.iter() if local(c.tag) == "bpRef"]
                data = next(c for c in el.iter() if local(c.tag) == "dataTable")
                axes = [self.breakpoints[r.attrib["bpID"]] for r in refs]
                values = numbers(data.text or "")
                if len(values) != math.prod(len(a) for a in axes):
                    raise StopError(f"{el.attrib['gtID']}: {len(values)} values")
                self.tables[el.attrib["gtID"]] = Table(axes, values)

    def shots(self) -> list[tuple[str, dict[str, float], dict[str, tuple[float, float]]]]:
        out = []
        for shot in self.root.iter():
            if local(shot.tag) != "staticShot":
                continue
            inputs: dict[str, float] = {}
            outputs: dict[str, tuple[float, float]] = {}
            for part in shot:
                kind = local(part.tag)
                if kind not in ("checkInputs", "checkOutputs"):
                    continue
                for sig in part:
                    fields = {local(c.tag): (c.text or "").strip() for c in sig}
                    if kind == "checkInputs":
                        inputs[fields["signalName"]] = float(fields["signalValue"])
                    else:
                        outputs[fields["signalName"]] = (
                            float(fields["signalValue"]),
                            float(fields["tol"]),
                        )
            out.append((shot.attrib.get("name", "?"), inputs, outputs))
        return out


def lookup1(axis: list[float], values: list[float], x: float) -> float:
    """DAVE-ML extrapolate="neither", which the package's Simulink realization implements as clip
    (addendum section 2): clamp, then interpolate linearly."""
    x = min(max(x, axis[0]), axis[-1])
    i = 0
    while i < len(axis) - 2 and x > axis[i + 1]:
        i += 1
    frac = (x - axis[i]) / (axis[i + 1] - axis[i])
    return values[i] + frac * (values[i + 1] - values[i])


def lookup(table: Table, *x: float) -> float:
    if len(table.axes) == 1:
        return lookup1(table.axes[0], table.values, x[0])
    slow, fast = table.axes
    n = len(fast)
    column = [lookup1(fast, table.values[k * n : (k + 1) * n], x[1]) for k in range(len(slow))]
    return lookup1(slow, column, x[0])


# --- The DML's build-ups, in its own units -------------------------------------------------------


class Aero:
    """F16_aero.dml's MathML (addendum section 2): coefficients from vt (ft/s), alpha, beta (deg),
    p, q, r (rad/s), el (deg, +TED), ail (deg, +LWD) and rdr (deg, +TEL)."""

    def __init__(self, dml: Dml) -> None:
        self.t = dml.tables
        self.cbar = 11.32  # ft, F16_aero.dml:368-384
        self.bspan = 30.0
        self.sref = 300.0
        self.rtd = 180.0 / 3.14159265  # F16_aero.dml:355-366, the file's truncated pi

    def coefficients(self, vt, alpha, beta, p, q, r, el, ail, rdr):
        t = self.t
        dele = el / 25.0
        dail = ail / 20.0
        drdr = rdr / 30.0
        cxt = lookup(t["CX_table_def"], el, alpha)
        cy0 = -0.02 * beta + 0.021 * dail + 0.086 * drdr
        czt = lookup(t["CZ0_table_def"], alpha)
        cz1 = czt * (1.0 - (beta / self.rtd) ** 2) - 0.19 * dele
        cmt = lookup(t["Cm0_table_def"], el, alpha)
        abs_cl0 = lookup(t["Cl0_table_def"], abs(beta), alpha)
        abs_cn0 = lookup(t["Cn0_table_def"], abs(beta), alpha)
        clt = -abs_cl0 if beta < 0 else abs_cl0
        cnt = -abs_cn0 if beta < 0 else abs_cn0
        damp = {
            k: lookup(t[f"{k}_table_def"], alpha)
            for k in ("CXq", "CYr", "CYp", "CZq", "Clr", "Clp", "Cmq", "Cnr", "Cnp")
        }
        dclda = lookup(t["dlda_table_def"], beta, alpha)
        dcldr = lookup(t["dldr_table_def"], beta, alpha)
        dcnda = lookup(t["dnda_table_def"], beta, alpha)
        dcndr = lookup(t["dndr_table_def"], beta, alpha)
        tvt = 2.0 * vt
        b2v = self.bspan / tvt
        cq2v = self.cbar * q / tvt
        cl1 = clt + dclda * dail + dcldr * drdr
        cn1 = cnt + dcnda * dail + dcndr * drdr
        rate = {
            "cx": cq2v * damp["CXq"],
            "cy": b2v * (damp["CYp"] * p + damp["CYr"] * r),
            "cz": cq2v * damp["CZq"],
            "cl": b2v * (damp["Clp"] * p + damp["Clr"] * r),
            "cm": cq2v * damp["Cmq"],
            "cn": b2v * (damp["Cnp"] * p + damp["Cnr"] * r),
        }
        static = {"cx": cxt, "cy": cy0, "cz": cz1, "cl": cl1, "cm": cmt, "cn": cn1}
        return static, rate


class Engine:
    """F16_prop.dml (addendum section 3): net thrust (lbf) along body x from PWR (pct), ALT (ft) and
    Mach, every table clipped; no moments, no state."""

    def __init__(self, dml: Dml) -> None:
        self.t = dml.tables
        self.mil = dml.initial["MIL_PWR"]

    def thrust(self, pwr: float, alt_ft: float, mach: float) -> float:
        idle = lookup(self.t["T_IDLE_table"], mach, alt_ft)
        mil = lookup(self.t["T_MIL_table"], mach, alt_ft)
        mx = lookup(self.t["T_MAX_table"], mach, alt_ft)
        if pwr < self.mil:
            return idle + pwr * (mil - idle) / self.mil
        return mil + (pwr - self.mil) * (mx - mil) / (100.0 - self.mil)


def check_shots(aero: Aero, engine: Engine, aero_dml: Dml, prop_dml: Dml) -> list[str]:
    log = []
    names = {
        "aeroBodyForceCoefficient_X": "cx",
        "aeroBodyForceCoefficient_Y": "cy",
        "aeroBodyForceCoefficient_Z": "cz",
        "aeroBodyMomentCoefficient_Roll": "cl",
        "aeroBodyMomentCoefficient_Pitch": "cm",
        "aeroBodyMomentCoefficient_Yaw": "cn",
    }
    worst = 0.0
    shots = aero_dml.shots()
    for name, i, o in shots:
        static, rate = aero.coefficients(
            i["trueAirspeed"],
            i["angleOfAttack"],
            i["angleOfSideslip"],
            i["bodyAngularRate_Roll"],
            i["bodyAngularRate_Pitch"],
            i["bodyAngularRate_Yaw"],
            i["elevatorDeflection"],
            i["aileronDeflection"],
            i["rudderDeflection"],
        )
        for signal, key in names.items():
            good, tol = o[signal]
            miss = abs(static[key] + rate[key] - good)
            worst = max(worst, miss / tol)
            if not miss < tol:
                raise StopError(
                    f"F16_aero.dml checkData '{name}' {signal}: miss {miss} >= tol {tol}"
                )
    log.append(
        f"  F16_aero.dml: {len(shots)} shots x 6 coefficients pass; largest miss/tol {worst:.3g}"
    )
    worst = 0.0
    shots = prop_dml.shots()
    for name, i, o in shots:
        fex = engine.thrust(i["powerLeverAngle"], i["altitudeMSL"], i["mach"])
        for signal, (good, tol) in o.items():
            value = fex if signal == "thrustBodyForce_X" else 0.0
            miss = abs(value - good)
            worst = max(worst, miss / tol)
            if not miss < tol:
                raise StopError(
                    f"F16_prop.dml checkData '{name}' {signal}: miss {miss} >= tol {tol}"
                )
    log.append(f"  F16_prop.dml: {len(shots)} shots x 6 outputs pass; largest miss/tol {worst:.3g}")
    if len(aero_dml.shots()) != 16 or len(prop_dml.shots()) != 9:
        raise StopError("checkData shot counts differ from the addendum's 16 and 9")
    return log


# --- Earth, gravity, atmosphere ------------------------------------------------------------------


def radii(lat: float) -> tuple[float, float]:
    """Meridian and prime-vertical radii (S&L 3rd ed. eqs. 1.6-5, 1.6-9; Groves eqs. 2.105,
    2.106)."""
    s2 = math.sin(lat) ** 2
    return WGS_A * (1.0 - WGS_E2) / (1.0 - WGS_E2 * s2) ** 1.5, WGS_A / math.sqrt(1.0 - WGS_E2 * s2)


def gravity_n(lat: float, lon: float, alt: float) -> np.ndarray:
    """Plumb-bob gravity in geodetic NED: S&L eq. 1.6-24's J2 gravitation, its gradient in
    geocentric spherical coordinates rotated through D = lat - psi_c, plus eq. 1.6-27's
    centrifugal term, at eq. 1.6-17's ECEF position (M7 PR 5's closed form)."""
    _, re_ = radii(lat)
    p = np.array(
        [
            (re_ + alt) * math.cos(lat) * math.cos(lon),
            (re_ + alt) * math.cos(lat) * math.sin(lon),
            (re_ * (1.0 - WGS_E2) + alt) * math.sin(lat),
        ]
    )
    r = float(np.linalg.norm(p))
    psi_c = math.asin(p[2] / r)
    k = (WGS_A / r) ** 2
    g_radial = -(WGS_GM / r**2) * (1.0 - 1.5 * WGS_J2 * k * (3.0 * math.sin(psi_c) ** 2 - 1.0))
    g_north_c = -3.0 * (WGS_GM / r**2) * WGS_J2 * k * math.sin(psi_c) * math.cos(psi_c)
    d = lat - psi_c
    gravitation = np.array(
        [
            -math.sin(d) * g_radial + math.cos(d) * g_north_c,
            0.0,
            -(math.cos(d) * g_radial + math.sin(d) * g_north_c),
        ]
    )
    centrifugal = (
        WGS_OMEGA**2
        * (re_ + alt)
        * np.array([-math.sin(lat) * math.cos(lat), 0.0, -(math.cos(lat) ** 2)])
    )
    return gravitation + centrifugal


def isa(z: float) -> tuple[float, float]:
    """Density (kg/m^3) and speed of sound (m/s) in layer 0 at geometric altitude z (m): eq 18's
    geopotential H, eq 33a's pressure, eq 42's density, eq 50's speed of sound."""
    h = ISA_R0 * z / (ISA_R0 + z)
    if not -5000.0 <= h < 11000.0:
        raise StopError(f"geopotential altitude {h} m is outside layer 0")
    t = ISA_T0 + ISA_L0 * h
    p = ISA_P0 * (ISA_T0 / t) ** (ISA_G0 * ISA_M0 / (ISA_RSTAR * ISA_L0))
    rho = p * ISA_M0 / (ISA_RSTAR * t)
    return rho, math.sqrt(ISA_GAMMA * ISA_RSTAR * t / ISA_M0)


def c_bn(psi: float, theta: float, phi: float) -> np.ndarray:
    """NED to body, 3-2-1 (S&L eq. 1.3-10)."""
    cps, sps = math.cos(psi), math.sin(psi)
    cth, sth = math.cos(theta), math.sin(theta)
    cph, sph = math.cos(phi), math.sin(phi)
    return np.array(
        [
            [cth * cps, cth * sps, -sth],
            [sph * sth * cps - cph * sps, sph * sth * sps + cph * cps, sph * cth],
            [cph * sth * cps + sph * sps, cph * sth * sps - sph * cps, cph * cth],
        ]
    )


# --- The residual by parts -----------------------------------------------------------------------


@dataclass
class Vehicle:
    mass: float  # kg
    inertia: np.ndarray  # kg m^2, product-of-inertia convention (negated off-diagonals)
    dx_ft: float  # CM forward of the MRC, ft


def residual_parts(aero: Aero, engine: Engine, veh: Vehicle, p_n, v_b, euler, omega_b, controls):
    """The six residuals (v_dot_b, omega_dot_b) at a state, by parts."""
    lat, lon, alt = p_n
    v_b = np.asarray(v_b, dtype=float)
    omega_b = np.asarray(omega_b, dtype=float)
    cbn = c_bn(*euler)
    omega_ie_b = cbn @ (WGS_OMEGA * np.array([math.cos(lat), 0.0, -math.sin(lat)]))
    omega_eb = omega_b - omega_ie_b  # ADR-0032: the rate the F-16 aero damps
    rho, sound = isa(alt)
    speed = float(np.linalg.norm(v_b))  # still air
    alpha = math.atan2(v_b[2], v_b[0])
    beta = math.asin(v_b[1] / speed)
    vt = speed / FT
    qbar = 0.5 * (rho / (SLUG / FT**3)) * vt**2  # lbf/ft^2
    static, rate = aero.coefficients(
        vt,
        math.degrees(alpha),
        math.degrees(beta),
        *omega_eb,
        math.degrees(controls["elevator"]),
        -math.degrees(controls["aileron"]),  # ICD +RWD -> +LWD
        math.degrees(controls["rudder"]),
    )

    def wrench(c: dict[str, float]) -> tuple[np.ndarray, np.ndarray]:
        f = qbar * aero.sref * np.array([c["cx"], c["cy"], c["cz"]])  # lbf
        m = (
            qbar
            * aero.sref
            * np.array([aero.bspan * c["cl"], aero.cbar * c["cm"], aero.bspan * c["cn"]])
        )
        m = m + np.array([0.0, veh.dx_ft * f[2], -veh.dx_ft * f[1]])  # D4: MRC to CM
        return f * LBF, m * LBF * FT

    f_static, m_static = wrench(static)
    f_rate, m_rate = wrench(rate)
    f_aero, m_aero = f_static + f_rate, m_static + m_rate
    pwr = 100.0 * controls["throttle"]
    thrust = engine.thrust(pwr, min(max(alt / FT, 0.0), 50000.0), min(max(speed / sound, 0.0), 1.0))
    f_engine = np.array([thrust * LBF, 0.0, 0.0])
    m_engine = np.zeros(3)
    inv = np.linalg.inv(veh.inertia)
    parts_v = {
        "kinematic": -np.cross(omega_b + omega_ie_b, v_b),
        "gravity": cbn @ gravity_n(lat, lon, alt),
        "aero": f_aero / veh.mass,
        "engine": f_engine / veh.mass,
    }
    parts_w = {
        "kinematic": inv @ (-np.cross(omega_b, veh.inertia @ omega_b)),
        "gravity": np.zeros(3),
        "aero": inv @ m_aero,
        "engine": inv @ m_engine,
    }
    total = np.concatenate([sum(parts_v.values()), sum(parts_w.values())])
    extra = {
        "aero_static_lateral": (f_static[1], m_static[0], m_static[2]),
        "aero_lbf": (f_aero / LBF, m_aero / (LBF * FT)),
        "omega_eb": omega_eb,
        "g": float(np.linalg.norm(gravity_n(lat, lon, alt))),
        "mach": speed / sound,
        "rho": rho,
    }
    return total, parts_v, parts_w, extra


def n_frame_rate(lat: float, alt: float, speed: float, psi: float, theta: float) -> np.ndarray:
    """omega_b = C_bn (omega_ie^n + omega_en^n) (Titterton eq. 3.72; Groves eq. 5.44)."""
    rn, re_ = radii(lat)
    v_n = speed * np.array([math.cos(psi), math.sin(psi), 0.0])
    w_ie = WGS_OMEGA * np.array([math.cos(lat), 0.0, -math.sin(lat)])
    w_en = np.array(
        [v_n[1] / (re_ + alt), -v_n[0] / (rn + alt), -v_n[1] * math.tan(lat) / (re_ + alt)]
    )
    return c_bn(psi, theta, 0.0) @ (w_ie + w_en)


def solve_trim(aero, engine, veh, lat, lon, alt, speed, psi, guess):
    """theta, elevator (rad) and throttle zeroing u_dot, w_dot and q_dot (Q7), level at heading psi,
    phi = beta = 0, aileron = rudder = 0, omega_b the n-frame rate: Newton with a finite-difference
    Jacobian, then a check that the solved residuals are at roundoff."""

    def solved(x: np.ndarray) -> np.ndarray:
        theta, el, throttle = x
        v_b = speed * np.array([math.cos(theta), 0.0, math.sin(theta)])
        w_b = n_frame_rate(lat, alt, speed, psi, theta)
        controls = {"elevator": el, "aileron": 0.0, "rudder": 0.0, "throttle": throttle}
        total, *_ = residual_parts(
            aero, engine, veh, (lat, lon, alt), v_b, (psi, theta, 0.0), w_b, controls
        )
        return total[[0, 2, 4]]

    x = np.array(guess, dtype=float)
    steps = np.array([1e-7, 1e-7, 1e-7])
    for _ in range(50):
        r0 = solved(x)
        jac = np.column_stack(
            [(solved(x + np.eye(3)[k] * steps[k]) - r0) / steps[k] for k in range(3)]
        )
        dx = np.linalg.solve(jac, -r0)
        x = x + dx
        if np.max(np.abs(dx)) < 1e-15:
            break
    if np.max(np.abs(solved(x))) > 1e-12:
        raise StopError(f"the independent trim did not converge: {solved(x)}")
    return x


# --- The constants table (amendment 13) ----------------------------------------------------------


def arithmetic(node: ast.AST, scope: dict[str, float]) -> float:
    """A C++ initializer of numbers, earlier names and + - * /, evaluated without eval()."""
    if isinstance(node, ast.Expression):
        return arithmetic(node.body, scope)
    if isinstance(node, ast.Constant) and isinstance(node.value, (int, float)):
        return float(node.value)
    if isinstance(node, ast.Name) and node.id in scope:
        return scope[node.id]
    if isinstance(node, ast.UnaryOp) and isinstance(node.op, ast.USub):
        return -arithmetic(node.operand, scope)
    if isinstance(node, ast.BinOp):
        left, right = arithmetic(node.left, scope), arithmetic(node.right, scope)
        ops = {ast.Add: left + right, ast.Sub: left - right, ast.Mult: left * right}
        if isinstance(node.op, ast.Div):
            return left / right
        for kind, value in ops.items():
            if isinstance(node.op, kind):
                return value
    raise StopError(f"not a plain arithmetic initializer: {ast.dump(node)}")


def core_constant(path: str, name: str, scope: dict[str, float]) -> float:
    text = (ROOT / path).read_text(encoding="utf-8")
    m = re.search(rf"constexpr double {name}\s*=\s*([^;]+);", text)
    if m is None:
        raise StopError(f"{path}: no constexpr double {name}")
    value = arithmetic(ast.parse(m.group(1).strip(), mode="eval"), scope)
    scope[name] = value
    return value


def constants_log() -> list[str]:
    scope: dict[str, float] = {}
    isa_cpp = "plugins/physical/atmosphere/isa1976/isa1976.cpp"
    geo = "core/include/astra/util/geodesy.hpp"
    units = "core/include/astra/util/units.hpp"
    core = {
        "T0": core_constant(isa_cpp, "kSeaLevelTemperature", scope),
        "P0": core_constant(isa_cpp, "kSeaLevelPressure", scope),
        "g0": core_constant(isa_cpp, "kG0Prime", scope),
        "R*": core_constant(isa_cpp, "kGasConstant", scope),
        "M0": core_constant(isa_cpp, "kMeanMolecularWeight", scope),
        "r0": core_constant(isa_cpp, "kEffectiveRadius", scope),
        "gamma": core_constant(isa_cpp, "kSpecificHeatRatio", scope),
        "a": core_constant(geo, "kSemiMajorAxis", scope),
        "f": core_constant(geo, "kFlattening", scope),
        "omega_ie": core_constant(geo, "kOmegaIe", scope),
        "GM": core_constant(geo, "kGm", scope),
        "J2": core_constant(geo, "kJ2", scope),
        "ft": core_constant(units, "kMetersPerFoot", scope),
        "lbf": core_constant(units, "kNewtonsPerPoundForce", scope),
        "slug": core_constant(units, "kKilogramsPerSlug", scope),
    }
    lapse = re.search(
        r"kLayerGradientKPerM = \{([^,]+),", (ROOT / isa_cpp).read_text(encoding="utf-8")
    )
    core["L0"] = float(lapse.group(1)) if lapse else float("nan")
    ours = {
        "T0": (ISA_T0, "K", "USSA 1976 Table 2"),
        "P0": (ISA_P0, "Pa", "USSA 1976 Table 2"),
        "L0": (ISA_L0, "K/m'", "USSA 1976 Table 4, layer 0"),
        "g0": (ISA_G0, "m/s^2", "USSA 1976 Table 2 (g0')"),
        "R*": (ISA_RSTAR, "J/(mol K)", "USSA 1976 Table 2; the core states 8314.32 J/(kmol K)"),
        "M0": (ISA_M0, "kg/mol", "USSA 1976 eq 20; the core states 28.9644 kg/kmol"),
        "r0": (ISA_R0, "m", "USSA 1976 eq 18; both convert geometric to geopotential"),
        "gamma": (ISA_GAMMA, "-", "USSA 1976 eq 50"),
        "a": (WGS_A, "m", "TR8350.2 eq 3-1"),
        "f": (WGS_F, "-", "TR8350.2 eq 3-2"),
        "omega_ie": (WGS_OMEGA, "rad/s", "TR8350.2 eq 3-6"),
        "GM": (WGS_GM, "m^3/s^2", "TR8350.2 eq 3-3"),
        "J2": (WGS_J2, "-", "the gravity model's declared value (PLAN.md's kJ2 item)"),
        "ft": (FT, "m", "Vol II Table 73"),
        "lbf": (LBF, "N", "Vol II Table 73"),
        "slug": (SLUG, "kg", "lbf s^2/ft from Vol II Table 73, as LBF / FT"),
    }
    log = []
    for key, (value, unit, source) in ours.items():
        theirs = core[key]
        theirs_si = theirs * 1e-3 if key in ("R*", "M0") else theirs  # kmol to mol
        rel = abs(value - theirs_si) / abs(value)
        note = "identical" if value == theirs_si else f"differs by {rel:.3g} relative"
        log.append(f"  {key:8s} {value:.17g} {unit:10s} core {theirs:.17g}  {note:28s} [{source}]")
    ratio_ours = ISA_RSTAR / ISA_M0
    ratio_core = core["R*"] / core["M0"]
    ratio_rel = abs(ratio_ours - ratio_core) / ratio_ours
    ratio_note = "identical" if ratio_ours == ratio_core else f"differs by {ratio_rel:.3g} relative"
    log.append(
        f"  R*/M0 (the only form either code uses): ours {ratio_ours:.17g}, "
        f"core {ratio_core:.17g}, {ratio_note}"
    )
    return log


# --- Main ----------------------------------------------------------------------------------------


def fmt3(v) -> str:
    return ", ".join(f"{float(x): .6e}" for x in v)


def main() -> int:
    refs = main_checkout() / "docs/refs/Atmospheric_models/F16_package/F16_S119_source"
    print("M7 PR 9: Q7's expected trim residual for cases 11 and 12, from the DML alone")
    print(f"P1: {refs}")
    for name, digest in SHA256.items():
        actual = hashlib.sha256((refs / name).read_bytes()).hexdigest()
        if actual != digest:
            raise StopError(f"{name}: SHA-256 {actual}, the addendum says {digest}")
        print(f"  {name} SHA-256 {actual} (matches the addendum's section 1.1)")

    print(
        "\nConstants (amendment 13): this script's value, the core's value read as text, "
        "and its source"
    )
    for line in constants_log():
        print(line)

    aero_dml, prop_dml, inertia_dml = (Dml(refs / n) for n in SHA256)
    aero, engine = Aero(aero_dml), Engine(prop_dml)
    print("\nThis transcription against the DML's checkData:")
    for line in check_shots(aero, engine, aero_dml, prop_dml):
        print(line)

    ini = inertia_dml.initial
    ixx, iyy, izz, izx = (ini[k] * SLUG * FT**2 for k in ("XIXX", "XIYY", "XIZZ", "XIZX"))
    if ini["XIXY"] != 0.0 or ini["XIYZ"] != 0.0:
        raise StopError("F16_inertia.dml: nonzero XIXY or XIYZ")
    veh = Vehicle(
        mass=ini["XMASS"] * SLUG,
        inertia=np.array([[ixx, 0.0, -izx], [0.0, iyy, 0.0], [-izx, 0.0, izz]]),
        dx_ft=0.01 * ini["CBAR"] * (35.0 - CG_PCT_MAC),  # F16_inertia.dml:141-162, DXCG (+FWD)
    )
    print(
        f"\nF16_inertia.dml: mass {ini['XMASS']} slug, Ixx {ini['XIXX']}, Iyy {ini['XIYY']}, "
        f"Izz {ini['XIZZ']}, Izx {ini['XIZX']} slug ft^2; CM {veh.dx_ft:.17g} ft forward of the MRC"
    )

    failures = 0
    for case, rel in CASES:
        path = ROOT / rel
        state, controls, text_log = read_config(path)
        print(f"\n=== {case}: {rel}")
        print("The config's values, read as text (amendments 3 and 12):")
        for line in text_log:
            print(line)
        lat, lon, alt = state["p_n"]
        speed = float(np.linalg.norm(state["v_b"]))
        psi = state["euler_nb"][0]

        x = solve_trim(
            aero,
            engine,
            veh,
            lat,
            lon,
            alt,
            speed,
            psi,
            (state["euler_nb"][1], controls["elevator"], controls["throttle"]),
        )
        theta, el, throttle = x
        diffs = (
            theta - state["euler_nb"][1],
            el - controls["elevator"],
            throttle - controls["throttle"],
        )
        print("\nIndependent trim against the config (amendment 1(b): each within 1e-6):")
        for name, mine, theirs, d in zip(
            ("theta (rad)", "elevator (rad)", "throttle"),
            x,
            (state["euler_nb"][1], controls["elevator"], controls["throttle"]),
            diffs,
            strict=True,
        ):
            ok = abs(d) <= TRIM_AGREEMENT
            failures += 0 if ok else 1
            print(
                f"  {name:15s} script {mine:.17g}  config {theirs:.17g}  diff {d: .3e}  "
                f"{'within' if ok else 'OUTSIDE'} 1e-6"
            )

        sets = {
            "the script's own trim": (
                (lat, lon, alt),
                speed * np.array([math.cos(theta), 0.0, math.sin(theta)]),
                (psi, theta, 0.0),
                n_frame_rate(lat, alt, speed, psi, theta),
                {"elevator": el, "aileron": 0.0, "rudder": 0.0, "throttle": throttle},
            ),
            "the config's literals": (
                tuple(state["p_n"]),
                state["v_b"],
                tuple(state["euler_nb"]),
                state["omega_b"],
                controls,
            ),
        }
        totals = {}
        for label, args in sets.items():
            total, pv, pw, extra = residual_parts(aero, engine, veh, *args)
            totals[label] = total
            static_lat = extra["aero_static_lateral"]
            print(f"\nResidual by parts at {label}:")
            print(
                f"  omega_b {fmt3(args[3])} rad/s; Earth-relative {fmt3(extra['omega_eb'])} rad/s"
            )
            print(
                f"  Mach {extra['mach']:.6f}, density {extra['rho']:.10f} kg/m^3, "
                f"|g| {extra['g']:.10f} m/s^2"
            )
            print(
                f"  aero at the CM: Y {extra['aero_lbf'][0][1]:.10g} lbf, "
                f"L {extra['aero_lbf'][1][0]:.10g}, N {extra['aero_lbf'][1][2]:.10g} lbf ft; "
                "static lateral part (beta = ail = rdr = 0) "
                f"{static_lat[0]:.3g} N, {static_lat[1]:.3g}, {static_lat[2]:.3g} N m"
            )
            for part in ("kinematic", "gravity", "aero", "engine"):
                print(
                    f"  v_dot_y {part:9s} {pv[part][1]: .9e}   p_dot, r_dot {part:9s} "
                    f"{pw[part][0]: .9e} {pw[part][2]: .9e}"
                )
            print(f"  v_dot_y TOTAL     {total[1]: .17g}")
            print(f"  p_dot TOTAL       {total[3]: .17g}")
            print(f"  r_dot TOTAL       {total[5]: .17g}")
            print(f"  solved: u_dot {total[0]: .3e}, w_dot {total[2]: .3e}, q_dot {total[4]: .3e}")

        literals = totals["the config's literals"]
        own = totals["the script's own trim"]
        b = BUDGETS[case]
        print(
            "\nThe constants for nesc_f16_open_loop_test.cpp (the config's literals, %.17g), "
            "with the "
            "plan's pre-registered bounds:"
        )
        for idx, name, bound in ((1, "v_dot_y", b[0]), (3, "p_dot", b[1]), (5, "r_dot", b[2])):
            print(
                f"  {name:8s} {literals[idx]:.17g}  bound {bound:.5g}  "
                f"(own trim minus literals {own[idx] - literals[idx]: .3e})"
            )
        print(
            f"  solved at the literals: u_dot {literals[0]:.3e}, w_dot {literals[2]:.3e}, "
            f"q_dot {literals[4]:.3e} (R4 bounds ASTRA's at 1e-6)"
        )
    print(f"\nderive_residual: {'OK' if failures == 0 else f'{failures} stop condition(s)'}")
    return 0 if failures == 0 else 1


if __name__ == "__main__":
    try:
        sys.exit(main())
    except StopError as error:
        print(f"derive_residual: STOP: {error}")
        sys.exit(1)
