"""The trimmed cruise.yaml (M3 PR 8, docs/plans/pr8-cruise-regression.md): its own initial state is
a still-air trim (D19), and a small-perturbation open-loop run from it shows the phugoid the
linearized model predicts (D21).

Still air throughout. cruise.yaml configures Dryden turbulence (seed 42), whose frame-0 gust is a
stationary-start draw, not zero -- wind_n = (-3.08, 0.91, 6.63) m/s -- so the trim residual with
that gust latched is 3.77 m/s^2 against 8.3e-17 without it (plan finding 3). The trim itself is a
still-air trim (astra.trim drops physical.wind), and a never-stepped wind-configured Simulation
refuses derivative() anyway (PR 5 R7a). Every Simulation here is built from cruise.yaml with
physical.wind removed, the same way trim._load_still_air_config does, and nothing else changed
except the stated perturbation.

Phugoid reference (Joe's Q2 decision): PR 7's numerical A -- its longitudinal [u, w, q, theta]
block, unchanged -- plus the altitude state h. PR 7's A holds position fixed (D11), so it has no
d/dh, and against this run its phugoid period is 13% long; S&L Example 3.8-5 (Sec. 3.8, pp.
212-213) describes exactly this: with the altitude state in A, the atmosphere model adds small
coupling terms and the nonlinear response "agrees closely with the linear model with altitude
included." The added column is a central difference in p_n[2] through linearize._evaluate itself,
about the same LinearizeResult's own Simulation, trim state, x0 and u0; the added row is the
kinematic h_dot = V_T sin(gamma) = u sin(theta) - w cos(theta) (S&L eq. 3.6-2 at beta = phi = 0;
h = -p_D per Sec. 2.5). The 4x4 (altitude-free) phugoid is printed, never asserted.

Measurement. u is sampled at 1 Hz; the first 20 s are dropped (the short period, zeta 0.55 at
omega_n 1.0 rad/s, has decayed by e^-11 by then). Period: the interval between the first two
positive-going zero crossings of u - u_trim, each linearly interpolated between samples. u_trim is
the trimmed config's own v_b[0] -- the 5x5 model's own equilibrium. Damping: logarithmic decrement
of the first two peaks (the largest sample between successive positive-going crossings). No
reference in docs/refs/ defines the log decrement, so it is derived here from the second-order
standard form S&L Sec. 4.2 uses for the phugoid (eqs. 4.2-13 to 4.2-15, "The Phugoid
Approximation", pp. 261-263): x(t) = A exp(-zeta omega_n t) cos(omega_d t + phi), omega_d =
omega_n sqrt(1 - zeta^2), puts successive peaks T_d = 2 pi / omega_d apart, so
delta = ln(x_1 / x_2) = zeta omega_n T_d = 2 pi zeta / sqrt(1 - zeta^2), and inverting,
zeta = delta / sqrt(4 pi^2 + delta^2).

Tolerances (5% period, 0.02 absolute zeta) are approximate, set by Joe for the 4x4 comparison;
against the 5x5 they carry slack (plan finding 6: agreement within 0.5% / 0.004 under every
reasonable reference choice). A miss is a finding, never a tolerance to widen.
"""

from __future__ import annotations

import itertools
import math
from dataclasses import dataclass
from pathlib import Path
from typing import Any

import numpy as np
import pytest
import yaml

from astra import _core, linearize

CRUISE_CONFIG = "aircraft/notional_transport/configs/cruise.yaml"
TRUE_AIRSPEED_MPS = 230.0
ALTITUDE_M = 10_000.0

# D21.
SPEED_PERTURBATION_MPS = 2.0
RUN_SECONDS = 300
FRAMES_PER_SAMPLE = 1000  # 1 Hz at dt = 1 ms
TRANSIENT_DISCARD_S = 20.0
PERIOD_RELATIVE_TOLERANCE = 0.05  # approximate
DAMPING_ABSOLUTE_TOLERANCE = 0.02  # approximate

# Central-difference step for the altitude column (plan finding 5: the column agrees to 8-9
# significant figures for 0.5, 1 and 2 m).
ALTITUDE_STEP_M = 1.0

_U, _W, _THETA = 0, 2, 7  # D11's x = [u, v, w, p, q, r, phi, theta, psi]


def _still_air_data() -> dict[str, Any]:
    data: dict[str, Any] = yaml.safe_load(Path(CRUISE_CONFIG).read_text(encoding="utf-8"))
    data["physical"].pop("wind", None)
    return data


