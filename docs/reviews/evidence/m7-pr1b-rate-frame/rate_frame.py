"""The NESC F-16 laws' rate frame, pre-registered (docs/plans/m7-pr1b-adrs.md, Deliverable 1).

Evidence for M7 PR 1b only: the gate never runs this file. It reads the NESC files read-only from
the main checkout's docs/refs (passed as --refs) and never copies them. Standard library only.

    python3 -I rate_frame.py --refs <main checkout>/docs/refs --phase floor   > floor.log
    python3 -I rate_frame.py --refs <main checkout>/docs/refs --phase compare > compare.log

Phase 1 runs in both: the self-checks (1a to 1d). The floor phase adds phase 2: the law self-check
(amendment 3), the signal, the noise floor, the counts and each member's deciding case. It never
prints the law's outputs. The compare phase reruns phases 1 and 2 silently, then runs phase 3: the
comparison, the verdicts, the result and the reported variants. A failed self-check exits 1 before
any later phase. Every number below that is a decision-rule parameter is fixed by the plan.

The XML parser is the standard library's: phase 1a checks every DML file's SHA-256 against the
addendum before any of them is parsed.
"""

from __future__ import annotations

import argparse
import bisect
import csv
import hashlib
import math
import operator
import re
import struct
import sys
from collections.abc import Callable, Iterable
from dataclasses import dataclass, field
from decimal import Decimal
from functools import reduce
from itertools import product
from pathlib import Path
from xml.etree import ElementTree

# --- Inputs (read-only, relative to --refs) -------------------------------------------------------

DML_DIR = Path("Atmospheric_models/F16_package/F16_S119_source")
# Addendum (docs/plans/m7-f16-inventory.md) section 1.1.
DML_SHA256 = {
    "F16_aero.dml": "82796577b989b5f3becb7d13430ff6c619b3ecd2e642a6427b743658fb177e51",
    "F16_control.dml": "bca7789dde61b741de69f697ce8799ee666214d0e1fa203973523774fc3a969b",
    "F16_gnc.dml": "fc1dbff00cc14b0228973c43cfc17797fd48d2e36950ec039ff4e77d8577827c",
    "F16_inertia.dml": "e11d4eba5b158d24db7034bd85682ad0b896dd2f3172f007ca3d6d6d4e693c4a",
}
CASE_DIR = "Atmospheric_checkcases"
CASE_CSV = {
    "11": CASE_DIR + "/Atmos_11_TrimCheckSubsonicF16/Atmos_11_sim_0{member}.csv",
    "13.3": CASE_DIR + "/Atmos_13p3_SubsonicHeadingChangeF16/Atmos_13p3_sim_0{member}.csv",
    "15": CASE_DIR + "/Atmos_15_CircleNorthPole/Atmos_15_sim_0{member}.csv",
}
MEMBERS = (4, 5)

# --- Constants from sources ---------------------------------------------------------------------

OMEGA_IE = 7.292115e-5  # rad/s, Vol II Table 73 (atmospheric check-cases)
WGS84_A_FT = 20925646.32546  # ft, Vol II Table 73 (6378137.0 m); H_N only
WGS84_INV_F = 298.257223563  # Vol II Table 73; H_N only
M_PER_FT = 0.3048  # exact
SLUG_PER_KG = 0.3048 / 4.4482216152605  # Vol II Table 73
RHO0_KG_M3 = 1.225  # US Standard Atmosphere 1976, sea level; el (reported) only
M_PER_S_PER_KNOT = 1852.0 / 3600.0  # the exact knot; el (reported) only
RAD_PER_DEG = math.pi / 180.0  # exact pi, for the CSVs' degrees
CG_PCT_MAC = 25.0  # Vol II Table 7; README; addendum D4

# --- The decision rule's numbers, fixed by the plan (Deliverable 1 (c), (d)) -------------------

RATIO_MIN = 10.0  # a sample counts for a surface when |delta| >= 10 F (and delta != 0)
SUPPORT_FRACTION = 0.25  # a counting sample supports H when |e(H)| <= |delta| / 4
MIN_COUNTING = 10  # per member, counting (sample, surface) pairs over ail and rdr together
HOLD_S = 0.02  # F_t's delta: one 100 Hz hold plus one period of compute delay
SAMPLE_S = 0.1  # 10 Hz
MATCH_TOL_S = 1e-5  # ADR-0028
FIRST_K = 80  # t = 8.0 s, footnote (a)
STEP_EXCLUSION_K = 2  # |t - 15.0 s| <= 0.2 s
STEP_K_13P3 = 150  # Table 39: the course command steps at t = 15 s
SELF_CHECK_REL = 1e-12  # amendment 3
SELF_CHECK_ABS_DEG = 1e-12  # amendment 3
PHASE1C_REL = 1e-7  # 1c


@dataclass(frozen=True)
class CaseSpec:
    name: str
    law_file: str
    last_k: int
    step_k: int | None
    segments: tuple[tuple[str, int, int], ...]


CASE_13P3 = CaseSpec(
    name="13.3",
    law_file="F16_control.dml",
    last_k=300,  # Table 39: 30 s
    step_k=STEP_K_13P3,
    segments=(("8 to 15 s", 0, 149), ("after the step", 151, 300)),
)
CASE_15 = CaseSpec(
    name="15",
    law_file="F16_gnc.dml",
    last_k=1800,  # Table 42: 180 s
    step_k=None,
    segments=(("8 to 180 s", 0, 1800),),
)


def base_chi_cmd_13p3(k: int) -> float:
    """Table 39: the course command is 45 deg, and 60 deg from t = 15 s."""
    return 45.0 if k < STEP_K_13P3 else 60.0


# --- A small DAVE-ML evaluator --------------------------------------------------------------------

DAVE = "{http://daveml.org/2010/DAVEML}"


def local_name(tag: str) -> str:
    return tag.rsplit("}", 1)[-1]


def parse_numbers(text: str) -> list[float]:
    return [float(item) for item in re.split(r"[,\s]+", text.strip()) if item]


@dataclass
class Table:
    independent: list[tuple[str, float, float]]  # (varID, min, max), extrapolate="neither" only
    breakpoints: list[list[float]]
    data: list[float]


