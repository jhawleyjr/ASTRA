"""astra.trim: static level trim at a fixed flight condition (M3 PR 6, docs/plans/pr6-trim.md;
M7 PR 5, docs/plans/m7-pr5-trim.md).

Solves for (elevator, throttle, pitch) that zero the longitudinal block of
``Simulation.derivative(state)`` -- v_dot_x, v_dot_z, q_dot -- via ``scipy.optimize.root``, at a
given heading, with phi = 0, still air and level flight. The Earth-relative velocity is then v_n = V
(cos psi, sin psi, 0), so v_b = (V cos theta, 0, V sin theta) at any heading: beta = 0 and alpha =
theta (the 3-2-1 C_frd/ned of Stevens & Lewis 3rd ed. eq. 1.3-10 with phi = 0). These are the M7
plan's Q7 unknowns, the three Vol II C.1.11 says the NESC participants varied.

The body rate is the rate convention's (M7 plan Q6; Joe's decision (b), 2026-10-08, makes it an
option
with today's convention the default):
- ``"zero_inertial"`` (the default): omega_b = 0, M3 PR 6's convention (NASA's SIM 2).
- ``"n_frame"``: omega_b is the n frame's own rate, Earth rate plus transport rate in body axes, so
  the Euler angles hold relative to the local vertical (SIM 5's). The rate is the core's own: the
  ``q_nb`` slice of ``derivative()`` is linear in omega_b, so it is evaluated at omega_b = 0 and at
  the three unit rates, and ``J omega = -q_dot(0)`` is solved by least squares (``_n_frame_rate``).
  Any Earth model and the constants it uses are honored; a flat Earth gives omega_b = 0.

Residual policy (M7 plan Q7, N-03): the three solved residuals must be at most
``SOLVED_RESIDUAL_BOUND`` (M3's D8), or the trim raises naming the worst. The lateral three
(v_dot_y, p_dot, r_dot) are reported in ``TrimResult.residual`` and never bounded: under
``"n_frame"`` their kinematic and gravity parts are nonzero by construction
(tests/python/test_trim_kinematics.py derives them in closed form), and an aero model's rate terms
add to them.

The altitude is the Earth model's: ``p_n[2]`` under the geodetic ``earth/round`` and
``earth/wgs84_rotating``, ``-p_n[2]`` under ``earth/flat`` (Q18). Any other Earth plugin is refused:
its position representation is not visible from Python. The two solved inputs are named by keyword
(ICD v1's command names by default) and checked against ``Simulation.input_names()``; every other
registered input keeps the config's initial value, and ``TrimResult.controls`` records them all.

The returned state is the core's own: the core builds it from the emitted ``euler_nb = [psi, theta,
0]`` (``_canonical_state``), so a config rebuilt from ``_runkit.trim_fragment`` reproduces it bit
for bit. Never steps the Simulation: ``Simulation.derivative()`` is bit-identical to the
integrator's own k1 (PR 5's R9). The config is read once, as the core reads it
(``astra._yaml_route``, M6 PR 11). Still air only: wind is dropped from the config
(``_load_still_air_config``), since ``derivative()`` raises on a wind-configured, never-stepped
Simulation by design (PR 5's R7a).
"""

from __future__ import annotations

import os
from dataclasses import dataclass
from typing import Any, Literal, cast

import numpy as np
import scipy.optimize  # pyright: ignore[reportMissingTypeStubs] -- scipy ships no py.typed marker

from astra import _core, _yaml_route

RateConvention = Literal["zero_inertial", "n_frame"]
RATE_CONVENTIONS: tuple[RateConvention, ...] = ("zero_inertial", "n_frame")
DEFAULT_RATE_CONVENTION: RateConvention = "zero_inertial"

# M3's D8 (docs/plans/pr6-trim.md), now on the three solved residuals only (M7 plan Q7, N-03).
SOLVED_RESIDUAL_BOUND = 1e-6

# The 6-vector [v_dot_b, omega_dot_b]: which components the solve zeroes, and their names.
RESIDUAL_NAMES = ("v_dot_x", "v_dot_y", "v_dot_z", "p_dot", "q_dot", "r_dot")
SOLVED_INDICES = (0, 2, 4)
LATERAL_INDICES = (1, 3, 5)

# The Earth models whose position representation trim knows (ADR-0003): geodetic, or flat NED.
FLAT_EARTH = "plugins/physical/earth/flat"
GEODETIC_EARTHS = ("plugins/physical/earth/round", "plugins/physical/earth/wgs84_rotating")

ELEVATOR_INPUT = "controls.elevator"
THROTTLE_INPUT = "controls.throttle"


