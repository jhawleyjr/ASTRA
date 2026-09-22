"""astra.linearize: central-difference linearization about astra.trim's cruise trim (M3 PR 7,
docs/plans/pr7-linearize.md).

State (D11, Euler-angle form, S&L eq. 1.8-16/1.4-4 -- not S&L Sec. 4.1, which is historical prose
with no equations; see the plan's own citation correction): ``x = [u, v, w, p, q, r, phi, theta,
psi]`` (9,) -- body velocity ``v_b``, body rate ``omega_b``, then Euler angles in ``(phi, theta,
psi)`` order. Note the permutation trap: ``frames.hpp``'s own ``quaternion_from_euler`` (and this
module's own port of it) takes the arguments in ``(psi, theta, phi)`` order -- ``x``'s own slice
must be reversed, not passed straight through. Control vector ``u = [elevator, aileron, rudder,
throttle]``, in ``Simulation.input_names()``'s own registration order (read from the registry, not
hardcoded -- the PR 6 follow-up this PR also delivers, ``Simulation.input_names``).

The function being differenced, ``f(x, u)`` (9,): build a full combined state from ``x`` (position
held fixed at the trim's own value), write ``u`` through ``Simulation.write_input``, call
``sim.derivative(state)`` for ``v_dot``/``omega_dot`` (the same named-slice pattern
``astra.trim._residual6`` uses), and compute the Euler-rate three rows directly from ``omega_b`` and
``(phi, theta)`` via the standard kinematic matrix (S&L eq. 1.4-4) -- never from
``derivative()``'s own ``q_dot_nb`` slice, which is a different (quaternion-rate) representation of
the same physical quantity.

Central differencing (D12): fixed step sizes (not adaptive, unlike ``astra.trim``'s own ``xtol``
search) -- 0.1 m/s for ``v_b``, 1e-3 rad/s for ``omega_b``, 1e-3 rad for the three Euler angles and
the three surface controls, 1e-2 for throttle. The per-column routine (``_column``) is its own
callable seam (D15): it restores ``u0`` via ``write_input`` before returning, every time, so a test
can drive it column by column and check isolation after each individual call -- not only once after
a full sweep. ``LinearizeResult`` carries the actual ``Simulation`` instance ``_column`` perturbed
(D14), since ``TrimResult`` itself carries none and a separately built "same config" instance would
pass an isolation check vacuously.

Mode classification (D13) is by eigenvalue magnitude within each 4x4 block (S&L Sec. 4.2's own
physical description: short period is much faster than phugoid, roll subsidence much faster than
spiral) -- ``modes()`` raises if the expected complex-pair/real-pole shape or separation isn't
found, rather than silently mislabeling. Roll and spiral are reported and must be compared as signed
``Re(lambda)`` (D16) -- the spiral mode may be mildly unstable at cruise (a positive real part), and
``abs()`` on either side of a roll/spiral comparison would hide exactly that disagreement. The
closed-form S&L Sec. 4.2 comparison itself is computed in ``tests/python/test_linearize.py``, not
here (D13: "computed in the test from cruise.yaml's own coefficients") -- this module reports only
the numerical modes.
"""

from __future__ import annotations

import os
from dataclasses import dataclass
from typing import Literal

import numpy as np

from astra import _core, trim

# D11: x = [u, v, w, p, q, r, phi, theta, psi].
_U, _V, _W = 0, 1, 2
_P, _Q, _R = 3, 4, 5
_PHI, _THETA, _PSI = 6, 7, 8
_STATE_SIZE = 9

# D11: u = [elevator, aileron, rudder, throttle], conceptually -- the actual column order in u0/B
# follows Simulation.input_names()'s own registration order at runtime, whatever it is, mapped by
# name (linearize()'s own control_trim_values dict) rather than assumed by position (Codex general
# review, base 53064e0: a fixed-position u0 silently mismatched control values to names once a
# config's vehicle.models listed the turbofans before the aero model, reproduced with a
# linearization residual of ~1.33 against a healthy <1e-16).
_CONTROL_NAMES = ("controls.elevator", "controls.aileron", "controls.rudder", "controls.throttle")
_CONTROL_SIZE = len(_CONTROL_NAMES)