def _simulation(data: dict[str, Any]) -> _core.Simulation:
    return _core.Simulation.from_config(
        _core.Config.from_dict(data, origin=CRUISE_CONFIG), _core.default_plugin_dir()
    )


def _d8_residual(sim: _core.Simulation, state: np.ndarray) -> np.ndarray:
    """D8 (docs/plans/pr6-trim.md): [v_dot_b, omega_dot_b] from derivative()'s named slices."""
    dq = sim.derivative(state)
    return np.concatenate(
        [
            dq[_core.VELOCITY : _core.VELOCITY + _core.VELOCITY_SIZE],
            dq[_core.ANGULAR_RATE : _core.ANGULAR_RATE + _core.ANGULAR_RATE_SIZE],
        ]
    )


def test_cruise_config_initial_state_is_trimmed() -> None:
    # D19 acceptance: the config's own initial state and controls, no trim call.
    sim = _simulation(_still_air_data())
    state = np.array(sim.vehicles[0].state)
    assert np.max(np.abs(_d8_residual(sim, state))) < 1e-6


@dataclass(frozen=True)
class _Response:
    t_s: np.ndarray
    u_minus_trim_mps: np.ndarray


@pytest.fixture(scope="module")
def response() -> _Response:
    data = _still_air_data()
    u_trim = float(data["initial"]["state"]["v_b"][0])
    data["initial"]["state"]["v_b"][0] = u_trim + SPEED_PERTURBATION_MPS
    sim = _simulation(data)
    signal = f"{sim.vehicles[0].name}.state.v_b_0"

    def u() -> float:
        value = sim.registry.find(signal)
        assert value is not None, signal
        return value

    samples = [u()]
    for _ in range(RUN_SECONDS):
        halt = sim.run(FRAMES_PER_SAMPLE)
        assert not halt, f"halted: {halt.check} from {halt.source} at frame {halt.frame}"
        samples.append(u())
    t = np.arange(len(samples), dtype=float)
    keep = t >= TRANSIENT_DISCARD_S
    return _Response(t[keep], np.array(samples)[keep] - u_trim)


def _positive_going_crossings(r: _Response) -> list[float]:
    t, y = r.t_s, r.u_minus_trim_mps
    return [
        float(t[i] - y[i] * (t[i + 1] - t[i]) / (y[i + 1] - y[i]))
        for i in range(len(y) - 1)
        if y[i] < 0.0 <= y[i + 1]
    ]


def _measured_period_s(r: _Response) -> float:
    crossings = _positive_going_crossings(r)
    assert len(crossings) >= 2, f"need two positive-going crossings, got {crossings}"
    return crossings[1] - crossings[0]


def _measured_damping_ratio(r: _Response) -> float:
    crossings = _positive_going_crossings(r)
    assert len(crossings) >= 3, f"need three positive-going crossings, got {crossings}"
    peaks = [
        float(np.max(r.u_minus_trim_mps[(r.t_s >= lo) & (r.t_s < hi)]))
        for lo, hi in itertools.pairwise(crossings[:3])
    ]
    delta = math.log(peaks[0] / peaks[1])
    return delta / math.sqrt(4.0 * math.pi**2 + delta**2)


@dataclass(frozen=True)
class _Reference:
    damped_period_s: float
    damping_ratio: float
    four_by_four_damped_period_s: float
    four_by_four_damping_ratio: float


def _altitude_column(result: linearize.LinearizeResult) -> np.ndarray:
    """d f / d h (9,) about the LinearizeResult's own trim: linearize._evaluate at base_state with
    p_n[2] +/- ALTITUDE_STEP_M, the same two-point central difference and the same restoration of
    u0 afterwards that linearize._column applies to an x or u column."""
    sim = result.simulation
    base = np.array(result.trim.state, dtype=float)
    altitude_index = _core.POSITION + _core.POSITION_SIZE - 1
    plus, minus = base.copy(), base.copy()
    plus[altitude_index] += ALTITUDE_STEP_M
    minus[altitude_index] -= ALTITUDE_STEP_M
    f_plus = linearize._evaluate(sim, plus, result.x0, result.u0)  # pyright: ignore[reportPrivateUsage]
    f_minus = linearize._evaluate(sim, minus, result.x0, result.u0)  # pyright: ignore[reportPrivateUsage]
    for name, value in zip(sim.input_names(), result.u0, strict=True):
        sim.write_input(name, value)
    return (f_plus - f_minus) / (2.0 * ALTITUDE_STEP_M)