class DaveModel:
    """Runs a DAVE-ML file as written: MathML calculations, gridded tables with linear
    interpolation and clip (addendum sections 2 and 3), and every minValue/maxValue as a
    saturation (section 8). An unknown element stops the run."""

    def __init__(self, path: Path) -> None:
        root = ElementTree.parse(path).getroot()
        self.variables: dict[str, ElementTree.Element] = {}
        self.name_to_id: dict[str, str] = {}
        breakpoint_defs: dict[str, list[float]] = {}
        function_elements = []
        for child in root:
            kind = local_name(child.tag)
            if kind == "variableDef":
                var_id = child.get("varID")
                if var_id is None or var_id in self.variables:
                    raise ValueError(f"{path.name}: bad or duplicate varID {var_id!r}")
                self.variables[var_id] = child
                self.name_to_id[child.get("name", "")] = var_id
            elif kind == "breakpointDef":
                breakpoint_defs[child.get("bpID", "")] = parse_numbers(
                    child.findtext(DAVE + "bpVals", "")
                )
            elif kind == "function":
                function_elements.append(child)
            elif kind not in ("fileHeader", "checkData"):
                raise ValueError(f"{path.name}: unknown top-level element {kind}")
        self.tables: dict[str, Table] = {}
        for function in function_elements:
            self.tables.update(self._parse_function(path, function, breakpoint_defs))
        self.check_data = root.find(DAVE + "checkData")

    @staticmethod
    def _parse_function(
        path: Path, function: ElementTree.Element, breakpoint_defs: dict[str, list[float]]
    ) -> dict[str, Table]:
        independent = []
        for ref in function.findall(DAVE + "independentVarRef"):
            if ref.get("extrapolate") != "neither" or ref.get("interpolate") not in (
                None,
                "linear",
            ):
                raise ValueError(f"{path.name}: unsupported independentVarRef {ref.attrib}")
            independent.append(
                (ref.get("varID", ""), float(ref.get("min", "")), float(ref.get("max", "")))
            )
        dependent = function.find(DAVE + "dependentVarRef")
        table = function.find(f"{DAVE}functionDefn/{DAVE}griddedTableDef")
        if dependent is None or table is None:
            raise ValueError(f"{path.name}: function {function.get('name')} is not a gridded table")
        refs = table.find(DAVE + "breakpointRefs")
        if refs is None:
            raise ValueError(f"{path.name}: function {function.get('name')} has no breakpointRefs")
        breakpoints = [breakpoint_defs[ref.get("bpID", "")] for ref in refs]
        data = parse_numbers(table.findtext(DAVE + "dataTable", ""))
        if len(breakpoints) != len(independent):
            raise ValueError(f"{path.name}: {function.get('name')}: dimension mismatch")
        if len(data) != math.prod(len(points) for points in breakpoints):
            raise ValueError(f"{path.name}: {function.get('name')}: value count mismatch")
        for (_, low, high), points in zip(independent, breakpoints, strict=True):
            if (low, high) != (points[0], points[-1]):
                raise ValueError(
                    f"{path.name}: {function.get('name')}: min/max off the breakpoints"
                )
        return {dependent.get("varID", ""): Table(independent, breakpoints, data)}

    def evaluate(self, inputs: dict[str, float], wanted: Iterable[str]) -> dict[str, float]:
        for var_id in inputs:
            element = self.variables[var_id]  # an unknown name raises
            if var_id in self.tables or element.find(DAVE + "calculation") is not None:
                raise ValueError(f"{var_id} is computed by the model and cannot be an input")
        values: dict[str, float] = {}

        def value(var_id: str) -> float:
            if var_id in values:
                return values[var_id]
            element = self.variables[var_id]
            calculation = element.find(DAVE + "calculation")
            if var_id in inputs:
                result = float(inputs[var_id])
            elif var_id in self.tables:
                result = self._interpolate(self.tables[var_id], value)
            elif calculation is not None:
                result = float(self._math(next(iter(calculation)), value))
            elif element.get("initialValue") is not None:
                result = float(element.get("initialValue", ""))
            else:
                raise KeyError(f"input {var_id} not supplied")
            low, high = element.get("minValue"), element.get("maxValue")
            if low is not None:
                result = max(result, float(low))
            if high is not None:
                result = min(result, float(high))
            values[var_id] = result
            return result

        return {var_id: value(var_id) for var_id in wanted}

    @staticmethod
    def _interpolate(table: Table, value: Callable[[str], float]) -> float:
        cells = []
        for (var_id, low, high), points in zip(table.independent, table.breakpoints, strict=True):
            x = min(max(value(var_id), low), high)  # "neither" read as clip (F16_aero_create.m)
            index = min(max(bisect.bisect_right(points, x) - 1, 0), len(points) - 2)
            fraction = (x - points[index]) / (points[index + 1] - points[index])
            cells.append((index, fraction))
        strides = [
            math.prod(len(points) for points in table.breakpoints[i + 1 :])
            for i in range(len(cells))
        ]
        result = 0.0
        for corner in product((0, 1), repeat=len(cells)):
            weight = 1.0
            offset = 0
            for (index, fraction), bit, stride in zip(cells, corner, strides, strict=True):
                weight *= fraction if bit else 1.0 - fraction
                offset += (index + bit) * stride
            result += weight * table.data[offset]
        return result

    def _math(self, node: ElementTree.Element, value: Callable[[str], float]) -> float | bool:
        kind = local_name(node.tag)
        if kind == "math":
            (child,) = list(node)
            return self._math(child, value)
        if kind == "cn":
            return float(node.text or "")
        if kind == "ci":
            return value((node.text or "").strip())
        if kind == "piecewise":
            for piece in node:
                piece_kind = local_name(piece.tag)
                if piece_kind == "piece":
                    result_node, condition = list(piece)
                    if self._math(condition, value):
                        return self._math(result_node, value)
                elif piece_kind == "otherwise":
                    (result_node,) = list(piece)
                    return self._math(result_node, value)
                else:
                    raise ValueError(f"unknown piecewise element {piece_kind}")
            raise ValueError("piecewise with no true piece and no otherwise")
        if kind != "apply":
            raise ValueError(f"unknown MathML element {kind}")
        operator_node, *argument_nodes = list(node)
        op = local_name(operator_node.tag)
        if op == "piecewise":
            if argument_nodes:
                raise ValueError("apply of piecewise with arguments")
            return self._math(operator_node, value)
        args = [float(self._math(argument, value)) for argument in argument_nodes]
        if op == "csymbol":
            if (operator_node.text or "").strip() == "atan2" and len(args) == 2:
                return math.atan2(args[0], args[1])
            raise ValueError(f"unknown csymbol {operator_node.text!r}")
        binary = {
            "divide": operator.truediv,
            "power": operator.pow,
            "gt": operator.gt,
            "lt": operator.lt,
            "geq": operator.ge,
            "leq": operator.le,
        }
        unary = {"abs": abs, "cos": math.cos, "sin": math.sin}
        if op == "plus" and args:
            return reduce(operator.add, args)
        if op == "times" and args:
            return reduce(operator.mul, args)
        if op == "minus" and len(args) == 1:
            return -args[0]
        if op == "minus" and len(args) == 2:
            return args[0] - args[1]
        if op in binary and len(args) == 2:
            return binary[op](args[0], args[1])
        if op in unary and len(args) == 1:
            return unary[op](args[0])
        raise ValueError(f"unknown MathML operator {op} with {len(args)} arguments")


