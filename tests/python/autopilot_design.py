"""Design model for the example autopilot (M4 PR 5, docs/plans/m4-pr5-autopilot.md D7, M4 plan Q13).

Not a test: the helper `test_autopilot_design.py` asserts on. It builds the linear models the
autopilot's control law is designed against, closes the loops with the gains that SHIP (read from
embedded/autopilot/gains.hpp, so the analyzed design is the C++ design), and provides the analyses:
closed-loop poles, loop-at-a-time margins at each actuator input, and the exchange-rate sample-and-
hold closed loop.

Plants (cruise.yaml's trim, 230 m/s at 10 km, via astra.linearize, unchanged):
  - Longitudinal x = [u, w, q, theta, h]: PR 7's longitudinal block of A plus the altitude state,
    PR 8's construction (a central-difference d f / d h column through linearize._evaluate, the
    kinematic h_dot = u sin(theta) - w cos(theta) row; S&L Example 3.8-5), with the elevator and
    throttle columns of B and a zero h_dot row. PR 7's own A holds position fixed (D11) and has no
    altitude state, which is why this is built here and `astra.linearize` is untouched (M4 plan
    Q13, Joe's decision (a)).
  - Lateral x = [v, p, r, phi, psi]: the 9-state A restricted to those states (its psi row is r /
    cos(theta)), with the aileron and rudder columns of B. No altitude state is needed.

The control law linearized here is the one in gains.hpp's header comment and autopilot.cpp.
Saturation (the output limits and the bank clamp) and turbulence are NOT in this model; PR 6's
nonlinear runs are.
"""

from __future__ import annotations

import math
import re
from dataclasses import dataclass
from pathlib import Path

import numpy as np
import scipy.linalg
import yaml

from astra import _core, linearize

GAINS_HPP = Path("embedded/autopilot/gains.hpp")
LIMITS_HPP = Path("embedded/autopilot/limits.hpp")
GAIN_NAMES = (
    "kAltP",
    "kAltI",
    "kAltD",
    "kPitchTheta",
    "kPitchRate",
    "kSpeedP",
    "kSpeedI",
    "kHeadingP",
    "kHeadingI",
    "kBankP",
    "kRollRate",
    "kYawRate",
)
# Not shipped. If gains.hpp ever defines kSideslip (a beta term in the rudder law, delta_r +=
# kSideslip beta) the design model analyzes it, and the effective-Cn_beta check in the test decides
# whether its sign is acceptable; the C++ law must then implement the same term.
OPTIONAL_GAIN_NAMES = ("kSideslip",)
_GAIN_LINE = re.compile(
    r"^constexpr double (k\w+) = ([-+]?[0-9]*\.?[0-9]+(?:[eE][-+]?[0-9]+)?);\s*$"
)

# The exchange period at transport.divisor 10 and dt 1 ms (M4 plan; the autopilot itself takes its
# dt from
# frame-derived time and does not know this, the design does).
EXCHANGE_PERIOD_S = 0.01

_U, _W, _THETA = 0, 2, 7  # D11's x = [u, v, w, p, q, r, phi, theta, psi]
_V, _P, _R, _PHI, _PSI = 1, 3, 5, 6, 8
_ALTITUDE_STEP_M = 1.0  # test_cruise_phugoid.py's central-difference step (plan finding 5 there)


def read_gains(path: Path = GAINS_HPP) -> dict[str, float]:
    """The gains in gains.hpp, strictly: one `constexpr double kName = value;` line per gain,
    exactly the
    names in GAIN_NAMES, each once, and no other `constexpr` line."""
    gains: dict[str, float] = {}
    for line in path.read_text(encoding="utf-8").splitlines():
        if not line.startswith("constexpr"):
            continue
        match = _GAIN_LINE.match(line)
        if match is None:
            raise ValueError(
                f"{path}: not a plain `constexpr double kName = value;` gain: {line!r}"
            )
        name = match.group(1)
        if name in gains:
            raise ValueError(f"{path}: {name} defined twice")
        gains[name] = float(match.group(2))
    allowed = set(GAIN_NAMES) | set(OPTIONAL_GAIN_NAMES)
    if not set(GAIN_NAMES) <= set(gains) or not set(gains) <= allowed:
        raise ValueError(
            f"{path}: gain names differ: missing {sorted(set(GAIN_NAMES) - set(gains))}, "
            f"extra {sorted(set(gains) - allowed)}"
        )
    return gains


