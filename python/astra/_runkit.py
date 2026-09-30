"""astra._runkit: the pieces ``astra.scenario`` and ``astra.montecarlo`` share (M5 PR 7).
Private: each tool documents the rules it takes from here in its own docstring.

- Schema validators (``closed_map``, ``number``, ``text``, ``frames_of``, ``trim_setpoint``)
  that raise the caller's own error class, so each tool keeps its error type and message.
- Leaf rules (``numeric_leaf``, ``set_numeric_leaf``): an existing numeric leaf only, reached by
  a dotted path whose integer segments index lists; never a new key.
- ``TRIM_OWNED``: the leaves trim sets or assumes (``astra.scenario``'s docstring says why).
- ``apply_trim``: trims a copy of a config without its ``log`` block and writes the result back.
- ``run_logged``: builds, drives and closes a ``Simulation`` whose log is at a given path, reads
  the log back strictly and accepts it, all while the ``Simulation`` is alive; if anything after
  the build raises, the log is discarded (``discard_on_failure``).
- ``discard_on_failure``: the one cleanup rule of ``astra.scenario``, ``astra.montecarlo`` and
  ``astra.log.replay`` (ADR-0027 item 8): a tool deletes only its own run's log file, only while
  handling a failure, and only through ``Simulation.discard_log()`` while that ``Simulation`` is
  alive. It never deletes by name, never a file whose construction was refused, and never a file
  that collided with or replaced its own; once the ``Simulation`` is gone it deletes nothing.
- ``run_length_failures``: a consumer assertion on ``frames_run`` (kept from M5 as a check of the
  driver, beside ADR-0027's finalization): ``duration / dt`` frames without a halt, the halt's own
  frame with one.
"""

from __future__ import annotations

import dataclasses
import math
import re
import tempfile
from collections.abc import Callable
from fractions import Fraction
from pathlib import Path
from typing import Any, cast

import numpy as np
import yaml

import astra.trim
from astra import _core, log

# With trim set, the key prefixes trim sets or assumes, and why (astra.scenario's docstring). A key
# matches a prefix when it equals it or continues it with "." (an element of a list).
TRIM_OWNED = (
    ("initial.state.p_n.2", "trim sets the altitude from trim.altitude"),
    ("initial.state.v_b", "trim sets v_b from trim.true_airspeed and its alpha"),
    ("initial.state.euler_nb", "trim sets euler_nb to [0, alpha, 0]"),
    ("initial.state.q_nb", "trim replaces q_nb with euler_nb"),
    ("initial.state.omega_b", "trim sets omega_b to zero"),
    ("vehicle.initial_controls.controls.elevator", "trim sets the elevator it solves for"),
    ("vehicle.initial_controls.controls.throttle", "trim sets the throttle it solves for"),
    ("vehicle.initial_controls.controls.aileron", "trim assumes aileron = 0 and never writes it"),
    ("vehicle.initial_controls.controls.rudder", "trim assumes rudder = 0 and never writes it"),
)

# D8's trim residual bound (docs/plans/pr6-trim.md), the bound cruise.yaml's trim was held to.
TRIM_RESIDUAL_BOUND = 1e-6

# The temporary directory apply_trim writes its still-air copy into. The copy's path is its Config
# origin, so it can reach an error message; astra.montecarlo replaces it with a placeholder.
TRIM_DIR_PREFIX = "astra_scenario_trim_"

_INDEX = re.compile(r"0|[1-9][0-9]*")

ErrorType = Callable[[str], Exception]


class LeafError(Exception):
    """A dotted path does not name an existing numeric leaf; the message is the reason alone."""


@dataclasses.dataclass(frozen=True)
class TrimSetpoint:
    true_airspeed: float
    altitude: float


# --- schema validators --------------------------------------------------------------------------


def closed_map(
    path: Path,
    value: Any,
    where: str,
    *,
    required: set[str],
    optional: set[str] | None,
    error: ErrorType,
) -> dict[str, Any]:
    """``value`` as a map with string keys: every ``required`` key present, and (unless
    ``optional`` is ``None``) no key outside ``optional``."""
    if not isinstance(value, dict):
        raise error(f"{path}: {where}: must be a map")
    mapping = cast(dict[Any, Any], value)
    for key in mapping:
        if not isinstance(key, str):
            raise error(f"{path}: {where}: key {key!r} is not a string")
        if optional is not None and key not in optional:
            raise error(
                f"{path}: {where}: unknown key {key!r} (allowed: {', '.join(sorted(optional))})"
            )
    missing = sorted(required - set(mapping))
    if missing:
        raise error(f"{path}: {where}: missing {', '.join(missing)}")
    return cast(dict[str, Any], mapping)