# --- The members' data ----------------------------------------------------------------------------


def float32_round_trips(x: float) -> bool:
    try:
        return struct.unpack("<f", struct.pack("<f", x))[0] == x
    except OverflowError:
        return False


def half_float32_ulp(x: float) -> float:
    bits = struct.unpack("<I", struct.pack("<f", abs(x)))[0]
    above = struct.unpack("<f", struct.pack("<I", bits + 1))[0]
    return 0.5 * (above - abs(x))


def significant_digits(text: str) -> int:
    digits = list(Decimal(text).as_tuple().digits)
    while len(digits) > 1 and digits[0] == 0:
        digits.pop(0)
    return len(digits)


@dataclass
class Column:
    index: int
    float32: bool
    digits: int  # P: the most significant digits any value in the column prints (amendment 15)


class CaseFile:
    def __init__(self, refs: Path, relative: str) -> None:
        self.relative = relative
        raw = (refs / relative).read_bytes()
        self.sha256 = hashlib.sha256(raw).hexdigest()
        rows = list(csv.reader(raw.decode("ascii").splitlines()))
        self.header = rows[0]
        self.rows = rows[1:]
        self.columns: dict[str, Column] = {}
        self.duplicates: list[str] = []
        for index, name in enumerate(self.header):
            if name in self.columns:
                first = self.columns[name].index
                if any(float(row[first]) != float(row[index]) for row in self.rows):
                    raise ValueError(f"{relative}: duplicate column {name} disagrees")
                self.duplicates.append(name)
                continue
            texts = [row[index] for row in self.rows]
            self.columns[name] = Column(
                index=index,
                float32=all(float32_round_trips(float(text)) for text in texts),
                digits=max(significant_digits(text) for text in texts),
            )
        self.row_of_k: dict[int, list[str]] = {}
        time_index = self.columns["time"].index
        for row in self.rows:
            t = float(row[time_index])
            k = round(t / SAMPLE_S)
            if abs(t - k / 10.0) <= MATCH_TOL_S:
                if k in self.row_of_k:
                    raise ValueError(f"{relative}: two rows match t = {k / 10.0}")
                self.row_of_k[k] = row

    def text(self, k: int, name: str) -> str:
        return self.row_of_k[k][self.columns[name].index]

    def value(self, k: int, name: str) -> float:
        return float(self.text(k, name))

    def uncertainty(self, k: int, name: str) -> float:
        """u(x), amendments 1 and 15."""
        column = self.columns[name]
        text = self.text(k, name)
        x = float(text)
        if column.float32:
            return half_float32_ulp(x)
        if x == 0.0:
            return 0.0
        leading = Decimal(text).adjusted()
        return 0.5 * 10.0 ** (leading - column.digits + 1)


YAW, PITCH, ROLL = "eulerAngle_deg_Yaw", "eulerAngle_deg_Pitch", "eulerAngle_deg_Roll"
V_NORTH, V_EAST, V_DOWN = "feVelocity_ft_s_X", "feVelocity_ft_s_Y", "feVelocity_ft_s_Z"
RATE_P, RATE_Q, RATE_R = (
    "bodyAngularRateWrtEi_deg_s_Roll",
    "bodyAngularRateWrtEi_deg_s_Pitch",
    "bodyAngularRateWrtEi_deg_s_Yaw",
)
LATITUDE, LONGITUDE, ALTITUDE = "latitude_deg", "longitude_deg", "altitudeMsl_ft"
QBAR, DENSITY = "dynamicPressure_lbf_ft2", "airDensity_slug_ft3"
FORCE_X, FORCE_Y, FORCE_Z = "aero_bodyForce_lbf_X", "aero_bodyForce_lbf_Y", "aero_bodyForce_lbf_Z"
MOMENT_L, MOMENT_M, MOMENT_N = (
    "aero_bodyMoment_ftlbf_L",
    "aero_bodyMoment_ftlbf_M",
    "aero_bodyMoment_ftlbf_N",
)
# Every CSV value the lateral back-out reads, and every one the lateral law reads.
BACKOUT_COLUMNS = (
    YAW,
    PITCH,
    ROLL,
    V_NORTH,
    V_EAST,
    V_DOWN,
    RATE_P,
    RATE_Q,
    RATE_R,
    LATITUDE,
    QBAR,
    FORCE_Y,
    MOMENT_L,
    MOMENT_N,
)
LAW_COLUMNS = {
    "13.3": (YAW, PITCH, ROLL, V_NORTH, V_EAST, V_DOWN, RATE_P, RATE_Q, RATE_R, LATITUDE),
    "15": (YAW, PITCH, ROLL, V_NORTH, V_EAST, V_DOWN, RATE_P, RATE_Q, RATE_R, LATITUDE, LONGITUDE),
}

Vector = tuple[float, float, float]
Matrix = tuple[Vector, Vector, Vector]


def matvec(m: Matrix, v: Vector) -> Vector:
    return (
        m[0][0] * v[0] + m[0][1] * v[1] + m[0][2] * v[2],
        m[1][0] * v[0] + m[1][1] * v[1] + m[1][2] * v[2],
        m[2][0] * v[0] + m[2][1] * v[1] + m[2][2] * v[2],
    )


def subtract(a: Vector, b: Vector) -> Vector:
    return (a[0] - b[0], a[1] - b[1], a[2] - b[2])


def dcm_n_to_b(psi: float, theta: float, phi: float) -> Matrix:
    """3-2-1 Euler angles (rad) to the matrix that rotates n (NED) into b (FRD), ADR-0001."""
    cps, sps = math.cos(psi), math.sin(psi)
    cth, sth = math.cos(theta), math.sin(theta)
    cph, sph = math.cos(phi), math.sin(phi)
    return (
        (cth * cps, cth * sps, -sth),
        (sph * sth * cps - cph * sps, sph * sth * sps + cph * cps, sph * cth),
        (cph * sth * cps + sph * sps, cph * sth * sps - sph * cps, cph * cth),
    )


@dataclass
class Sample:
    phi: float  # deg
    theta: float  # deg
    psi: float  # deg
    vt: float  # ft/s
    alpha: float  # deg
    beta: float  # deg
    latitude: float  # deg
    longitude: float  # deg
    qbar: float  # lbf/ft2
    density: float  # slug/ft3
    force: Vector  # lbf, body
    moment_cm: Vector  # lbf ft, about the CM
    rates: dict[str, Vector] = field(default_factory=dict)  # rad/s: "I", "E", "N"