_RANGE_LINE = re.compile(r"^inline constexpr Range (k\w+)\{([-+]?[0-9.]+), ([-+]?[0-9.]+)\};\s*$")


def read_limits(path: Path = LIMITS_HPP) -> dict[str, tuple[float, float]]:
    """The `inline constexpr Range kName{lo, hi};` lines of limits.hpp (output and bank limits)."""
    limits: dict[str, tuple[float, float]] = {}
    for line in path.read_text(encoding="utf-8").splitlines():
        match = _RANGE_LINE.match(line)
        if match is not None:
            limits[match.group(1)] = (float(match.group(2)), float(match.group(3)))
    return limits


@dataclass(frozen=True)
class Plants:
    a_lon: np.ndarray  # 5x5, [u, w, q, theta, h]
    b_lon: np.ndarray  # 5x2, [elevator, throttle]
    a_lat: np.ndarray  # 5x5, [v, p, r, phi, psi]
    b_lat: np.ndarray  # 5x2, [aileron, rudder]
    true_airspeed_mps: float
    u0_mps: float
    w0_mps: float


def build_plants(config_path: str, true_airspeed_mps: float, altitude_m: float) -> Plants:
    result = linearize.linearize(config_path, true_airspeed_mps, altitude_m)
    lon = list(linearize._LONGITUDINAL_INDICES)  # pyright: ignore[reportPrivateUsage]
    x0 = result.x0

    # d f / d h about the LinearizeResult's own trim, as test_cruise_phugoid._altitude_column does.
    sim = result.simulation
    base = np.array(result.trim.state, dtype=float)
    altitude_index = _core.POSITION + _core.POSITION_SIZE - 1
    plus, minus = base.copy(), base.copy()
    plus[altitude_index] += _ALTITUDE_STEP_M
    minus[altitude_index] -= _ALTITUDE_STEP_M
    evaluate = linearize._evaluate  # pyright: ignore[reportPrivateUsage]
    f_plus = evaluate(sim, plus, result.x0, result.u0)
    f_minus = evaluate(sim, minus, result.x0, result.u0)
    for name, value in zip(sim.input_names(), result.u0, strict=True):
        sim.write_input(name, value)
    d_dh = (f_plus - f_minus) / (2.0 * _ALTITUDE_STEP_M)

    u, w, theta = float(x0[_U]), float(x0[_W]), float(x0[_THETA])
    a_lon = np.zeros((5, 5))
    a_lon[:4, :4] = result.a[np.ix_(lon, lon)]
    a_lon[:4, 4] = d_dh[lon]
    a_lon[4, :4] = [
        math.sin(theta),
        -math.cos(theta),
        0.0,
        u * math.cos(theta) + w * math.sin(theta),
    ]
    names = list(result.input_names)
    elevator, aileron = names.index("controls.elevator"), names.index("controls.aileron")
    rudder, throttle = names.index("controls.rudder"), names.index("controls.throttle")
    b_lon = np.zeros((5, 2))
    b_lon[:4, 0] = result.b[lon, elevator]
    b_lon[:4, 1] = result.b[lon, throttle]

    lat = [_V, _P, _R, _PHI, _PSI]
    a_lat = result.a[np.ix_(lat, lat)]
    b_lat = result.b[np.ix_(lat, [aileron, rudder])]
    return Plants(a_lon, b_lon, a_lat, b_lat, math.hypot(u, w), u, w)


