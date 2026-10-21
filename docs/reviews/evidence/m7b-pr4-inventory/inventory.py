"""M7b PR 4's asymmetry inventory (docs/plans/m7b-pr4-f16-symmetry.md, sections 6, 8 and 10; R10).

Independence. This script imports nothing from astra and links no plugin. It reads:
- aircraft/f16/physical/aero/aero_tables.hpp's text, the generated header: the source of the four
  tables' figures, as the M7b plan's finding 2 measured them;
- docs/reviews/evidence/m7b-pr4-trim/trim_n_frame.out, the oracle's one trim result (section 5);
- M7 PR 11a's derive_residual.py, for its own ISA-1976 and its foot and slug; it imports nothing
  from astra.
Every other constant is F16_aero.dml's, F16_inertia.dml's or F16_control.dml's, written here with
the line of ASTRA's realization that carries it.

It prints, in order:
1. finding 2's figures for dclda, dcldr, dcnda and dcndr, and the odd slopes at the trim's alpha
   (section 6);
2. the Omega term at the trim and a plain replay of the law's lateral rows at the frame-0
   request: the Omega-only control's precondition (section 10);
3. the doubled control's four registered numbers at state 1 (section 10, amendment 9);
4. the shipped pairs' static estimate, which section 8's report is read against (amendment 4).

It exits 1 when a registered figure moves (section 6's stop rule), or when one of the doubled
control's four numbers is below 2^10 (section 10).

Run from the repository root:
    .venv/bin/python -I -B docs/reviews/evidence/m7b-pr4-inventory/inventory.py
"""

from __future__ import annotations

import math
import re
import sys
from pathlib import Path

import yaml

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[3]
sys.path.insert(0, str(ROOT / "docs/reviews/evidence/m7-pr11a-residual"))

import derive_residual as dr  # noqa: E402  (M7 PR 11a's evidence: imports nothing from astra)

HEADER = ROOT / "aircraft/f16/physical/aero/aero_tables.hpp"
TRIM = ROOT / "docs/reviews/evidence/m7b-pr4-trim/trim_n_frame.out"

DEG = math.pi / 180.0
H = 1e-3  # s, sim.dt
LAT = 0.6286530522161018  # rad, case 11's latitude (section 5's base)

# F16_aero.dml, as aero_math.hpp carries it.
S_REF = 300.0 * dr.FT * dr.FT  # :51, 300 ft^2
B_SPAN = 30.0 * dr.FT  # :49, 30 ft
CM_FWD_OF_MRC = 0.01 * 11.32 * (35.0 - 25.0) * dr.FT  # :82-83, 1.132 ft
AILERON_SCALE = 20.0 * DEG  # :64
RUDDER_SCALE = 30.0 * DEG  # :65
CY0_DAIL = 0.021  # :69
CY0_DRDR = 0.086  # :70

# F16_inertia.dml (f16_flight.hpp:509-513 carries the same values). The tensor's off-diagonal
# is -Ixz (mass.cpp:56, the S&L product-of-inertia convention).
MASS = 637.1595 * dr.SLUG
IXX, IYY, IZZ, IXZ = (v * dr.SLUG * dr.FT * dr.FT for v in (9496.0, 55814.0, 63100.0, 982.0))

# F16_control.dml, as control_law.hpp carries it (:101-110).
PER_DEG = 1.0 / DEG
K_LAT = (
    3.078043941515770 * PER_DEG,
    0.032365863044163 * PER_DEG,
    4.557858908828332,
    0.589443156647647,
)
K_DIR = (
    -0.705817452754520 * PER_DEG,
    -0.256362860634868 * PER_DEG,
    -1.073666149713151,
    0.822114635953878,
)
AILERON_PER_STICK = -21.5 * DEG  # :108
RUDDER_PER_PEDAL = -30.0 * DEG  # :109
AILERON_TO_RUDDER = 0.008  # :110
OMEGA_IE = 7.292115e-5  # geodesy::kOmegaIe