def build_sample(get: Callable[[str], float]) -> Sample:
    psi, theta, phi = get(YAW), get(PITCH), get(ROLL)
    c_bn = dcm_n_to_b(psi * RAD_PER_DEG, theta * RAD_PER_DEG, phi * RAD_PER_DEG)
    v_n = (get(V_NORTH), get(V_EAST), get(V_DOWN))
    u, v, w = matvec(c_bn, v_n)  # no wind: air-relative
    vt = math.sqrt(u * u + v * v + w * w)
    latitude = get(LATITUDE)
    lat = latitude * RAD_PER_DEG
    omega_ie_b = matvec(c_bn, (OMEGA_IE * math.cos(lat), 0.0, -OMEGA_IE * math.sin(lat)))
    # H_N only: the transport rate on WGS-84 (Table 73's a and f).
    f = 1.0 / WGS84_INV_F
    e2 = f * (2.0 - f)
    s2 = math.sin(lat) ** 2
    radius_n = WGS84_A_FT / math.sqrt(1.0 - e2 * s2)
    radius_m = WGS84_A_FT * (1.0 - e2) / (1.0 - e2 * s2) ** 1.5
    h = get(ALTITUDE)
    omega_en_n = (
        v_n[1] / (radius_n + h),
        -v_n[0] / (radius_m + h),
        -v_n[1] * math.tan(lat) / (radius_n + h),
    )
    omega_en_b = matvec(c_bn, omega_en_n)
    inertial = (get(RATE_P) * RAD_PER_DEG, get(RATE_Q) * RAD_PER_DEG, get(RATE_R) * RAD_PER_DEG)
    earth = subtract(inertial, omega_ie_b)
    return Sample(
        phi=phi,
        theta=theta,
        psi=psi,
        vt=vt,
        alpha=math.degrees(math.atan2(w, u)),
        beta=math.degrees(math.asin(v / vt)),
        latitude=latitude,
        longitude=get(LONGITUDE),
        qbar=get(QBAR),
        density=get(DENSITY),
        force=(get(FORCE_X), get(FORCE_Y), get(FORCE_Z)),
        moment_cm=(get(MOMENT_L), get(MOMENT_M), get(MOMENT_N)),
        rates={"I": inertial, "E": earth, "N": subtract(earth, omega_en_b)},
    )


def sample_at(data: CaseFile, k: int, perturb: tuple[str, float] | None = None) -> Sample:
    def get(name: str) -> float:
        x = data.value(k, name)
        if perturb is not None and perturb[0] == name:
            x += perturb[1]
        return x

    return build_sample(get)


# --- The back-out (D3's aero with Earth-relative rates, D4's transfer) ----------------------------


@dataclass(frozen=True)
class Geometry:
    sref: float
    bspan: float
    cbar: float
    dxcg: float  # ft, CM forward of the MRC


def aero_inputs(s: Sample, rates: Vector, el: float, ail: float, rdr: float) -> dict[str, float]:
    return {
        "vt": s.vt,
        "alpha": s.alpha,
        "beta": s.beta,
        "p": rates[0],
        "q": rates[1],
        "r": rates[2],
        "el": el,
        "ail": ail,
        "rdr": rdr,
    }


def measured_coefficients(s: Sample, g: Geometry) -> dict[str, float]:
    x, y, z = s.force
    moment_l, moment_m_cm, moment_n_cm = s.moment_cm
    q_s = s.qbar * g.sref
    return {
        "cx": x / q_s,
        "cy": y / q_s,
        "cz": z / q_s,
        "cl": moment_l / (q_s * g.bspan),
        "cm": (moment_m_cm - g.dxcg * z) / (q_s * g.cbar),  # M_mrc = M_cm - DXCG Z
        "cn": (moment_n_cm + g.dxcg * y) / (q_s * g.bspan),  # N_mrc = N_cm + DXCG Y
    }


LATERAL = ("cy", "cl", "cn")
LONGITUDINAL = ("cx", "cz", "cm")


def solve2(
    a: tuple[tuple[float, float], tuple[float, float]], b: tuple[float, float]
) -> tuple[float, float]:
    det = a[0][0] * a[1][1] - a[0][1] * a[1][0]
    return ((b[0] * a[1][1] - a[0][1] * b[1]) / det, (a[0][0] * b[1] - b[0] * a[1][0]) / det)


@dataclass
class LateralBackout:
    ail: float
    rdr: float
    residual: Vector  # cy, cl, cn
    spread: tuple[float, float]  # F_b for ail, rdr


def backout_lateral(aero: DaveModel, s: Sample, g: Geometry, aero_rates: Vector) -> LateralBackout:
    def coefficients(ail: float, rdr: float) -> Vector:
        values = aero.evaluate(aero_inputs(s, aero_rates, 0.0, ail, rdr), LATERAL)
        return (values["cy"], values["cl"], values["cn"])

    base = coefficients(0.0, 0.0)
    unit_ail = subtract(coefficients(20.0, 0.0), base)  # dail = ail / 20 = 1
    unit_rdr = subtract(coefficients(0.0, 30.0), base)  # drdr = rdr / 30 = 1
    both = subtract(coefficients(20.0, 30.0), base)
    for i in range(3):  # the build-up is linear in dail and drdr (addendum section 2)
        if abs(both[i] - unit_ail[i] - unit_rdr[i]) > 1e-12 * (1.0 + abs(both[i])):
            raise ValueError("lateral coefficients are not linear in ail and rdr")
    measured = measured_coefficients(s, g)
    rhs = tuple(measured[name] - base[i] for i, name in enumerate(LATERAL))
    a11 = sum(unit_ail[i] * unit_ail[i] for i in range(3))
    a12 = sum(unit_ail[i] * unit_rdr[i] for i in range(3))
    a22 = sum(unit_rdr[i] * unit_rdr[i] for i in range(3))
    b1 = sum(unit_ail[i] * rhs[i] for i in range(3))
    b2 = sum(unit_rdr[i] * rhs[i] for i in range(3))
    dail, drdr = solve2(((a11, a12), (a12, a22)), (b1, b2))
    residual = tuple(rhs[i] - unit_ail[i] * dail - unit_rdr[i] * drdr for i in range(3))
    spread_ail = 0.0
    spread_rdr = 0.0
    for i, j in ((0, 1), (0, 2), (1, 2)):
        pair_ail, pair_rdr = solve2(
            ((unit_ail[i], unit_rdr[i]), (unit_ail[j], unit_rdr[j])), (rhs[i], rhs[j])
        )
        spread_ail = max(spread_ail, abs(20.0 * (pair_ail - dail)))
        spread_rdr = max(spread_rdr, abs(30.0 * (pair_rdr - drdr)))
    return LateralBackout(
        ail=20.0 * dail,
        rdr=30.0 * drdr,
        residual=(residual[0], residual[1], residual[2]),
        spread=(spread_ail, spread_rdr),
    )