@dataclass(frozen=True)
class Loop:
    """A closed-loop system: plant states then controller integrator states. z' = az z + bz u, u =
    cc z.
    `n_plant` marks where the integrator states start; `integrator_rows` is d(integrator)/dt as
    rows of
    az[n_plant:, :n_plant] (integrators are driven by plant states only)."""

    az: np.ndarray
    bz: np.ndarray
    cc: np.ndarray
    n_plant: int
    channels: tuple[str, ...]

    def closed(self) -> np.ndarray:
        return self.az + self.bz @ self.cc


def longitudinal_loop(plants: Plants, gains: dict[str, float]) -> Loop:
    """Elevator: delta_e = -(kAltP e_h + kAltI int e_h) + kAltD h_dot + kPitchTheta theta +
    kPitchRate q,
    e_h = -h (regulator form). Throttle: delta_t = kSpeedP e_v + kSpeedI int e_v, e_v = -V (tas)."""
    a5 = plants.a_lon
    tas_row = np.array([plants.u0_mps, plants.w0_mps, 0.0, 0.0, 0.0]) / plants.true_airspeed_mps
    h_dot_row = a5[4]  # vertical_speed is the measured rate of altitude
    az = np.zeros((7, 7))
    az[:5, :5] = a5
    az[5, 4] = -1.0  # int e_h' = e_h = -h
    az[6, :5] = -tas_row  # int e_v' = e_v = -V
    bz = np.zeros((7, 2))
    bz[:5, :] = plants.b_lon
    cc = np.zeros((2, 7))
    cc[0, 4] = gains["kAltP"]  # -kAltP * e_h = +kAltP * h
    cc[0, 5] = -gains["kAltI"]
    cc[0, :5] += gains["kAltD"] * h_dot_row
    cc[0, 3] += gains["kPitchTheta"]
    cc[0, 2] += gains["kPitchRate"]
    cc[1, :5] = -gains["kSpeedP"] * tas_row
    cc[1, 6] = gains["kSpeedI"]
    return Loop(az, bz, cc, 5, ("elevator", "throttle"))


def lateral_loop(plants: Plants, gains: dict[str, float]) -> Loop:
    """phi_c = kHeadingP e_psi + kHeadingI int e_psi (e_psi = -psi), aileron = kBankP (phi_c - phi)
    - kRollRate p; rudder = kYawRate r (a pure yaw-rate damper; no sideslip term, see gains.hpp)."""
    az = np.zeros((6, 6))
    az[:5, :5] = plants.a_lat
    az[5, 4] = -1.0  # int e_psi' = e_psi = -psi
    bz = np.zeros((6, 2))
    bz[:5, :] = plants.b_lat
    cc = np.zeros((2, 6))
    cc[0, 4] = -gains["kBankP"] * gains["kHeadingP"]
    cc[0, 5] = gains["kBankP"] * gains["kHeadingI"]
    cc[0, 3] = -gains["kBankP"]
    cc[0, 1] = -gains["kRollRate"]
    cc[1, 2] = gains["kYawRate"]
    cc[1, 0] = gains.get("kSideslip", 0.0) / plants.true_airspeed_mps  # beta = v / V
    return Loop(az, bz, cc, 5, ("aileron", "rudder"))


def min_damping_ratio(poles: np.ndarray) -> float:
    ratios = [-p.real / abs(p) for p in poles if abs(p.imag) > 1e-9 and abs(p) > 1e-12]
    return min(ratios) if ratios else 1.0


@dataclass(frozen=True)
class Margins:
    """Loop-at-a-time, broken-loop margins at one actuator input, the other channels closed. The
    return
    ratio is L(jw) = c_i (jw I - F_i)^-1 b_i, F_i the closed loop with channel i's feedback
    removed; the loop
    closes with u_i = +c_i, so the critical point is +1. `phase_margin_deg`: the smallest
    |angle(L)| at any
    |L| = 1 crossing. `gain_increase_db` / `gain_decrease_db`: the factor by which the loop gain
    can rise or
    fall before L reaches +1 at a 0-degree crossing (inf when there is none)."""

    phase_margin_deg: float
    gain_increase_db: float
    gain_decrease_db: float