# D12.
_V_STEP_MPS = 0.1
_OMEGA_STEP_RAD_S = 1e-3
_ANGLE_STEP_RAD = 1e-3
_THROTTLE_STEP = 1e-2

# D13: longitudinal from [u, w, q, theta], lateral from [v, p, r, phi] -- psi (index 8) belongs to
# neither block (S&L eq. 2.6-5a drops psi from the linearized dynamics at psi_e = 0).
_LONGITUDINAL_INDICES = (_U, _W, _Q, _THETA)
_LATERAL_INDICES = (_V, _P, _R, _PHI)

# D13/D16: a mode's own magnitude must be at least this much larger than its slower/spiral
# counterpart's for the magnitude-based classification to be trusted -- below this, raise rather
# than silently pick one (Validation and risks, docs/plans/pr7-linearize.md).
_MIN_SEPARATION_RATIO = 2.0

# A pair of complex-conjugate eigenvalues is identified by |Im(lambda)| clearing this floor,
# relative to nothing else -- eigenvalues here are O(1) rad/s in every axis, so an absolute floor
# many orders below the smallest expected natural frequency (a bare numerical-noise guard, not a
# physical tolerance) is enough to separate a genuine complex pair from floating-point residue on a
# real eigenvalue numpy's own eigensolver leaves behind.
_REAL_EIGENVALUE_IMAG_TOL = 1e-9


def state_step(index: int) -> float:
    """D12's own fixed step for x's index-th component."""
    if index in (_U, _V, _W):
        return _V_STEP_MPS
    if index in (_P, _Q, _R):
        return _OMEGA_STEP_RAD_S
    if index in (_PHI, _THETA, _PSI):
        return _ANGLE_STEP_RAD
    raise ValueError(f"linearize.state_step: index {index} out of range [0, {_STATE_SIZE})")


def control_step(name: str) -> float:
    """D12's own fixed step for the named control input -- keyed by name, not position, since
    Simulation.input_names()'s own registration order is not guaranteed to match D11's u =
    [elevator, aileron, rudder, throttle] listing (Codex general review, base 53064e0)."""
    if name in ("controls.elevator", "controls.aileron", "controls.rudder"):
        return _ANGLE_STEP_RAD
    if name == "controls.throttle":
        return _THROTTLE_STEP
    raise ValueError(f"linearize.control_step: unrecognized control name {name!r}")


def _quaternion_from_euler(psi_rad: float, theta_rad: float, phi_rad: float) -> np.ndarray:
    """S&L eq. 1.8-16, ported line for line from `core/include/astra/util/frames.hpp::
    quaternion_from_euler` (half angles a = phi/2, b = theta/2, c = psi/2) -- the single core-side
    reference this port must match, checked directly by
    tests/python/test_linearize.py::test_quaternion_from_euler_matches_the_real_construction against
    Simulation.from_config's own initial.state.euler_nb construction, the same convention
    astra.trim._state_from_alpha's own docstring already establishes for its collapsed special
    case."""
    ca = np.cos(phi_rad / 2.0)
    sa = np.sin(phi_rad / 2.0)
    cb = np.cos(theta_rad / 2.0)
    sb = np.sin(theta_rad / 2.0)
    cc = np.cos(psi_rad / 2.0)
    sc = np.sin(psi_rad / 2.0)
    return np.array(
        [
            ca * cb * cc + sa * sb * sc,
            sa * cb * cc - ca * sb * sc,
            ca * sb * cc + sa * cb * sc,
            ca * cb * sc - sa * sb * cc,
        ]
    )


def _euler_rate(omega_b: np.ndarray, phi_rad: float, theta_rad: float) -> np.ndarray:
    """S&L eq. 1.4-4, the Euler kinematical equations: [phi_dot, theta_dot, psi_dot] = H(Phi)
    [P, Q, R] -- a closed-form kinematic identity in (omega_b, phi, theta) alone, independent of the
    aerodynamic/EOM derivative `Simulation.derivative` returns (D11's own instruction: these three
    rows of f(x, u) are not read from derivative()'s own q_dot_nb slice)."""
    p, q, r = omega_b
    tan_theta = np.tan(theta_rad)
    phi_dot = p + q * np.sin(phi_rad) * tan_theta + r * np.cos(phi_rad) * tan_theta
    theta_dot = q * np.cos(phi_rad) - r * np.sin(phi_rad)
    psi_dot = (q * np.sin(phi_rad) + r * np.cos(phi_rad)) / np.cos(theta_rad)
    return np.array([phi_dot, theta_dot, psi_dot])