@dataclass(frozen=True)
class TrimResult:
    """The solved trim point. ``state`` is the length-STATE_SIZE rigid-body state the core builds
    from the emitted initial state; ``residual`` is the 6-vector [v_dot_b, omega_dot_b] (D8),
    recomputed there. The solved three are bounded by the solve itself (``SOLVED_RESIDUAL_BOUND``);
    the lateral three are reported, never bounded (module docstring). ``alpha_rad`` is the pitch
    (theta = alpha in level flight). ``controls`` holds every registered input as the residual saw
    it: the two solved (``solved_inputs``: elevator, throttle), and the rest at the config's initial
    values."""

    state: np.ndarray
    elevator_rad: float
    throttle: float
    alpha_rad: float
    residual: np.ndarray
    scipy_result: scipy.optimize.OptimizeResult
    heading_rad: float
    rate_convention: RateConvention
    controls: dict[str, float]
    solved_inputs: tuple[str, str]

    @property
    def solved_residual(self) -> np.ndarray:
        """[v_dot_x, v_dot_z, q_dot]: what the solve zeroes."""
        return self.residual[list(SOLVED_INDICES)]

    @property
    def lateral_residual(self) -> np.ndarray:
        """[v_dot_y, p_dot, r_dot]: reported, never bounded."""
        return self.residual[list(LATERAL_INDICES)]


def _read_yaml_dict(config_path: str | os.PathLike[str]) -> dict[str, Any]:
    """The config as a dict that reads as the core reads the file (``astra._yaml_route``); a
    leaf PyYAML reads differently and cannot be repaired is the core's own ``ConfigError``."""
    with open(config_path, encoding="utf-8") as f:
        text = f.read()
    return cast(dict[str, Any], _yaml_route.load(text, str(config_path), error=_core.ConfigError))


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


def altitude_sign(data: dict[str, Any]) -> float:
    """+1 when ``p_n[2]`` is the altitude (a geodetic Earth model), -1 when it is NED down (flat).
    Refuses any other Earth model, naming ``physical.earth``."""
    earth = _earth_plugin_path(data)
    if earth == FLAT_EARTH:
        return -1.0
    if earth in GEODETIC_EARTHS:
        return 1.0
    known = ", ".join((FLAT_EARTH, *GEODETIC_EARTHS))
    raise ValueError(
        f"trim_straight_and_level: 'physical.earth' is {earth!r}, whose position representation "
        f"astra.trim does not know (it knows {known})"
    )


def _load_still_air_config(  # pyright: ignore[reportUnusedFunction] -- astra.linearize, astra.cli
    config_path: str | os.PathLike[str],
) -> _core.Config:
    """Loads `config_path` as YAML and drops `physical.wind` if present -- still air only (PR 6's
    own scope; see the module docstring) -- and the top-level `log` block (M5 PR 4, ADR-0025: an
    analysis Simulation must not create, or finalize, the run's own log file, whose path a later
    run then finds taken). Every other key is passed through unchanged, so the config file itself
    stays the single source of truth for models/coefficients/initial state. `astra.linearize` and
    `astra.lockstep.trim_for` build through this too."""
    return _still_air_config(_read_yaml_dict(config_path), config_path)


def _still_air_config(data: dict[str, Any], config_path: str | os.PathLike[str]) -> _core.Config:
    """`_load_still_air_config` on a dict already read (it drops keys from `data`)."""
    data.pop("log", None)
    physical = data.get("physical")
    if isinstance(physical, dict):
        cast(dict[str, Any], physical).pop("wind", None)
    return _core.Config.from_dict(data, origin=str(config_path))


def _quaternion_from_euler(psi_rad: float, theta_rad: float, phi_rad: float) -> np.ndarray:
    """S&L Example 1.8-1 Eq. (1), p. 52, ported line for line from `core/include/astra/util/
    frames.hpp::quaternion_from_euler` (half angles a = phi/2, b = theta/2, c = psi/2) -- checked
    against Simulation.from_config's own initial.state.euler_nb construction by
    tests/python/test_linearize.py::test_quaternion_from_euler_matches_the_real_construction. Used
    inside the solve only; the returned state is the core's own (``_canonical_state``)."""
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