# Section 6's registered figures (the M7b plan's finding 2 and its inventory's Omega row).
FINDING2_MAX = {"Dclda": 0.079, "Dcldr": 0.031, "Dcnda": 0.035, "Dcndr": 0.044}
FINDING2_NEAR = {  # |T(10) - T(-10)| at alpha 0 and 5 deg, and T(0) at alpha 0
    "Dclda": ((0.002, 0.004), -0.051),
    "Dcldr": ((0.001, 0.002), 0.015),
    "Dcnda": ((0.006, 0.008), -0.010),
    "Dcndr": ((0.001, 0.003), -0.045),
}
DOUBLED_MIN_RATIO = 2.0**10  # section 10


def matrix(src: str, name: str) -> list[list[float]]:
    """The rows of the generated header's array `name` (one row for a 1-D table)."""
    start = src.index(f" {name}{{")
    body = src[src.index("{", start) : src.index(";", start)]
    rows = re.findall(r"\{([^{}]*)\}", body)
    return [[float(v) for v in row.split(",") if v.strip()] for row in rows]


def vector(src: str, name: str) -> list[float]:
    (row,) = matrix(src, name)
    return row


def bracket(axis: list[float], x: float) -> tuple[int, float]:
    i = max(k for k in range(len(axis) - 1) if axis[k] <= x)
    return i, (x - axis[i]) / (axis[i + 1] - axis[i])