def _altitude_rate_row(x0: np.ndarray) -> np.ndarray:
    """d h_dot / d [u, w, q, theta] for h_dot = u sin(theta) - w cos(theta), at x0."""
    u, w, theta = x0[_U], x0[_W], x0[_THETA]
    return np.array(
        [math.sin(theta), -math.cos(theta), 0.0, u * math.cos(theta) + w * math.sin(theta)]
    )


@pytest.fixture(scope="module")
def linear() -> linearize.LinearizeResult:
    return linearize.linearize(CRUISE_CONFIG, TRUE_AIRSPEED_MPS, ALTITUDE_M)


@pytest.fixture(scope="module")
def reference(linear: linearize.LinearizeResult) -> _Reference:
    longitudinal = linearize._LONGITUDINAL_INDICES  # pyright: ignore[reportPrivateUsage]
    a5 = np.zeros((5, 5))
    a5[:4, :4] = linearize.longitudinal_block(linear.a)
    a5[:4, 4] = _altitude_column(linear)[list(longitudinal)]
    a5[4, :4] = _altitude_rate_row(linear.x0)

    eigenvalues = np.linalg.eigvals(a5)
    oscillatory = sorted((complex(e) for e in eigenvalues if e.imag > 1e-6), key=abs)
    real = [e for e in eigenvalues if abs(e.imag) <= 1e-6]
    assert len(oscillatory) == 2, f"unexpected 5x5 eigenvalues {eigenvalues!r}"
    assert len(real) == 1, f"unexpected 5x5 eigenvalues {eigenvalues!r}"
    phugoid = oscillatory[0]  # the slower pair; the faster is the short period

    four = linearize.modes(linear.a).phugoid
    four_omega_d = four.natural_frequency_rad_s * math.sqrt(1.0 - four.damping_ratio**2)
    return _Reference(
        damped_period_s=2.0 * math.pi / phugoid.imag,
        damping_ratio=-phugoid.real / abs(phugoid),
        four_by_four_damped_period_s=2.0 * math.pi / four_omega_d,
        four_by_four_damping_ratio=four.damping_ratio,
    )


def _report(ref: _Reference, label: str, measured: float) -> str:
    text = (
        f"{label}: measured {measured:.6g}; 5x5 (PR 7 A + altitude) period "
        f"{ref.damped_period_s:.6g} s, zeta {ref.damping_ratio:.6g}; 4x4 (PR 7 A, printed, not "
        f"asserted) period {ref.four_by_four_damped_period_s:.6g} s, zeta "
        f"{ref.four_by_four_damping_ratio:.6g}"
    )
    print(text)
    return text


def test_phugoid_period_matches_the_linear_model(
    response: _Response, reference: _Reference
) -> None:
    measured = _measured_period_s(response)
    message = _report(reference, "damped period (s)", measured)
    assert abs(measured / reference.damped_period_s - 1.0) < PERIOD_RELATIVE_TOLERANCE, message


def test_phugoid_damping_matches_the_linear_model(
    response: _Response, reference: _Reference
) -> None:
    measured = _measured_damping_ratio(response)
    message = _report(reference, "damping ratio", measured)
    assert abs(measured - reference.damping_ratio) < DAMPING_ABSOLUTE_TOLERANCE, message


def test_altitude_rate_row_matches_the_derivative(linear: linearize.LinearizeResult) -> None:
    # The hand-written h_dot row above, against a central difference of derivative()'s own
    # altitude rate (p_n[2]'s derivative under wgs84_rotating, [lat, lon, altitude]) with PR 7's
    # own D12 steps -- so the kinematic row cannot silently disagree with the Earth model.
    sim = linear.simulation
    base = np.array(linear.trim.state, dtype=float)
    altitude_index = _core.POSITION + _core.POSITION_SIZE - 1

    def altitude_rate(x: np.ndarray) -> float:
        state = linearize._state_from_x(base, x)  # pyright: ignore[reportPrivateUsage]
        return float(sim.derivative(state)[altitude_index])

    assert abs(altitude_rate(linear.x0)) < 1e-9  # level flight: gamma = 0 at trim
    row = _altitude_rate_row(linear.x0)
    for column, index in ((0, _U), (1, _W), (3, _THETA)):
        step = linearize.state_step(index)
        plus, minus = linear.x0.copy(), linear.x0.copy()
        plus[index] += step
        minus[index] -= step
        numerical = (altitude_rate(plus) - altitude_rate(minus)) / (2.0 * step)
        assert numerical == pytest.approx(row[column], rel=1e-6), f"x index {index}"