def loop_margins(loop: Loop, omega: np.ndarray | None = None) -> dict[str, Margins]:
    w_grid = np.logspace(-3.0, 2.3, 4000) if omega is None else omega
    n = loop.az.shape[0]
    out: dict[str, Margins] = {}
    for i, name in enumerate(loop.channels):
        cc = loop.cc.copy()
        row = loop.cc[i].copy()
        cc[i] = 0.0
        f = loop.az + loop.bz @ cc
        b = loop.bz[:, i]
        resp = np.array([row @ np.linalg.solve(1j * w * np.eye(n) - f, b) for w in w_grid])
        mag, ang = np.abs(resp), np.angle(resp)
        phase_margin = 180.0
        gain_up = math.inf
        gain_down = math.inf
        for k in range(len(w_grid) - 1):
            if (mag[k] - 1.0) * (mag[k + 1] - 1.0) < 0.0:
                phase_margin = min(phase_margin, abs(math.degrees(float(ang[k]))))
            if (
                ang[k] * ang[k + 1] < 0.0
                and abs(ang[k] - ang[k + 1]) < math.pi
                and resp[k].real > 0.0
            ):
                if mag[k] < 1.0:
                    gain_up = min(gain_up, 1.0 / float(mag[k]))
                else:
                    gain_down = min(gain_down, float(mag[k]))
        out[name] = Margins(
            phase_margin,
            20.0 * math.log10(gain_up) if math.isfinite(gain_up) else math.inf,
            20.0 * math.log10(gain_down) if math.isfinite(gain_down) else math.inf,
        )
    return out


def sampled_closed_loop(
    loop: Loop, period_s: float = EXCHANGE_PERIOD_S, delay: bool = False
) -> np.ndarray:
    """The closed loop as the sim applies it: plant states x_k are sampled at frame k, the
    integrators take
    that frame's error (I_k = I_{k-1} + T e_k, the C++'s order), the command u_k = c(x_k, I_k) is
    applied in
    the SAME frame (docs/architecture.md section 5, ADR-0022: zero compute delay) and held for one
    period
    (zero-order hold). State s_k = [x_k, I_{k-1}]. `delay=True` adds one exchange of extra delay
    (u applied one period late): conservative, not the acceptance."""
    n, m = loop.az.shape[0], loop.bz.shape[1]
    npl = loop.n_plant
    k_int = n - npl
    ap_, bp_ = loop.az[:npl, :npl], loop.bz[:npl]
    block = np.zeros((npl + m, npl + m))
    block[:npl, :npl] = ap_ * period_s
    block[:npl, npl:] = bp_ * period_s
    expm = scipy.linalg.expm(block)
    ad, bd = expm[:npl, :npl], expm[:npl, npl:]
    e_int = loop.az[npl:, :npl]  # d(integrator)/dt = e_int x
    cx, ci = loop.cc[:, :npl], loop.cc[:, npl:]
    u_from_x = cx + ci @ (period_s * e_int)  # u_k = u_from_x x_k + ci I_{k-1}
    if not delay:
        f = np.zeros((n, n))
        f[:npl, :npl] = ad + bd @ u_from_x
        f[:npl, npl:] = bd @ ci
        f[npl:, :npl] = period_s * e_int
        f[npl:, npl:] = np.eye(k_int)
        return f
    f = np.zeros((n + m, n + m))
    f[:npl, :npl] = ad
    f[:npl, n:] = bd  # x_{k+1} = Ad x_k + Bd u_{k-1}
    f[npl:n, :npl] = period_s * e_int
    f[npl:n, npl:n] = np.eye(k_int)
    f[n:, :npl] = u_from_x  # u_{k} stored for the next period
    f[n:, npl:n] = ci
    return f


@dataclass(frozen=True)
class Phugoid:
    damped_period_s: float
    damping_ratio: float