def text(path: Path, value: Any, where: str, *, error: ErrorType) -> str:
    if not isinstance(value, str) or not value:
        raise error(f"{path}: {where}: must be a non-empty string")
    return value


def number(path: Path, value: Any, where: str, *, error: ErrorType) -> float:
    if isinstance(value, bool) or not isinstance(value, int | float):
        hint = ""
        if isinstance(value, str) and re.fullmatch(r"[-+]?[0-9.]+[eE][0-9]+", value):
            hint = " (YAML 1.1 reads an exponent without a sign as a string: write 1.0e+3)"
        raise error(f"{path}: {where}: must be a number, got {value!r}{hint}")
    result = float(value)
    if not math.isfinite(result):
        raise error(f"{path}: {where}: must be finite, got {value!r}")
    return result


def frames_of(path: Path, value: Any, where: str, dt: float, *, error: ErrorType) -> int:
    """The frame count of the time ``value``: an exact multiple of ``dt`` in the decimals the
    author and the config wrote (``Fraction(repr(x)) / Fraction(repr(dt))`` is an integer)."""
    seconds = number(path, value, where, error=error)
    ratio = Fraction(repr(seconds)) / Fraction(repr(dt))
    if ratio.denominator != 1:
        raise error(f"{path}: {where}: {seconds!r} s is not an exact multiple of dt = {dt!r} s")
    return int(ratio)


def trim_setpoint(path: Path, value: Any, *, error: ErrorType) -> TrimSetpoint | None:
    """``{true_airspeed, altitude}`` for ``astra.trim.trim_straight_and_level``, or ``None``."""
    if value is None:
        return None
    keys = {"true_airspeed", "altitude"}
    spec = closed_map(path, value, "trim", required=keys, optional=keys, error=error)
    return TrimSetpoint(
        number(path, spec["true_airspeed"], "trim.true_airspeed", error=error),
        number(path, spec["altitude"], "trim.altitude", error=error),
    )


# --- leaf rules ---------------------------------------------------------------------------------


def _slot(config: dict[str, Any], key: str) -> tuple[Any, Any]:
    """The container and the key or index of the existing numeric leaf ``key`` names."""
    node: Any = config
    segments = key.split(".")
    for i, segment in enumerate(segments):
        leaf = i == len(segments) - 1
        if isinstance(node, dict):
            mapping = cast(dict[str, Any], node)
            if segment not in mapping:
                raise LeafError(f"no key {segment!r} in the config")
            if leaf:
                _require_number(mapping[segment])
                return mapping, segment
            node = mapping[segment]
        elif isinstance(node, list):
            items = cast(list[Any], node)
            if _INDEX.fullmatch(segment) is None or int(segment) >= len(items):
                raise LeafError(f"{segment!r} is not an index of a {len(items)}-item list")
            if leaf:
                _require_number(items[int(segment)])
                return items, int(segment)
            node = items[int(segment)]
        else:
            raise LeafError(f"{segment!r} is below a scalar")
    raise LeafError("the path is empty")  # unreachable: str.split always yields a segment


def _require_number(old: Any) -> None:
    if isinstance(old, bool) or not isinstance(old, int | float):
        raise LeafError(f"the config's value {old!r} is not a number")


def numeric_leaf(config: dict[str, Any], key: str) -> int | float:
    """The value of the existing numeric leaf ``key`` (``LeafError`` otherwise)."""
    container, slot = _slot(config, key)
    return cast(int | float, container[slot])


def set_numeric_leaf(config: dict[str, Any], key: str, value: int | float) -> None:
    """Replaces the existing numeric leaf ``key`` with ``value`` (``LeafError`` otherwise)."""
    container, slot = _slot(config, key)
    container[slot] = value


def trim_owned_reason(key: str) -> str | None:
    """Why trim owns ``key`` (``TRIM_OWNED``), or ``None`` when it does not."""
    for prefix, reason in TRIM_OWNED:
        if key == prefix or key.startswith(f"{prefix}."):
            return reason
    return None


# --- trim and run -------------------------------------------------------------------------------


