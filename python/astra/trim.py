"""astra.trim: static straight-and-level trim at a fixed flight condition (M3 PR 6,
docs/plans/pr6-trim.md).

Solves for (elevator, throttle, alpha) that zero the longitudinal block of
``Simulation.derivative(state)`` -- v_dot_x, v_dot_z, q_dot -- via ``scipy.optimize.root``. The
scenario is pinned to straight-and-level (beta = phi = psi = 0, omega_b = 0, aileron = rudder = 0,
still air): gamma = 0 makes theta = alpha, collapsing the nominal 4-unknown/6-residual trim to this
square 3x3 problem. v_y_dot/p_dot/r_dot are expected to vanish by symmetry at the solution (heading
north at latitude 0 puts the Earth-rate vector parallel to velocity, and this aircraft's only
off-diagonal inertia term, ixz, couples only x/z) -- checked at the solution, not assumed silently.

Never steps the Simulation: this is a static algebraic solve, and `Simulation.derivative()` is
bit-identical to the real integrator's own k1 (PR 5's R9). Still air only -- wind is dropped from
the config entirely (see `_load_still_air_config`), since `derivative()` raises on a
wind-configured, never-stepped Simulation by design (PR 5's R7a).

Control names ("controls.elevator", "controls.throttle") are hardcoded below, not read from the
registry -- a deviation from Joe's original instruction, accepted for PR 6 (docs/plans/pr6-trim.md,
D10) because the Python binding exposes no input-name enumeration (`InputRegistry::names()` exists
in C++, PR 2, but isn't bound yet). `write_input`'s own raise-on-unregistered-name is a runtime
guard against a wrong or stale name, not a substitute for actually reading one. PLAN.md's PR 7 line
carries the follow-up: bind `InputRegistry::names()` so trim/linearize can discover control names
instead of hardcoding them.
"""

from __future__ import annotations

import os
from dataclasses import dataclass
from typing import Any, cast

import numpy as np
import scipy.optimize  # pyright: ignore[reportMissingTypeStubs] -- scipy ships no py.typed marker
import yaml

from astra import _core


@dataclass(frozen=True)
class TrimResult:
    """The solved trim point: `state` is the length-STATE_SIZE rigid-body state; `residual` is the
    6-vector [v_dot_b, omega_dot_b] (D8), recomputed fresh at this `state` after the solve. This
    class does not itself enforce any bound on `residual` -- `trim_straight_and_level` only raises
    when scipy reports `success=False`; a caller relying on a specific bound (e.g. D8's 1e-6) must
    assert it against `residual` itself, not assume it from a successful return."""

    state: np.ndarray
    elevator_rad: float
    throttle: float
    alpha_rad: float
    residual: np.ndarray
    scipy_result: scipy.optimize.OptimizeResult


def _read_yaml_dict(config_path: str | os.PathLike[str]) -> dict[str, Any]:
    with open(config_path, encoding="utf-8") as f:
        return cast(dict[str, Any], yaml.safe_load(f))


def _earth_plugin_path(data: dict[str, Any]) -> str | None:
    """Reads `physical.earth`, accepting both docs/architecture.md §13 spellings (a plain plugin
    path, or a map with its own config next to "plugin") -- mirrors
    `simulation.cpp::resolve_model_spec`'s own contract, not re-derived independently."""
    physical = data.get("physical")
    if not isinstance(physical, dict):
        return None
    earth = cast(dict[str, Any], physical).get("earth")
    if isinstance(earth, dict):
        return cast("str | None", cast(dict[str, Any], earth).get("plugin"))
    return cast("str | None", earth)


def _load_still_air_config(config_path: str | os.PathLike[str]) -> _core.Config:
    """Loads `config_path` as YAML and drops `physical.wind` if present -- still air only (PR 6's
    own scope; see the module docstring). Every other key is passed through unchanged, so the
    config file itself stays the single source of truth for models/coefficients/initial state."""
    data = _read_yaml_dict(config_path)
    physical = data.get("physical")
    if isinstance(physical, dict):
        cast(dict[str, Any], physical).pop("wind", None)
    return _core.Config.from_dict(data, origin=str(config_path))


def _state_from_alpha(p_n: np.ndarray, true_airspeed_mps: float, alpha_rad: float) -> np.ndarray:
    """v_b = [V cos(alpha), 0, V sin(alpha)] (beta = 0); q_nb from euler_nb = [0, alpha, 0], which
    collapses Stevens & Lewis eq. 1.8-16 (`frames.hpp::quaternion_from_euler`) to
    [cos(alpha/2), 0, sin(alpha/2), 0] when psi = phi = 0 -- proved against the real binding by
    `tests/python/test_trim.py::test_state_from_alpha_matches_the_real_quaternion_construction`.
    p_n and omega_b are otherwise fixed (position not solved; omega_b = 0 by scenario)."""
    state = np.zeros(_core.STATE_SIZE)
    state[_core.POSITION : _core.POSITION + _core.POSITION_SIZE] = p_n
    state[_core.VELOCITY : _core.VELOCITY + _core.VELOCITY_SIZE] = [
        true_airspeed_mps * np.cos(alpha_rad),
        0.0,
        true_airspeed_mps * np.sin(alpha_rad),
    ]
    half = alpha_rad / 2.0
    state[_core.QUATERNION : _core.QUATERNION + _core.QUATERNION_SIZE] = [
        np.cos(half),
        0.0,
        np.sin(half),
        0.0,
    ]
    return state