EL_NODES = (-25.0, -24.0, -12.0, 0.0, 12.0, 24.0, 25.0)  # cx, cz, cm are affine between these


def backout_el(aero: DaveModel, s: Sample, g: Geometry, aero_rates: Vector) -> tuple[float, float]:
    """el by least squares over cx, cz, cm within each affine cell, keeping the smallest residual;
    returns (el, the largest single-channel distance from it). Reported only."""
    measured = measured_coefficients(s, g)
    nodes = [
        aero.evaluate(aero_inputs(s, aero_rates, el, 0.0, 0.0), LONGITUDINAL) for el in EL_NODES
    ]
    best: tuple[float, float, float, int] | None = None  # (sum of squares, el, low, cell)
    for cell in range(len(EL_NODES) - 1):
        low, high = EL_NODES[cell], EL_NODES[cell + 1]
        slopes = [(nodes[cell + 1][c] - nodes[cell][c]) / (high - low) for c in LONGITUDINAL]
        offsets = [measured[c] - nodes[cell][c] for c in LONGITUDINAL]
        denominator = sum(slope * slope for slope in slopes)
        el = low + sum(m * d for m, d in zip(slopes, offsets, strict=True)) / denominator
        el = min(max(el, low), high)
        squares = sum((d - m * (el - low)) ** 2 for m, d in zip(slopes, offsets, strict=True))
        if best is None or squares < best[0]:
            best = (squares, el, low, cell)
    assert best is not None
    _, el, low, cell = best
    spread = 0.0
    for c in LONGITUDINAL:
        slope = (nodes[cell + 1][c] - nodes[cell][c]) / (EL_NODES[cell + 1] - low)
        if slope != 0.0:
            spread = max(spread, abs(low + (measured[c] - nodes[cell][c]) / slope - el))
    return el, spread


# --- The law -----------------------------------------------------------------------------------


def lateral_law_inputs(case: CaseSpec, k: int, s: Sample, rates: Vector) -> dict[str, float]:
    inputs = {
        "apOn": 1.0,
        "sasOn": 1.0,
        "latStk": 0.0,
        "pedal": 0.0,
        "beta": s.beta,
        "psi": s.psi,
        "phi": s.phi,
        "pb": rates[0],
        "rb": rates[2],
    }
    if case.name == "13.3":
        inputs.update({"latOffset": 0.0, "baseChiCmd": base_chi_cmd_13p3(k)})
    else:
        inputs.update(
            {"circlePoleSW": 1.0, "ownshipN_deg": s.latitude, "ownshipE_deg": s.longitude}
        )
    return inputs


def law_lateral(law: DaveModel, case: CaseSpec, k: int, s: Sample, rates: Vector) -> Vector:
    values = law.evaluate(lateral_law_inputs(case, k, s, rates), ("ail", "rdr"))
    return (values["ail"], values["rdr"], 0.0)


# Amendment 3: addendum section 5's lateral law, written from its text and its printed gains, not
# from the MathML.
LAT_GAINS = (3.078043941515770, 0.032365863044163, 4.557858908828332, 0.589443156647647)
DIR_GAINS = (-0.705817452754520, -0.256362860634868, -1.073666149713151, 0.822114635953878)


def clamp(x: float, low: float, high: float) -> float:
    return min(max(x, low), high)


def closed_form_lateral(case: CaseSpec, k: int, s: Sample, rates: Vector) -> Vector:
    if case.name == "13.3":
        lat_offset, base_chi_cmd = 0.0, base_chi_cmd_13p3(k)
    else:  # section 5's polar navigator: deg_to_ft = 60 * 6076.12, circleRadius_ft = 3 * 6076.12
        lat_offset = 60.0 * 6076.12 * (90.0 - s.latitude) - 3.0 * 6076.12
        base_chi_cmd = 90.0
    chi_est = s.beta + s.psi
    chi_cmd = clamp(-0.01 * lat_offset, -30.0, 30.0) + base_chi_cmd
    chi_err = chi_est - chi_cmd
    if abs(chi_err) > 180.0:
        chi_err = chi_err - 360.0 if chi_err > 0.0 else chi_err + 360.0
    delta_phi = s.phi - clamp(-10.0 * chi_err, -30.0, 30.0)
    terms = (delta_phi, s.beta, rates[0], rates[2])
    lat_lqr = -sum(gain * term for gain, term in zip(LAT_GAINS, terms, strict=True))
    dir_lqr = -sum(gain * term for gain, term in zip(DIR_GAINS, terms, strict=True))
    ail = -21.5 * clamp(lat_lqr, -1.0, 1.0)
    rdr = -30.0 * clamp(dir_lqr, -1.0, 1.0) + 0.008 * ail
    return (ail, rdr, 0.0)


# --- Phases ------------------------------------------------------------------------------------


class Output:
    def __init__(self, enabled: bool) -> None:
        self.enabled = enabled

    def line(self, text: str = "") -> None:
        if self.enabled:
            print(text)


def fail(message: str) -> None:
    print(f"FAIL: {message}")
    sys.exit(1)


@dataclass
class Context:
    refs: Path
    aero: DaveModel
    inertia: DaveModel
    laws: dict[str, DaveModel]
    geometry: Geometry
    files: dict[tuple[str, int], CaseFile] = field(default_factory=dict)

    def case_file(self, case: str, member: int) -> CaseFile:
        key = (case, member)
        if key not in self.files:
            self.files[key] = CaseFile(self.refs, CASE_CSV[case].format(member=member))
        return self.files[key]


def describe_file(out: Output, data: CaseFile) -> None:
    out.line(f"{data.relative}  sha256 {data.sha256}")
    for name in data.duplicates:
        out.line(f"  duplicate column {name}: equal to its first copy in every row")
    for name, column in data.columns.items():
        kind = "float32" if column.float32 else "decimal"
        out.line(f"  {name}: {kind}, P = {column.digits}")