def main() -> int:
    src = HEADER.read_text(encoding="utf-8")
    alpha_bp = vector(src, "kAlpha1")
    beta2 = vector(src, "kBeta2")
    beta1 = vector(src, "kBeta1")
    assert beta2 == [-30.0, -20.0, -10.0, 0.0, 10.0, 20.0, 30.0]
    assert beta1[:2] == [0.0, 5.0]
    trim = yaml.safe_load(TRIM.read_text(encoding="utf-8"))
    alpha = float(trim["alpha"])
    v_b = [float(v) for v in trim["initial"]["state"]["v_b"]]
    altitude = float(trim["altitude"])
    tas = math.sqrt(sum(v * v for v in v_b))
    rho, _ = dr.isa(altitude)
    qbar = 0.5 * rho * tas * tas
    i_a, f_a = bracket(alpha_bp, alpha / DEG)
    moved: list[str] = []

    print("M7b PR 4: the asymmetry inventory")
    print("  (docs/plans/m7b-pr4-f16-symmetry.md, sections 6, 8 and 10)")
    print(f"  header {HEADER.relative_to(ROOT)}; trim {TRIM.relative_to(ROOT)}")
    print(f"  trim alpha {alpha!r} rad ({alpha / DEG:.6f} deg), |v_b| {tas!r} m/s")
    print(f"  altitude {altitude!r} m; rho {rho:.9g} kg/m^3 (derive_residual.isa)")
    print(f"  qbar {qbar:.9g} Pa")

    print("\n1. The four signed-beta tables (finding 2) and their odd slopes")
    slope: dict[str, float] = {}
    center: dict[str, float] = {}
    for name in ("Dclda", "Dcldr", "Dcnda", "Dcndr"):
        t = matrix(src, "k" + name)
        worst = max(
            (abs(t[6 - i][j] - t[i][j]), beta2[6 - i], alpha_bp[j])
            for i in range(3)
            for j in range(12)
        )
        near = (abs(t[4][2] - t[2][2]), abs(t[4][3] - t[2][3]))
        t0 = t[3][2]
        diff = (1 - f_a) * (t[4][i_a] - t[2][i_a]) + f_a * (t[4][i_a + 1] - t[2][i_a + 1])
        slope[name] = diff / (2.0 * 10.0 * DEG)
        center[name] = (1 - f_a) * t[3][i_a] + f_a * t[3][i_a + 1]
        print(
            f"  {name}: max |T(-b) - T(b)| {worst[0]:.3f} at beta +-{worst[1]:g} deg,"
            f" alpha {worst[2]:g} deg"
        )
        print(
            f"    |T(10) - T(-10)| at alpha 0 and 5 deg: {near[0]:.3f}, {near[1]:.3f};"
            f" T(0) at alpha 0: {t0:.3f}"
        )
        print(
            f"    at the trim's alpha: T(0) {center[name]:+.6f};"
            f" odd slope (T(10) - T(-10))/(2*10 deg) {slope[name]:+.6f} per rad"
        )
        if round(worst[0], 3) != FINDING2_MAX[name]:
            moved.append(f"{name} max {worst[0]:.3f} against {FINDING2_MAX[name]}")
        (lo, hi), c0 = FINDING2_NEAR[name]
        if (round(min(near), 3), round(max(near), 3)) != (lo, hi) or round(t0, 3) != c0:
            moved.append(f"{name} near-condition figures {near}, {t0} against {(lo, hi)}, {c0}")

    print("\n2. The Omega term at the trim, and the law's frame-0 lateral replies")
    c_nb = dr.c_bn(0.0, alpha, 0.0)
    omega_n = [OMEGA_IE * math.cos(LAT), 0.0, -OMEGA_IE * math.sin(LAT)]
    b = [sum(c_nb[i][k] * omega_n[k] for k in range(3)) for i in range(3)]
    p_law, r_law = -b[0], -b[2]
    # Frame 0: phi = beta = psi = 0 and the course 0, so delta_phi = 0 (control_law.hpp:241-255).
    lat_lqr = -(K_LAT[2] * p_law + K_LAT[3] * r_law)
    dir_lqr = -(K_DIR[2] * p_law + K_DIR[3] * r_law)
    total_lat = min(max(lat_lqr, -1.0), 1.0)
    total_pedal = min(max(dir_lqr, -1.0), 1.0)
    aileron_lwd = AILERON_PER_STICK * total_lat
    rudder = RUDDER_PER_PEDAL * total_pedal + AILERON_TO_RUDDER * aileron_lwd
    aileron_icd = -aileron_lwd
    precondition = aileron_icd != 0.0 and rudder != 0.0
    print(f"  b = C_nb Omega_n = ({b[0]:+.4e}, {b[1]:+.4e}, {b[2]:+.4e}) rad/s")
    print(f"  the law's frame-0 p and r: {p_law:+.4e}, {r_law:+.4e} rad/s")
    print(f"  frame-0 reply: aileron {aileron_icd / DEG:+.6f} deg (ICD +RWD),")
    print(f"  rudder {rudder / DEG:+.6f} deg")
    print(f"  the Omega-only control's precondition (both nonzero): {precondition}")
    if float(f"{p_law:.0e}") != -6e-5 or float(f"{r_law:.0e}") != 4e-5:
        moved.append(f"the law's frame-0 p, r {p_law:.3e}, {r_law:.3e} against -6e-5, +4e-5")
    if not 0.005 <= aileron_icd / DEG <= 0.007 or round(rudder / DEG, 3) != 0.003:
        moved.append(f"frame-0 aileron, rudder {aileron_icd / DEG:.5f}, {rudder / DEG:.5f} deg")
    if not precondition:
        moved.append("the Omega-only control's precondition: a zero frame-0 aileron or rudder")

    print("\n3. The doubled control's four registered numbers at state 1 (section 10)")
    dail = aileron_lwd / AILERON_SCALE  # aero.cpp:71 flips ICD +RWD to the DML's +LWD
    drdr = rudder / RUDDER_SCALE
    cy0 = CY0_DAIL * dail + CY0_DRDR * drdr  # aero_math.hpp:208 at beta = 0, p = r = 0
    fy = qbar * S_REF * cy0
    v1 = H * fy / MASS  # stage 1's v_dot = Fy/m at frame 0: g_b,y = 0 and (omega x v)_y = 0
    beta1 = math.atan2(v1, math.hypot(v_b[0], v_b[2]))
    odd = {name: slope[name] * beta1 for name in slope}
    dcl1 = odd["Dclda"] * dail + odd["Dcldr"] * drdr
    dcn1 = odd["Dcnda"] * dail + odd["Dcndr"] * drdr
    # clt and the damping terms are near 0 at state 1.
    cl = center["Dclda"] * dail + center["Dcldr"] * drdr
    cn = center["Dcnda"] * dail + center["Dcndr"] * drdr
    qsb = qbar * S_REF * B_SPAN
    det = IXX * IZZ - IXZ * IXZ
    inv = ((IZZ / det, IXZ / det), (IXZ / det, IXX / det))  # I^-1's roll-yaw block, tensor -Ixz
    roll, yaw = qsb * cl, qsb * cn - CM_FWD_OF_MRC * fy
    p_dot = inv[0][0] * roll + inv[0][1] * yaw
    r_dot = inv[1][0] * roll + inv[1][1] * yaw
    dp_dot = inv[0][0] * qsb * dcl1 + inv[0][1] * qsb * dcn1
    dr_dot = inv[1][0] * qsb * dcl1 + inv[1][1] * qsb * dcn1
    print(f"  frame 0's dail {dail:+.4e}, drdr {drdr:+.4e}; cy0 {cy0:+.4e}; Fy {fy:+.4e} N")
    print(f"  beta(1) ~ h Fy/(m V) = {beta1:+.4e} rad")
    for name, value in odd.items():
        tab = abs(center[name])
        print(
            f"  intermediate: {name} odd part at beta(1) {value:+.4e}"
            f" against ulp(T) {math.ulp(tab):.3e} ({abs(value) / math.ulp(tab):.3g})"
        )
    numbers = [
        ("extra cl1", dcl1, cl),
        ("extra cn1", dcn1, cn),
        ("delta p_dot", dp_dot, p_dot),
        ("delta r_dot", dr_dot, r_dot),
    ]
    for label, delta, value in numbers:
        ratio = abs(delta) / math.ulp(value)
        print(
            f"  {label} {delta:+.4e} against ulp({value:+.4e}) = {math.ulp(value):.3e}: {ratio:.3g}"
        )
        if not ratio >= DOUBLED_MIN_RATIO:
            moved.append(f"the doubled control's {label} ratio {ratio:.3g} is below 2^10")

    print("\n4. The shipped pairs' static estimate (section 8's report; amendment 4)")
    beta = 1.5 * DEG
    dcl = 2.0 * slope["Dclda"] * beta  # |dail| = 1
    dcn = 2.0 * slope["Dcnda"] * beta
    clp_table = vector(src, "kClp")
    clp = (1 - f_a) * clp_table[i_a] + f_a * clp_table[i_a + 1]
    abs_cn0 = matrix(src, "kAbsCn0")
    cn_beta = ((1 - f_a) * abs_cn0[1][i_a] + f_a * abs_cn0[1][i_a + 1]) / (5.0 * DEG)
    b2v = B_SPAN / (2.0 * tas)
    dp = abs(dcl) / (abs(clp) * b2v)
    dbeta = abs(dcn) / cn_beta
    stick = K_LAT[0] * dp * 0.1 + K_LAT[2] * dp
    print(f"  at |beta| = 1.5 deg and |dail| = 1: |dCl| {abs(dcl):.3e}, |dCn| {abs(dcn):.3e}")
    print(f"  p: |dCl| / (|Clp| b/2V) = {dp:.3e} rad/s (Clp {clp:.4f}, b/2V {b2v:.5f})")
    print(f"  phi: {dp * 0.1:.2e} to {dp * 0.3:.2e} rad over 0.1 to 0.3 s of the roll-in")
    print(f"  beta: |dCn| / Cn_beta = {dbeta:.3e} rad (Cn_beta {cn_beta:.4f} per rad),")
    print("  about half that over a partial dutch roll")
    print(f"  the lateral stick: {K_LAT[0]:.2f} dphi + {K_LAT[2]:.3f} dp, about {stick:.2f} units")
    print("  at 0.1 s")
    print(f"  pre-step (Omega): reply aileron asymmetry {2.0 * aileron_icd / DEG:+.5f} deg,")
    print(f"  rudder {2.0 * rudder / DEG:+.5f} deg")

    if moved:
        print("\nSTOP (section 6): " + "; ".join(moved))
        return 1
    print("\nno registered figure moved")
    return 0


if __name__ == "__main__":
    sys.exit(main())