def apply_trim(
    data: dict[str, Any], setpoint: TrimSetpoint, *, origin: Path, config_name: str
) -> None:
    """Trims a copy of ``data`` (which has no ``log`` block yet) and writes the result into
    ``data``'s initial state and controls. Raises ``RuntimeError`` when the residual misses D8's
    bound."""
    if "log" in data:
        raise RuntimeError(f"{origin}: the trim copy must not carry the run's log block")
    with tempfile.TemporaryDirectory(prefix=TRIM_DIR_PREFIX) as directory:
        still = Path(directory) / config_name
        still.write_text(yaml.safe_dump(data, sort_keys=False), encoding="utf-8")
        result = astra.trim.trim_straight_and_level(
            still, setpoint.true_airspeed, setpoint.altitude
        )
    worst = float(np.max(np.abs(result.residual)))
    if worst > TRIM_RESIDUAL_BOUND:
        raise RuntimeError(
            f"{origin}: trim residual {worst:.3e} exceeds D8's {TRIM_RESIDUAL_BOUND:g}"
        )
    state = [float(v) for v in result.state]
    initial = cast(dict[str, Any], data.setdefault("initial", {}))
    values = cast(dict[str, Any], initial.setdefault("state", {}))
    values.pop("q_nb", None)  # the trim fixes the attitude; euler_nb below states it
    values["p_n"] = state[_core.POSITION : _core.POSITION + _core.POSITION_SIZE]
    values["v_b"] = state[_core.VELOCITY : _core.VELOCITY + _core.VELOCITY_SIZE]
    values["euler_nb"] = [0.0, result.alpha_rad, 0.0]  # [psi, theta, phi]; theta = alpha
    values["omega_b"] = [0.0, 0.0, 0.0]
    vehicle = cast(dict[str, Any], data.setdefault("vehicle", {}))
    controls = cast(
        dict[str, Any],
        cast(dict[str, Any], vehicle.setdefault("initial_controls", {})).setdefault("controls", {}),
    )
    controls["elevator"] = result.elevator_rad  # astra.trim's own control names (its D10)
    controls["throttle"] = result.throttle


def discard_on_failure(sim: _core.Simulation, error: BaseException) -> None:
    """ADR-0027 item 8's tool rule, for a failure being handled while ``sim`` is alive: discards
    the run's own log (the ``.partial``, or ``log.path`` once published) through
    ``Simulation.discard_log()``, which deletes only after checking the name is still this run's
    file. When it deletes nothing (``MISSING``, ``MISMATCH`` or ``FAILED``), every file is left as
    it is and ``error`` carries a note naming the result."""
    result = sim.discard_log()
    if not result.deleted:
        error.add_note(f"astra: discard_log() deleted nothing: {result!r}")


def run_logged(
    data: dict[str, Any],
    log_path: Path,
    *,
    origin: str,
    plugin_dir: str | Path | None,
    drive: Callable[[_core.Simulation], None],
    close: Callable[[_core.Simulation], None],
    accept: Callable[[log.RunLog], None] | None = None,
) -> log.RunLog:
    """Builds a ``Simulation`` from ``data`` plus a ``log`` block naming ``log_path`` (which must
    not exist), calls ``drive`` and then ``close`` (which must call ``close_log()``), reads the log
    back strictly and passes it to ``accept`` (which raises to reject it), all while the
    ``Simulation`` is still alive, so a missing close shows as a refused log (nothing is published
    without ``close_log()``). If ``from_config`` raises, the core has already deleted the
    ``.partial`` it created; if anything after it raises, the log is discarded
    (``discard_on_failure``) and the error re-raised."""
    if log_path.exists():
        raise FileExistsError(f"{log_path}: the run's log path already exists")
    if "log" in data:
        raise RuntimeError(f"{origin}: the config already has a log block")
    config = _core.Config.from_dict({**data, "log": {"path": str(log_path)}}, origin=origin)
    directory = _core.default_plugin_dir() if plugin_dir is None else str(plugin_dir)
    sim = _core.Simulation.from_config(config, directory)
    try:
        drive(sim)
        close(sim)
        run_log = log.load(log_path)  # sim is still alive: nothing has been dropped
        if accept is not None:
            accept(run_log)
    except BaseException as error:
        discard_on_failure(sim, error)
        raise
    return run_log


# --- the consumer assertion ---------------------------------------------------------------------


def duration_failure(frames_run: int | None, frames: int) -> str | None:
    if frames_run == frames:
        return None
    return f"frames_run is {frames_run}, not duration / dt = {frames} frames"


def halt_frame_failure(frames_run: int | None, halt: log.Halt) -> str | None:
    if frames_run == halt.frame:
        return None
    return f"frames_run is {frames_run}, not the halt's frame {halt.frame}"


def run_length_failures(run_log: log.RunLog, frames: int) -> list[str]:
    """A run asked for ``frames`` frames: without a halt it ran exactly that many; with one, it ran
    up to the halt's own frame, which is below ``frames``."""
    halt = run_log.halt
    if halt is None:
        failure = duration_failure(run_log.frames_run, frames)
        return [] if failure is None else [failure]
    failures: list[str] = []
    failure = halt_frame_failure(run_log.frames_run, halt)
    if failure is not None:
        failures.append(failure)
    if not 0 <= halt.frame < frames:
        failures.append(f"the halt's frame {halt.frame} is outside [0, {frames})")
    return failures