def phase1(refs: Path, out: Output) -> Context:
    out.line("== Phase 1: self-checks")
    for name, expected in DML_SHA256.items():
        actual = hashlib.sha256((refs / DML_DIR / name).read_bytes()).hexdigest()
        out.line(f"1a {name} sha256 {actual} {'ok' if actual == expected else 'MISMATCH'}")
        if actual != expected:
            fail(f"1a: {name} does not match addendum section 1.1")
    aero = DaveModel(refs / DML_DIR / "F16_aero.dml")
    inertia = DaveModel(refs / DML_DIR / "F16_inertia.dml")
    laws = {
        "13.3": DaveModel(refs / DML_DIR / "F16_control.dml"),
        "15": DaveModel(refs / DML_DIR / "F16_gnc.dml"),
    }
    constants = aero.evaluate({}, ("sref", "bspan", "cbar"))
    dxcg = inertia.evaluate({"CG_PCT_MAC": CG_PCT_MAC}, ("DXCG",))["DXCG"]
    geometry = Geometry(constants["sref"], constants["bspan"], constants["cbar"], dxcg)
    out.line(
        f"   sref {geometry.sref!r} ft2, bspan {geometry.bspan!r} ft, cbar {geometry.cbar!r} ft, "
        f"DXCG {geometry.dxcg!r} ft at CG {CG_PCT_MAC!r} % MAC"
    )

    # 1b: F16_aero.dml's checkData, by the file's own test |y - y_good| < tol.
    assert aero.check_data is not None
    shots = aero.check_data.findall(DAVE + "staticShot")
    for shot in shots:
        inputs = {}
        for signal in shot.findall(f"{DAVE}checkInputs/{DAVE}signal"):
            inputs[aero.name_to_id[signal.findtext(DAVE + "signalName", "").strip()]] = float(
                signal.findtext(DAVE + "signalValue", "")
            )
        expected_outputs = []
        for signal in shot.findall(f"{DAVE}checkOutputs/{DAVE}signal"):
            expected_outputs.append(
                (
                    aero.name_to_id[signal.findtext(DAVE + "signalName", "").strip()],
                    float(signal.findtext(DAVE + "signalValue", "")),
                    float(signal.findtext(DAVE + "tol", "")),
                )
            )
        values = aero.evaluate(inputs, [var_id for var_id, _, _ in expected_outputs])
        worst = max(abs(values[v] - good) / tol for v, good, tol in expected_outputs)
        passed = all(abs(values[v] - good) < tol for v, good, tol in expected_outputs)
        out.line(
            f"1b {shot.get('name')}: {len(expected_outputs)} outputs, "
            f"max |y - y_good|/tol {worst!r}"
        )
        if not passed:
            fail(f"1b: checkData shot {shot.get('name')}")
    out.line(f"1b {len(shots)} shots pass")

    context = Context(refs, aero, inertia, laws, geometry)

    # 1c: D3's chain on SIM 5's case 11 at t = 0.
    data = context.case_file("11", 5)
    describe_file(out, data)
    s = sample_at(data, 0)
    values = aero.evaluate(aero_inputs(s, s.rates["E"], 0.0, 0.0, 0.0), LATERAL)
    q_s = s.qbar * geometry.sref
    predicted_y = q_s * values["cy"]
    predicted = (
        predicted_y,
        q_s * geometry.bspan * values["cl"],
        q_s * geometry.bspan * values["cn"] - geometry.dxcg * predicted_y,  # N_cm
    )
    recorded = (s.force[1], s.moment_cm[0], s.moment_cm[2])
    for name, p, r in zip(("Y", "L", "N"), predicted, recorded, strict=True):
        relative = abs(p - r) / abs(r)
        out.line(
            f"1c case 11 SIM 5 t = 0 {name}: predicted {p!r}, CSV {r!r}, relative {relative!r}"
        )
        if relative > PHASE1C_REL:
            fail(f"1c: {name} off by {relative!r} relative")

    # 1d: the disengaged law with the DML's default trims (README Table 11).
    disengaged = {name: 0.0 for name in ("apOn", "sasOn", "longStk", "latStk", "pedal", "throttle")}
    values = laws["13.3"].evaluate(disengaged, ("el", "PWR"))
    out.line(f"1d disengaged law: el {values['el']!r} deg, PWR {values['PWR']!r} %")
    if abs(values["el"] - -3.2410) > 0.5e-4 or abs(values["PWR"] - 13.9019) > 0.5e-4:
        fail("1d: the disengaged law misses README Table 11's -3.2410 deg and 13.9019 %")
    return context


@dataclass
class Row:
    k: int
    backed: Vector  # ail, rdr
    residual: Vector
    delta: Vector  # law(H_I) - law(H_E), ail and rdr
    floor_b: tuple[float, float]
    floor_t: tuple[float, float]
    floor_p: tuple[float, float]
    counts: tuple[bool, bool]


@dataclass
class MemberCase:
    member: int
    case: CaseSpec
    rows: list[Row]
    law: dict[tuple[str, int], Vector]  # (H, k) -> ail, rdr; never printed in phase 2
    backed: dict[int, LateralBackout]

    @property
    def counting(self) -> int:
        return sum(row.counts[0] + row.counts[1] for row in self.rows)


def usable(case: CaseSpec, k: int) -> bool:
    if not FIRST_K <= k <= case.last_k - 1:  # both neighbors inside 7.9 s to the case's end
        return False
    return case.step_k is None or abs(k - case.step_k) > STEP_EXCLUSION_K


def law_self_check(context: Context, case: CaseSpec, member: int, out: Output) -> None:
    data = context.case_file(case.name, member)
    law = context.laws[case.name]
    worst_abs = 0.0
    worst_ratio = 0.0
    count = 0
    for k in range(case.last_k + 1):
        s = sample_at(data, k)
        for hypothesis in ("I", "E"):
            evaluated = law_lateral(law, case, k, s, s.rates[hypothesis])
            expected = closed_form_lateral(case, k, s, s.rates[hypothesis])
            for i in range(2):
                deviation = abs(evaluated[i] - expected[i])
                allowed = max(SELF_CHECK_REL * abs(expected[i]), SELF_CHECK_ABS_DEG)
                worst_abs = max(worst_abs, deviation)
                worst_ratio = max(worst_ratio, deviation / allowed)
                count += 1
    out.line(
        f"law self-check, case {case.name}, SIM {member}: {count} values, largest |deviation| "
        f"{worst_abs!r} deg, largest deviation / tolerance {worst_ratio!r}"
    )
    if worst_ratio > 1.0:
        fail(f"law self-check: case {case.name} SIM {member}")