def open_loop_phugoid(a_lon: np.ndarray) -> Phugoid:
    """The slower oscillatory pair of the open-loop 5x5 (the faster is the short period)."""
    eigenvalues = np.linalg.eigvals(a_lon)
    oscillatory = sorted((complex(e) for e in eigenvalues if e.imag > 1e-6), key=abs)
    if len(oscillatory) != 2:
        raise ValueError(f"unexpected 5x5 eigenvalues {eigenvalues!r}")
    phugoid = oscillatory[0]
    return Phugoid(2.0 * math.pi / phugoid.imag, -phugoid.real / abs(phugoid))


# --- Inner-loop separation and the yaw-damper sign (M4 PR 5 correction round 1) ---


def aero_yaw_coefficients(config_path: str) -> tuple[float, float]:
    """(cn_beta, cn_rudder) per rad from the aero model's own config, the coefficients of
    aero_math.hpp's yaw moment N = q S b (cn_beta beta + cn_rudder delta_r + ...). Positive rudder
    is trailing-edge-left (nose-left), so cn_rudder is negative; positive beta is wind from the
    right, so a stiffening rudder gives a POSITIVE yaw moment for positive beta."""
    data = yaml.safe_load(Path(config_path).read_text(encoding="utf-8"))
    for model in data["vehicle"]["models"]:
        if str(model.get("plugin", "")).endswith("physical/aero"):
            return float(model["cn_beta_per_rad"]), float(model["cn_rudder_per_rad"])
    raise ValueError(f"{config_path}: no physical/aero model in vehicle.models")


def effective_cn_beta(cn_beta: float, cn_rudder: float, k_sideslip: float = 0.0) -> float:
    """cn_beta seen through a rudder law delta_r = kYawRate r + k_sideslip beta: the
    beta-proportional part of the yaw moment is (cn_beta + cn_rudder k_sideslip) beta. The shipped
    law has NO sideslip term (k_sideslip = 0, so this is cn_beta); the default states that.
    cn_rudder is negative, so a positive k_sideslip (the defect found in round 1: 0.269 took 0.26
    down to 0.206) weakens the weathervane stability and only k_sideslip <= 0 stiffens it."""
    return cn_beta + cn_rudder * k_sideslip


def bank_loop_poles(plants: Plants, gains: dict[str, float]) -> np.ndarray:
    """Poles of the bank-attitude inner loop alone (heading loop open): [v, p, r, phi] with
    aileron = -kBankP phi - kRollRate p (phi command held at 0) and the rudder yaw damper closed."""
    a, b = plants.a_lat[:4, :4], plants.b_lat[:4]
    c = np.zeros((2, 4))
    c[0, 3] = -gains["kBankP"]
    c[0, 1] = -gains["kRollRate"]
    c[1, 2] = gains["kYawRate"]
    c[1, 0] = gains.get("kSideslip", 0.0) / plants.true_airspeed_mps
    return np.linalg.eigvals(a + b @ c)


def pitch_loop_poles(plants: Plants, gains: dict[str, float]) -> np.ndarray:
    """Poles of the pitch-attitude inner loop alone (altitude and speed loops open): [u, w, q,
    theta] with elevator = kPitchTheta theta + kPitchRate q and the throttle held."""
    a, b = plants.a_lon[:4, :4], plants.b_lon[:4, 0:1]
    c = np.zeros((1, 4))
    c[0, 3] = gains["kPitchTheta"]
    c[0, 2] = gains["kPitchRate"]
    return np.linalg.eigvals(a + b @ c)


def fastest_oscillatory_pair(poles: np.ndarray) -> complex:
    pairs = [complex(p) for p in poles if p.imag > 1e-6]
    if not pairs:
        raise ValueError(f"no oscillatory pole in {poles!r}")
    return max(pairs, key=abs)


def slowest_poles(poles: np.ndarray, count: int) -> list[complex]:
    """The `count` poles of smallest magnitude (heading-loop poles in the lateral system)."""
    return sorted((complex(p) for p in poles), key=abs)[:count]