def _state_from_alpha(
    p_n: np.ndarray, true_airspeed_mps: float, alpha_rad: float, heading_rad: float = 0.0
) -> np.ndarray:
    """v_b = [V cos(alpha), 0, V sin(alpha)] (beta = 0, level, phi = 0); q_nb from euler_nb =
    [heading, alpha, 0] through ``_quaternion_from_euler``, which at heading 0 is [cos(alpha/2), 0,
    sin(alpha/2), 0] bit for bit -- proved against the real binding by
    `tests/python/test_trim.py::test_state_from_alpha_matches_the_real_quaternion_construction`.
    omega_b = 0 here; the rate convention sets it (``_with_rate``)."""
    state = np.zeros(_core.STATE_SIZE)
    state[_core.POSITION : _core.POSITION + _core.POSITION_SIZE] = p_n
    state[_core.VELOCITY : _core.VELOCITY + _core.VELOCITY_SIZE] = [
        true_airspeed_mps * np.cos(alpha_rad),
        0.0,
        true_airspeed_mps * np.sin(alpha_rad),
    ]
    state[_core.QUATERNION : _core.QUATERNION + _core.QUATERNION_SIZE] = _quaternion_from_euler(
        heading_rad, alpha_rad, 0.0
    )
    return state


def _attitude_rate(sim: _core.Simulation, state: np.ndarray) -> np.ndarray:
    """The ``q_nb`` slice of ``derivative(state)``."""
    dq = sim.derivative(state)
    return np.array(dq[_core.QUATERNION : _core.QUATERNION + _core.QUATERNION_SIZE])


def _n_frame_rate(sim: _core.Simulation, state: np.ndarray) -> np.ndarray:
    """omega_in^b at ``state``'s position, velocity and attitude, as the core's own kinematics form
    it: q_dot_nb = J (omega_b - omega_in^b) with J = Xi(q) / 2 (4x3, orthonormal columns up to the
    1/2), so omega_in^b = -lstsq(J, q_dot(0)). J is read off the core at the three unit rates."""
    rate = slice(_core.ANGULAR_RATE, _core.ANGULAR_RATE + _core.ANGULAR_RATE_SIZE)
    still = state.copy()
    still[rate] = 0.0
    at_rest = _attitude_rate(sim, still)
    jacobian = np.empty((_core.QUATERNION_SIZE, _core.ANGULAR_RATE_SIZE))
    for i in range(_core.ANGULAR_RATE_SIZE):
        unit = still.copy()
        unit[_core.ANGULAR_RATE + i] = 1.0
        jacobian[:, i] = _attitude_rate(sim, unit) - at_rest
    return np.linalg.lstsq(jacobian, -at_rest, rcond=None)[0]


def _with_rate(
    sim: _core.Simulation, state: np.ndarray, rate_convention: RateConvention
) -> np.ndarray:
    """``state`` with omega_b set by the rate convention (module docstring)."""
    if rate_convention == "n_frame":
        state = state.copy()
        state[_core.ANGULAR_RATE : _core.ANGULAR_RATE + _core.ANGULAR_RATE_SIZE] = _n_frame_rate(
            sim, state
        )
    return state


def _residual6(sim: _core.Simulation, state: np.ndarray) -> np.ndarray:
    """D8: the 6-vector [v_dot_b, omega_dot_b] from `derivative(state)`'s named rigid-body slices
    (ADR-0021 R4) -- p_dot_n/q_dot_nb excluded."""
    dq = sim.derivative(state)
    v_dot = dq[_core.VELOCITY : _core.VELOCITY + _core.VELOCITY_SIZE]
    omega_dot = dq[_core.ANGULAR_RATE : _core.ANGULAR_RATE + _core.ANGULAR_RATE_SIZE]
    return np.concatenate([v_dot, omega_dot])


def _canonical_state(
    data: dict[str, Any],
    config_path: str | os.PathLike[str],
    state: np.ndarray,
    euler_nb: list[float],
) -> np.ndarray:
    """The rigid-body state the core builds from ``state``'s p_n, v_b and omega_b with ``euler_nb``
    in place of any q_nb: what a config rebuilt from ``_runkit.trim_fragment`` starts from. ``data``
    is the still-air dict (no ``log``, no ``physical.wind``); it is copied, never changed."""
    copy = cast(dict[str, Any], _deep(data))
    initial = cast(dict[str, Any], copy.setdefault("initial", {}))
    values = cast(dict[str, Any], initial.setdefault("state", {}))
    values.pop("q_nb", None)
    values["p_n"] = [float(v) for v in state[_core.POSITION : _core.POSITION + _core.POSITION_SIZE]]
    values["v_b"] = [float(v) for v in state[_core.VELOCITY : _core.VELOCITY + _core.VELOCITY_SIZE]]
    values["euler_nb"] = euler_nb
    values["omega_b"] = [
        float(v) for v in state[_core.ANGULAR_RATE : _core.ANGULAR_RATE + _core.ANGULAR_RATE_SIZE]
    ]
    sim = _core.Simulation.from_config(
        _core.Config.from_dict(copy, origin=str(config_path)), _core.default_plugin_dir()
    )
    return np.array(sim.vehicles[0].state[: _core.STATE_SIZE])