def phase2_member(context: Context, case: CaseSpec, member: int) -> MemberCase:
    data = context.case_file(case.name, member)
    law = context.laws[case.name]
    g = context.geometry
    ks = [k for k in range(case.last_k + 1) if usable(case, k)]
    needed = sorted({j for k in ks for j in (k - 1, k, k + 1)})
    backed: dict[int, LateralBackout] = {}
    law_values: dict[tuple[str, int], Vector] = {}
    for k in needed:
        s = sample_at(data, k)
        backed[k] = backout_lateral(context.aero, s, g, s.rates["E"])
        for hypothesis in ("I", "E"):
            law_values[(hypothesis, k)] = law_lateral(law, case, k, s, s.rates[hypothesis])

    def backed_surfaces(k: int, perturb: tuple[str, float]) -> Vector:
        s = sample_at(data, k, perturb)
        result = backout_lateral(context.aero, s, g, s.rates["E"])
        return (result.ail, result.rdr, 0.0)

    def law_surfaces(k: int, hypothesis: str, perturb: tuple[str, float]) -> Vector:
        s = sample_at(data, k, perturb)
        return law_lateral(law, case, k, s, s.rates[hypothesis])

    rows = []
    for k in ks:
        delta = subtract(law_values[("I", k)], law_values[("E", k)])
        rates_of_change = []
        for i in range(2):
            candidates = [
                (backed_surface(backed[k + 1], i) - backed_surface(backed[k - 1], i))
                / (2.0 * SAMPLE_S)
            ]
            for hypothesis in ("I", "E"):
                candidates.append(
                    (law_values[(hypothesis, k + 1)][i] - law_values[(hypothesis, k - 1)][i])
                    / (2.0 * SAMPLE_S)
                )
            rates_of_change.append(max(abs(c) for c in candidates))
        floor_t = (HOLD_S * rates_of_change[0], HOLD_S * rates_of_change[1])
        backout_sum = [0.0, 0.0]
        for name in BACKOUT_COLUMNS:
            u = data.uncertainty(k, name)
            if u == 0.0:
                continue
            up, down = backed_surfaces(k, (name, u)), backed_surfaces(k, (name, -u))
            for i in range(2):
                backout_sum[i] += abs(up[i] - down[i]) / 2.0
        law_sum = [0.0, 0.0]
        for hypothesis in ("I", "E"):
            per_hypothesis = [0.0, 0.0]
            for name in LAW_COLUMNS[case.name]:
                u = data.uncertainty(k, name)
                if u == 0.0:
                    continue
                up = law_surfaces(k, hypothesis, (name, u))
                down = law_surfaces(k, hypothesis, (name, -u))
                for i in range(2):
                    per_hypothesis[i] += abs(up[i] - down[i]) / 2.0
            for i in range(2):
                law_sum[i] = max(law_sum[i], per_hypothesis[i])
        floor_p = (backout_sum[0] + law_sum[0], backout_sum[1] + law_sum[1])
        floor_b = backed[k].spread
        totals = [floor_b[i] + floor_t[i] + floor_p[i] for i in range(2)]
        counts = (
            delta[0] != 0.0 and abs(delta[0]) >= RATIO_MIN * totals[0],
            delta[1] != 0.0 and abs(delta[1]) >= RATIO_MIN * totals[1],
        )
        rows.append(
            Row(
                k=k,
                backed=(backed[k].ail, backed[k].rdr, 0.0),
                residual=backed[k].residual,
                delta=delta,
                floor_b=floor_b,
                floor_t=floor_t,
                floor_p=floor_p,
                counts=counts,
            )
        )
    return MemberCase(member, case, rows, law_values, backed)


def backed_surface(result: LateralBackout, i: int) -> float:
    return result.ail if i == 0 else result.rdr


def ratio(delta: float, floor: float) -> float:
    return math.inf if floor == 0.0 else abs(delta) / floor


def print_phase2(out: Output, result: MemberCase) -> None:
    out.line(f"== Phase 2: case {result.case.name}, SIM {result.member}")
    out.line(
        "k t ail_backed rdr_backed res_cy res_cl res_cn delta_ail delta_rdr "
        "Fb_ail Ft_ail Fp_ail ratio_ail counts_ail Fb_rdr Ft_rdr Fp_rdr ratio_rdr counts_rdr"
    )
    for row in result.rows:
        fields = [str(row.k), repr(row.k / 10.0), repr(row.backed[0]), repr(row.backed[1])]
        fields += [repr(x) for x in row.residual]
        fields += [repr(row.delta[0]), repr(row.delta[1])]
        for i in range(2):
            total = row.floor_b[i] + row.floor_t[i] + row.floor_p[i]
            fields += [repr(row.floor_b[i]), repr(row.floor_t[i]), repr(row.floor_p[i])]
            fields += [repr(ratio(row.delta[i], total)), "yes" if row.counts[i] else "no"]
        out.line(" ".join(fields))
    met = result.counting >= MIN_COUNTING
    out.line(
        f"case {result.case.name}, SIM {result.member}: {len(result.rows)} samples, "
        f"{result.counting} counting (sample, surface) pairs; precondition "
        f"(>= {MIN_COUNTING}) {'met' if met else 'not met'}"
    )


def phase2(context: Context, out: Output) -> dict[int, MemberCase | None]:
    deciding: dict[int, MemberCase | None] = {}
    for member in MEMBERS:
        describe_file(out, context.case_file("13.3", member))
        law_self_check(context, CASE_13P3, member, out)
    for member in MEMBERS:
        result = phase2_member(context, CASE_13P3, member)
        print_phase2(out, result)
        deciding[member] = result if result.counting >= MIN_COUNTING else None
    for member in MEMBERS:
        if deciding[member] is not None:
            continue
        out.line(f"SIM {member} is short in 13.3: case 15 under the same rule")
        describe_file(out, context.case_file("15", member))
        law_self_check(context, CASE_15, member, out)
        result = phase2_member(context, CASE_15, member)
        print_phase2(out, result)
        deciding[member] = result if result.counting >= MIN_COUNTING else None
    for member in MEMBERS:
        chosen = deciding[member]
        where = f"case {chosen.case.name}" if chosen is not None else "none"
        out.line(f"SIM {member}: deciding case {where}")
    return deciding


# --- Phase 3 -----------------------------------------------------------------------------------


def segment_of(case: CaseSpec, k: int) -> str:
    for name, low, high in case.segments:
        if low <= k <= high:
            return name
    raise ValueError(f"sample {k} is in no segment")


def statistics(out: Output, label: str, case: CaseSpec, values: list[tuple[int, float]]) -> None:
    for name, _, _ in case.segments:
        chosen = [e for k, e in values if segment_of(case, k) == name]
        if chosen:
            mean = sum(chosen) / len(chosen)
            largest = max(abs(e) for e in chosen)
            out.line(
                f"  {label} [{name}]: count {len(chosen)}, mean {mean!r}, largest |e| {largest!r}"
            )
        else:
            out.line(f"  {label} [{name}]: count 0")