def _state_from_x(base_state: np.ndarray, x: np.ndarray) -> np.ndarray:
    """Builds a full combined state (base_state's own shape, e.g. any state-owning model's own
    slice held fixed at the trim value D11 never perturbs) with the rigid-body block overwritten
    from x: v_b/omega_b directly, q_nb via the ported eq. 1.8-16 from x's own (phi, theta, psi)
    slice -- permuted to the (psi, theta, phi) argument order `_quaternion_from_euler` (and the real
    `frames.hpp`) expect, not passed straight through."""
    state = base_state.copy()
    state[_core.VELOCITY : _core.VELOCITY + _core.VELOCITY_SIZE] = x[_U : _W + 1]
    state[_core.ANGULAR_RATE : _core.ANGULAR_RATE + _core.ANGULAR_RATE_SIZE] = x[_P : _R + 1]
    state[_core.QUATERNION : _core.QUATERNION + _core.QUATERNION_SIZE] = _quaternion_from_euler(
        psi_rad=x[_PSI], theta_rad=x[_THETA], phi_rad=x[_PHI]
    )
    return state


def _evaluate(
    sim: _core.Simulation, base_state: np.ndarray, x: np.ndarray, u: np.ndarray
) -> np.ndarray:
    """f(x, u) (9,): writes u's four components through Simulation.write_input (names read from
    sim.input_names(), not hardcoded), builds the full state from x, evaluates v_dot/omega_dot via
    Simulation.derivative's own named slices, and the Euler-rate rows via the closed-form eq. 1.4-4
    kinematic identity -- never from derivative()'s own q_dot_nb slice."""
    for name, value in zip(sim.input_names(), u, strict=True):
        sim.write_input(name, value)
    state = _state_from_x(base_state, x)
    dq = sim.derivative(state)
    v_dot = dq[_core.VELOCITY : _core.VELOCITY + _core.VELOCITY_SIZE]
    omega_dot = dq[_core.ANGULAR_RATE : _core.ANGULAR_RATE + _core.ANGULAR_RATE_SIZE]
    euler_dot = _euler_rate(x[_P : _R + 1], phi_rad=x[_PHI], theta_rad=x[_THETA])
    return np.concatenate([v_dot, omega_dot, euler_dot])


def _column(
    sim: _core.Simulation,
    base_state: np.ndarray,
    x0: np.ndarray,
    u0: np.ndarray,
    index: int,
    step: float,
    kind: Literal["x", "u"],
) -> np.ndarray:
    """One central-difference column (D12/D15): perturbs x0 or u0 at `index` by +-step, evaluates
    f at both points, and -- regardless of kind, unconditionally -- restores u0's own four
    components via write_input before returning, the one place D12's "trim inputs are restored
    after every column" is actually enforced in code. A separately callable seam so a test can
    drive it column by column and check isolation after each individual call (D15), not only once
    after a full linearize() sweep."""
    if kind == "x":
        x_plus, x_minus = x0.copy(), x0.copy()
        x_plus[index] += step
        x_minus[index] -= step
        f_plus = _evaluate(sim, base_state, x_plus, u0)
        f_minus = _evaluate(sim, base_state, x_minus, u0)
    else:
        u_plus, u_minus = u0.copy(), u0.copy()
        u_plus[index] += step
        u_minus[index] -= step
        f_plus = _evaluate(sim, base_state, x0, u_plus)
        f_minus = _evaluate(sim, base_state, x0, u_minus)

    for name, value in zip(sim.input_names(), u0, strict=True):
        sim.write_input(name, value)

    return (f_plus - f_minus) / (2.0 * step)


@dataclass(frozen=True)
class LinearizeResult:
    """D14: `simulation` is the actual instance `_column` perturbed -- not a separately built "same
    config" instance, which would pass a column-isolation check (R6) vacuously."""

    trim: trim.TrimResult
    simulation: _core.Simulation
    x0: np.ndarray
    u0: np.ndarray
    input_names: list[str]
    a: np.ndarray
    b: np.ndarray