def _deep(value: Any) -> Any:
    """A copy of a YAML tree's maps and lists (the leaves are immutable)."""
    if isinstance(value, dict):
        return {k: _deep(v) for k, v in cast(dict[Any, Any], value).items()}
    if isinstance(value, list):
        return [_deep(v) for v in cast(list[Any], value)]
    return value


def trim_straight_and_level(
    config_path: str | os.PathLike[str],
    true_airspeed_mps: float,
    altitude_m: float,
    *,
    heading_rad: float = 0.0,
    rate_convention: RateConvention = DEFAULT_RATE_CONVENTION,
    elevator_input: str = ELEVATOR_INPUT,
    throttle_input: str = THROTTLE_INPUT,
    elevator_guess_rad: float = 0.0,
    throttle_guess: float = 0.5,
    alpha_guess_rad: float = 0.0,
    xtol: float = 1e-12,
) -> TrimResult:
    """Solves level trim (phi = beta = 0, still air, the other inputs at the config's values) for
    `config_path` at the given true airspeed, altitude and heading, under `rate_convention`.

    Raises `ValueError` (message starting ``trim_straight_and_level: ``) for a setup it refuses: an
    unknown rate convention, an Earth model whose representation it does not know, or a solved
    input the config's models do not register. Raises `RuntimeError` naming scipy's own message and
    the last residual when `scipy.optimize.root` does not report success, and naming the worst
    solved component when one exceeds ``SOLVED_RESIDUAL_BOUND``. (`hybr`'s default `xtol` lands
    near a typical 1e-6 residual for this aircraft, docs/plans/pr6-trim.md; the default here is
    1e-12.)
    """
    if rate_convention not in RATE_CONVENTIONS:
        raise ValueError(
            f"trim_straight_and_level: rate_convention {rate_convention!r} is not one of "
            f"{', '.join(RATE_CONVENTIONS)}"
        )
    data = _read_yaml_dict(config_path)  # read once: _yaml_route checks it (M6 PR 11)
    sign = altitude_sign(data)
    config = _still_air_config(data, config_path)  # drops data's log and wind
    sim = _core.Simulation.from_config(config, _core.default_plugin_dir())
    names = sim.input_names()
    for name in (elevator_input, throttle_input):
        if name not in names:
            raise ValueError(
                f"trim_straight_and_level: input {name!r} is not registered by the config's "
                f"models (registered: {', '.join(names) or 'none'})"
            )
    controls = {name: config.number(f"vehicle.initial_controls.{name}") for name in names}
    p_n = np.array(sim.vehicles[0].state[_core.POSITION : _core.POSITION + _core.POSITION_SIZE])
    p_n[-1] = sign * altitude_m

    def trial(alpha_rad: float) -> np.ndarray:
        state = _state_from_alpha(p_n, true_airspeed_mps, alpha_rad, heading_rad)
        return _with_rate(sim, state, rate_convention)

    def residual3(x: np.ndarray) -> np.ndarray:
        elevator_rad, throttle, alpha_rad = x
        sim.write_input(elevator_input, elevator_rad)
        sim.write_input(throttle_input, throttle)
        residual = _residual6(sim, trial(alpha_rad))
        return residual[list(SOLVED_INDICES)]

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
    controls[elevator_input] = elevator_rad
    controls[throttle_input] = throttle
    sim.write_input(elevator_input, elevator_rad)
    sim.write_input(throttle_input, throttle)
    # The core's own state from the emitted euler_nb, then the convention's rate at exactly it.
    euler_nb = [float(heading_rad), alpha_rad, 0.0]
    state = _canonical_state(data, config_path, trial(alpha_rad), euler_nb)
    state = _with_rate(sim, state, rate_convention)
    residual = _residual6(sim, state)

    if not success:
        raise RuntimeError(
            f"trim_straight_and_level did not converge: {message} "
            f"(max|residual| = {np.max(np.abs(residual)):.3e})"
        )
    solved = np.abs(residual[list(SOLVED_INDICES)])
    worst = int(np.argmax(solved))
    if solved[worst] > SOLVED_RESIDUAL_BOUND:
        name = RESIDUAL_NAMES[SOLVED_INDICES[worst]]
        raise RuntimeError(
            f"trim_straight_and_level: solved residual {name} = "
            f"{float(residual[SOLVED_INDICES[worst]])!r} exceeds {SOLVED_RESIDUAL_BOUND!r}"
        )

    return TrimResult(
        state=state,
        elevator_rad=elevator_rad,
        throttle=throttle,
        alpha_rad=alpha_rad,
        residual=residual,
        scipy_result=sol,
        heading_rad=float(heading_rad),
        rate_convention=rate_convention,
        controls=controls,
        solved_inputs=(elevator_input, throttle_input),
    )