def phase3_member(context: Context, result: MemberCase, out: Output) -> str:
    case, member = result.case, result.member
    data = context.case_file(case.name, member)
    law = context.laws[case.name]
    g = context.geometry
    out.line(f"== Phase 3: SIM {member}, deciding case {case.name}")
    out.line("k t surface delta e_I e_E supports")
    errors: dict[tuple[str, str], list[tuple[int, float]]] = {}
    supports_all = {"I": True, "E": True}
    for row in result.rows:
        for i, surface in enumerate(("ail", "rdr")):
            if not row.counts[i]:
                continue
            e = {h: row.backed[i] - result.law[(h, row.k)][i] for h in ("I", "E")}
            supported = [h for h in ("I", "E") if abs(e[h]) <= SUPPORT_FRACTION * abs(row.delta[i])]
            for h in ("I", "E"):
                supports_all[h] = supports_all[h] and h in supported
                errors.setdefault((surface, h), []).append((row.k, e[h]))
            out.line(
                f"{row.k} {row.k / 10.0!r} {surface} {row.delta[i]!r} {e['I']!r} {e['E']!r} "
                f"{','.join('H_' + h for h in supported) or 'none'}"
            )
    verdict = "H_I" if supports_all["I"] else "H_E" if supports_all["E"] else "neither"
    out.line(f"SIM {member} verdict: {verdict}")
    out.line("Segment statistics over counting samples (amendment 4):")
    for (surface, h), values in errors.items():
        statistics(out, f"{surface} e(H_{h})", case, values)

    counting = [(row, i) for row in result.rows for i in range(2) if row.counts[i]]
    out.line("Reported, never selected: e under H_N, the rate relative to n")
    variant: dict[str, list[tuple[int, float]]] = {}
    for row, i in counting:
        s = sample_at(data, row.k)
        law_n = law_lateral(law, case, row.k, s, s.rates["N"])
        variant.setdefault(("ail", "rdr")[i], []).append((row.k, row.backed[i] - law_n[i]))
    for surface, values in variant.items():
        statistics(out, f"{surface} e(H_N)", case, values)

    out.line(
        "Reported, never selected: the back-out with inertial aero rates (D3's rejected reading)"
    )
    alternative: dict[tuple[str, str], list[tuple[int, float]]] = {}
    for row, i in counting:
        s = sample_at(data, row.k)
        backed = backout_lateral(context.aero, s, g, s.rates["I"])
        for h in ("I", "E"):
            key = (("ail", "rdr")[i], h)
            alternative.setdefault(key, []).append(
                (row.k, backed_surface(backed, i) - result.law[(h, row.k)][i])
            )
    for (surface, h), values in alternative.items():
        statistics(out, f"{surface} e_inertial_aero(H_{h})", case, values)

    report_el(context, result, out)
    return verdict


def keas(s: Sample) -> float:
    rho0 = RHO0_KG_M3 * SLUG_PER_KG * M_PER_FT**3  # slug/ft3
    return s.vt * M_PER_FT * math.sqrt(s.density / rho0) / M_PER_S_PER_KNOT


def report_el(context: Context, result: MemberCase, out: Output) -> None:
    case, member = result.case, result.member
    data = context.case_file(case.name, member)
    law = context.laws[case.name]
    g = context.geometry
    first = sample_at(data, 0)
    parameters = {"altCmd": data.value(0, ALTITUDE), "keasCmd": keas(first)}
    if member == 5:  # Q11: only SIM 5 replaced the LQR references with its trim values
        parameters.update(
            {"trimmedAlpha": first.alpha, "trimmedTheta": first.theta, "trimmedKEAS": keas(first)}
        )
    out.line("Reported, never selected: el, with every parameter estimated from the data")
    out.line(f"  estimated at t = 0: {', '.join(f'{k} {v!r}' for k, v in parameters.items())}")

    def law_el(k: int, s: Sample, hypothesis: str, stick_trim: float) -> float:
        inputs = lateral_law_inputs(case, k, s, s.rates[hypothesis])
        inputs.update(parameters)
        inputs.update(
            {
                "longStk": 0.0,
                "throttle": 0.0,
                "altMsl": data.value(k, ALTITUDE),
                "Vequiv": keas(s),
                "alpha": s.alpha,
                "theta": s.theta,
                "qb": s.rates[hypothesis][1],
                "longStkTrim": stick_trim,
            }
        )
        return law.evaluate(inputs, ("el",))["el"]

    el_first, _ = backout_el(context.aero, first, g, first.rates["E"])
    for hypothesis in ("I", "E"):
        stick_trim = (law_el(0, first, hypothesis, 0.0) - el_first) / 25.0
        out.line(f"  longStkTrim fitted at t = 0 under H_{hypothesis}: {stick_trim!r}")
        values = []
        for row in result.rows:
            s = sample_at(data, row.k)
            el_backed, _ = backout_el(context.aero, s, g, s.rates["E"])
            values.append((row.k, el_backed - law_el(row.k, s, hypothesis, stick_trim)))
        statistics(out, f"el e(H_{hypothesis}) over every window sample", case, values)


def main() -> None:
    parser = argparse.ArgumentParser(description="The NESC F-16 laws' rate frame (M7 PR 1b).")
    parser.add_argument("--refs", type=Path, required=True, help="the main checkout's docs/refs")
    parser.add_argument("--phase", choices=("floor", "compare"), required=True)
    args = parser.parse_args()
    floor = args.phase == "floor"
    out = Output(floor)
    print(f"M7 PR 1b rate-frame analysis, phase {args.phase}; Python {sys.version.split()[0]}")
    context = phase1(args.refs, out)
    deciding = phase2(context, out)
    if floor:
        return
    print("Phases 1 and 2 reran; every self-check passed. Files read:")
    for data in context.files.values():
        print(f"  {data.relative}  sha256 {data.sha256}")
    for member in MEMBERS:
        chosen = deciding[member]
        print(f"SIM {member}: deciding case {chosen.case.name if chosen else 'none'}")
    full = Output(True)
    verdicts: dict[int, str] = {}
    for member in MEMBERS:
        chosen = deciding[member]
        if chosen is None:
            verdicts[member] = "cannot discriminate (no deciding case; no comparison computed)"
            continue
        verdicts[member] = phase3_member(context, chosen, full)
    if any(deciding[member] is None for member in MEMBERS):
        label = "cannot discriminate"
    elif verdicts[4] == verdicts[5] and verdicts[4] != "neither":
        label = f"discriminates: {verdicts[4]}"
    else:
        label = "inconsistent"
    parts = []
    for member in MEMBERS:
        chosen = deciding[member]
        where = chosen.case.name if chosen is not None else "none"
        parts.append(f"SIM {member} {verdicts[member]} (case {where})")
    print(f"RESULT: {label}; {', '.join(parts)}")


if __name__ == "__main__":
    main()