def linearize(
    config_path: str | os.PathLike[str],
    true_airspeed_mps: float,
    altitude_m: float,
    *,
    v_step: float = _V_STEP_MPS,
    omega_step: float = _OMEGA_STEP_RAD_S,
    angle_step: float = _ANGLE_STEP_RAD,
    throttle_step: float = _THROTTLE_STEP,
    **trim_kwargs: float,
) -> LinearizeResult:
    """Central-differences A/B (D12) about `astra.trim.trim_straight_and_level`'s own solution at
    `config_path`/`true_airspeed_mps`/`altitude_m` (`trim_kwargs` forwarded to it unchanged, e.g. a
    tighter `xtol`). Builds its own still-air `Simulation` for the differencing (`TrimResult` itself
    carries none) -- never re-solves trim, never steps the `Simulation`. The `v_step`/`omega_step`/
    `angle_step`/`throttle_step` keyword overrides exist for R1's own halved-step comparison, not
    for routine callers -- D12 pins the defaults.
    """
    trim_result = trim.trim_straight_and_level(
        config_path, true_airspeed_mps, altitude_m, **trim_kwargs
    )
    # Same-package private reuse (docs/plans/pr7-linearize.md, D14) -- not a duplicated second copy
    # of the wind-dropping logic.
    config = trim._load_still_air_config(config_path)  # pyright: ignore[reportPrivateUsage]
    sim = _core.Simulation.from_config(config, _core.default_plugin_dir())

    base_state = np.array(trim_result.state, dtype=float)
    v_b = base_state[_core.VELOCITY : _core.VELOCITY + _core.VELOCITY_SIZE]
    omega_b = base_state[_core.ANGULAR_RATE : _core.ANGULAR_RATE + _core.ANGULAR_RATE_SIZE]
    # trim_straight_and_level's own scenario (docs/plans/pr6-trim.md): phi_e = psi_e = 0, theta_e =
    # alpha_rad -- not re-derived from base_state's own q_nb by inverse quaternion.
    x0 = np.array(
        [
            v_b[0],
            v_b[1],
            v_b[2],
            omega_b[0],
            omega_b[1],
            omega_b[2],
            0.0,
            trim_result.alpha_rad,
            0.0,
        ]
    )
    # aileron/rudder pinned to 0 by the same straight-and-level scenario. Keyed by name, not
    # position: Simulation.input_names()'s own registration order is whatever vehicle.models
    # actually lists, not necessarily D11's [elevator, aileron, rudder, throttle] listing order.
    control_trim_values = {
        "controls.elevator": trim_result.elevator_rad,
        "controls.aileron": 0.0,
        "controls.rudder": 0.0,
        "controls.throttle": trim_result.throttle,
    }
    control_steps = {
        "controls.elevator": angle_step,
        "controls.aileron": angle_step,
        "controls.rudder": angle_step,
        "controls.throttle": throttle_step,
    }
    input_names = sim.input_names()
    if set(input_names) != set(_CONTROL_NAMES):
        raise ValueError(
            f"linearize: Simulation.input_names() returned {input_names!r}, expected exactly "
            f"D11's four controls {sorted(_CONTROL_NAMES)!r}"
        )
    u0 = np.array([control_trim_values[name] for name in input_names])

    a = np.empty((_STATE_SIZE, _STATE_SIZE))
    for i in range(_STATE_SIZE):
        step = {
            **{k: v_step for k in (_U, _V, _W)},
            **{k: omega_step for k in (_P, _Q, _R)},
            **{k: angle_step for k in (_PHI, _THETA, _PSI)},
        }[i]
        a[:, i] = _column(sim, base_state, x0, u0, i, step, "x")

    b = np.empty((_STATE_SIZE, _CONTROL_SIZE))
    for j, name in enumerate(input_names):
        b[:, j] = _column(sim, base_state, x0, u0, j, control_steps[name], "u")

    return LinearizeResult(
        trim=trim_result,
        simulation=sim,
        x0=x0,
        u0=u0,
        input_names=input_names,
        a=a,
        b=b,
    )


def longitudinal_block(a: np.ndarray) -> np.ndarray:
    """D13: the 4x4 [u, w, q, theta] block."""
    return a[np.ix_(_LONGITUDINAL_INDICES, _LONGITUDINAL_INDICES)]