def _residual6(sim: _core.Simulation, state: np.ndarray) -> np.ndarray:
    """D8: the 6-vector [v_dot_b, omega_dot_b] from `derivative(state)`'s named rigid-body slices
    (ADR-0021 R4) -- p_dot_n/q_dot_nb excluded."""
    dq = sim.derivative(state)
    v_dot = dq[_core.VELOCITY : _core.VELOCITY + _core.VELOCITY_SIZE]
    omega_dot = dq[_core.ANGULAR_RATE : _core.ANGULAR_RATE + _core.ANGULAR_RATE_SIZE]
    return np.concatenate([v_dot, omega_dot])


def trim_straight_and_level(
    config_path: str | os.PathLike[str],
    true_airspeed_mps: float,
    altitude_m: float,
    *,
    elevator_guess_rad: float = 0.0,
    throttle_guess: float = 0.5,
    alpha_guess_rad: float = 0.0,
    xtol: float = 1e-12,
) -> TrimResult:
    """Solves straight-and-level trim (beta = phi = psi = 0, omega_b = 0, aileron = rudder = 0,
    still air) for `config_path` at the given true airspeed and altitude.

    Raises `RuntimeError` naming scipy's own message and the last residual if `scipy.optimize.root`
    does not report success. Otherwise returns a `TrimResult` whose own `residual` is recomputed
    fresh at the solution -- never trusts `success` alone (see docs/plans/pr6-trim.md's own
    convergence-tolerance note: `hybr`'s default `xtol` lands right at a typical 1e-6 residual
    bound for this aircraft, so a caller relying on this function's own D8-style bound should pass
    a `xtol` tight enough to clear it, not assume the default suffices).
    """
    earth_plugin = _earth_plugin_path(_read_yaml_dict(config_path))
    if earth_plugin == "plugins/physical/earth/flat":
        # ADR-0003: the Earth model owns the position representation. p_n[-1] below assumes
        # geodetic altitude (round/wgs84_rotating's own [lat, lon, altitude] layout); under
        # earth/flat, p_n[2] is NED down, so the same assignment would place the aircraft at
        # -altitude_m instead. Refuse rather than trim from a silently wrong setup.
        raise ValueError(
            f"trim_straight_and_level: 'physical.earth' is {earth_plugin!r}, but altitude_m is "
            "only meaningful as p_n's last component under a geodetic (round/wgs84_rotating) "
            "Earth model -- pass a config using one of those instead"
        )

    config = _load_still_air_config(config_path)
    sim = _core.Simulation.from_config(config, _core.default_plugin_dir())
    p_n = np.array(sim.vehicles[0].state[_core.POSITION : _core.POSITION + _core.POSITION_SIZE])
    p_n[-1] = altitude_m

    def residual3(x: np.ndarray) -> np.ndarray:
        elevator_rad, throttle, alpha_rad = x
        sim.write_input("controls.elevator", elevator_rad)
        sim.write_input("controls.throttle", throttle)
        state = _state_from_alpha(p_n, true_airspeed_mps, alpha_rad)
        residual = _residual6(sim, state)
        return residual[[0, 2, 4]]  # v_dot_x, v_dot_z, q_dot

    x0 = np.array([elevator_guess_rad, throttle_guess, alpha_guess_rad])
    # scipy ships no type stubs (see the import above); `root`'s own signature and
    # `OptimizeResult`'s dynamic (dict-backed) attributes are unavoidably untyped -- cast at each
    # boundary below so that, not this function's own logic, is what stays unchecked.
    sol = scipy.optimize.root(  # pyright: ignore[reportUnknownMemberType]
        residual3, x0, method="hybr", options={"xtol": xtol}
    )
    solution = cast(np.ndarray, sol.x)  # pyright: ignore[reportUnknownMemberType]
    success = cast(bool, sol.success)  # pyright: ignore[reportUnknownMemberType]
    message = cast(str, sol.message)  # pyright: ignore[reportUnknownMemberType]

    elevator_rad, throttle, alpha_rad = (float(v) for v in solution)
    sim.write_input("controls.elevator", elevator_rad)
    sim.write_input("controls.throttle", throttle)
    state = _state_from_alpha(p_n, true_airspeed_mps, alpha_rad)
    residual = _residual6(sim, state)

    if not success:
        raise RuntimeError(
            f"trim_straight_and_level did not converge: {message} "
            f"(max|residual| = {np.max(np.abs(residual)):.3e})"
        )

    return TrimResult(
        state=state,
        elevator_rad=elevator_rad,
        throttle=throttle,
        alpha_rad=alpha_rad,
        residual=residual,
        scipy_result=sol,
    )