def lateral_block(a: np.ndarray) -> np.ndarray:
    """D13: the 4x4 [v, p, r, phi] block."""
    return a[np.ix_(_LATERAL_INDICES, _LATERAL_INDICES)]


@dataclass(frozen=True)
class OscillatoryMode:
    natural_frequency_rad_s: float
    damping_ratio: float


@dataclass(frozen=True)
class RealMode:
    """D16: `sigma_per_s` is the signed `Re(lambda)` -- never report or compare `abs()` of this;
    the spiral mode may be mildly unstable at cruise (a positive value)."""

    sigma_per_s: float


@dataclass(frozen=True)
class Modes:
    short_period: OscillatoryMode
    phugoid: OscillatoryMode
    dutch_roll: OscillatoryMode
    roll: RealMode
    spiral: RealMode


def _oscillatory_mode(lam: complex) -> OscillatoryMode:
    omega_n = abs(lam)
    zeta = -lam.real / omega_n
    return OscillatoryMode(omega_n, zeta)


def _split_by_kind(eigenvalues: np.ndarray) -> tuple[list[complex], list[complex]]:
    reals = [complex(e) for e in eigenvalues if abs(e.imag) < _REAL_EIGENVALUE_IMAG_TOL]
    complexes = [complex(e) for e in eigenvalues if abs(e.imag) >= _REAL_EIGENVALUE_IMAG_TOL]
    return reals, complexes


def _require_separation(fast: complex | float, slow: complex | float, label: str) -> None:
    fast_mag, slow_mag = abs(fast), abs(slow)
    if slow_mag > 0 and fast_mag < _MIN_SEPARATION_RATIO * slow_mag:
        raise ValueError(
            f"linearize.modes: {label} candidates are not clearly separated by magnitude "
            f"(|{fast_mag:.6g}| vs |{slow_mag:.6g}|, need >= {_MIN_SEPARATION_RATIO}x) -- "
            "classification by magnitude is not trustworthy here; report this as a finding, "
            "not something to silently pick a side on"
        )


def modes(a: np.ndarray) -> Modes:
    """D13/D16: eigenvalues of the longitudinal/lateral 4x4 blocks, classified by magnitude (short
    period faster than phugoid, roll faster than spiral) -- raises if the expected complex-pair/
    real-pole shape, or the expected magnitude separation, isn't found, rather than silently
    mislabeling (Validation and risks, docs/plans/pr7-linearize.md)."""
    long_eigs = np.linalg.eigvals(longitudinal_block(a))
    long_reals, long_complex = _split_by_kind(long_eigs)
    if long_reals or len(long_complex) != 4:
        raise ValueError(
            "linearize.modes: expected two complex-conjugate pairs (short period, phugoid) in the "
            f"longitudinal block, got eigenvalues {long_eigs!r}"
        )
    long_reps = sorted((e for e in long_complex if e.imag > 0), key=lambda e: -abs(e))
    if len(long_reps) != 2:
        raise ValueError(
            f"linearize.modes: expected exactly two positive-imaginary representatives in the "
            f"longitudinal block, got eigenvalues {long_eigs!r}"
        )
    short_period_lambda, phugoid_lambda = long_reps
    _require_separation(short_period_lambda, phugoid_lambda, "short period/phugoid")

    lat_eigs = np.linalg.eigvals(lateral_block(a))
    lat_reals, lat_complex = _split_by_kind(lat_eigs)
    if len(lat_reals) != 2 or len(lat_complex) != 2:
        raise ValueError(
            "linearize.modes: expected two real eigenvalues (roll, spiral) plus one "
            "complex-conjugate pair (Dutch roll) in the lateral block, got eigenvalues "
            f"{lat_eigs!r}"
        )
    roll_lambda, spiral_lambda = sorted(lat_reals, key=lambda e: -abs(e.real))
    _require_separation(roll_lambda.real, spiral_lambda.real, "roll/spiral")
    dutch_roll_lambda = lat_complex[0] if lat_complex[0].imag > 0 else lat_complex[1]

    return Modes(
        short_period=_oscillatory_mode(short_period_lambda),
        phugoid=_oscillatory_mode(phugoid_lambda),
        dutch_roll=_oscillatory_mode(dutch_roll_lambda),
        roll=RealMode(roll_lambda.real),
        spiral=RealMode(spiral_lambda.real),
    )
