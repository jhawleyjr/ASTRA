"""astra.scenario: the scenario harness (M5 PR 6; ADR-0025, docs/plans/m5-tooling.md Q14 to Q16,
R11 to R13).

A scenario is one YAML file (``tests/scenarios/*.yaml``, one pytest item each, through that
directory's ``conftest.py``). It names a config, optionally changes existing numeric leaves of it
and trims it, runs it open loop for a duration with input writes at given times, and checks
signals in the run's own HDF5 log. Every value is SI (ADR-0002); no key carries a unit suffix.

Schema (Q14). Every map is closed: an unknown key at any level is an error.

- ``config``: path of the base config, relative to ``load``'s ``root``. A config with a
  ``transport`` block is refused (these scenarios are open loop, Q16); so is one with a ``log``
  block, because the harness owns the run's log.
- ``overrides``: ``{dotted.path: number}``. Integer segments index lists
  (``initial.state.p_n.2``). Only an existing numeric leaf can be changed, never a new key.
- ``trim``: ``{true_airspeed, altitude}``, for ``astra.trim.trim_straight_and_level``. It works
  on a copy of the config without the ``log`` block, so it never creates the run's file. Its state
  and its elevator and throttle replace the config's initial state and controls.
- ``duration``: seconds, greater than zero.
- ``events``: a list of ``{at, write, value}``. ``write`` is a write-seam input name
  (``Simulation.input_names()``) and ``value`` is absolute and finite. It is written through
  ``write_input`` before the step of frame ``at / dt``, the same rule ``astra.log.replay`` uses,
  so a scenario's log replays. ``0 <= at < duration``.
- ``checks``: a list of ``{signal, window, <kind>}``, where the kind is one of:
  - ``within: {target, tol}``: ``|x - target| <= tol``;
  - ``between: {lo, hi}``: ``lo <= x <= hi``;
  - ``angle_within: {target, tol}``: ``|wrap(x - target)| <= tol``, wrapped to [-pi, pi].

  ``window`` is ``{from, to}``, the half-open time interval ``[from, to)`` with
  ``0 <= from < to <= duration``, or ``final``.
- ``expect_halt``: ``{check, source, frame}``, for a scenario that must halt. ``check`` is a
  ``HealthCheck`` name other than ``NONE``, ``TRANSPORT`` or ``LOG``, and ``frame`` is the frame
  index the halt names.

Times. A time is valid only when it is an exact multiple of the config's ``dt``: the decimal
written, ``Fraction(repr(x))``, divided by ``Fraction(repr(dt))``, must be an integer. After that
check the harness works in integer frames only (ADR-0010).

What "final" means. ``window: final`` is the log's last row: frame ``frames_run - 1``, at
``t = (frames_run - 1) * dt``. Per ADR-0025, row k holds the frame-start values of frame k, so the
last row is the start of the last frame that ran. It is one frame before ``duration`` (or before
the halt). ``/final_state``, the state after the last frame, is never used by a check: it holds
state only, not derived signals or controls, and one definition should cover every signal.

Collection (``load``). Every error above is a ``ScenarioError``, which the conftest turns into a
collection error, never a skip. That covers the schema, the refused config blocks, the overrides,
the times, an unknown signal or input, and a config that does not build. ``load`` builds a
``Simulation`` from the resolved config, with no log and no trim, to learn the signal and input
names.

Run (``run``). It writes the log at the given path and calls ``close_log()`` explicitly. It keeps
the ``Simulation`` alive until ``astra.log.load`` has read the file, so a missing close shows as a
refused log, never as one the destructor finalized with ``complete = 1`` (PLAN.md's
finalization-marker open item). The log is read strictly, never with ``allow_incomplete``. If
anything raises between building the ``Simulation`` and reading its log, the log file is removed,
so an aborted run never leaves a log that looks complete.

Verification (``verify``) reads only the log and asserts:
- ``frames_run`` equals ``duration / dt``, exactly; with ``expect_halt``, it equals the halt's
  frame instead.
- There is no halt without ``expect_halt``. With it, the recorded halt's check, source and frame
  equal the expected ones.
- Every warn counter (``WARN_COUNTERS``) is zero in every logged row, and zero in ``/summary``.
  The one exception is the expected halt's own counter in ``/summary``, which must be at least 1.
  A halting detection is counted before it halts, and ADR-0020's per-stage atmosphere check counts
  once per out-of-range RK4 stage, so the halted frame can count up to four (the seed ceiling halt
  counts 3). The halted frame has no row, and with a ``halt`` action any detection in an earlier
  frame would have halted there. The rows being zero is therefore what shows that nothing was
  counted before the halt. The log's ``health.*`` names must be exactly ``HEALTH_SIGNALS``, so a
  new counter forces a deliberate update here.
- Each check holds over every row in its window, and the window has every row it asks for. A
  failure names the scenario, the check, the signal, the window, the worst value and its frame.
  A NaN is the worst value.
"""

from __future__ import annotations

import copy
import dataclasses
import math
import re
import tempfile
from fractions import Fraction
from pathlib import Path
from typing import Any, cast

import numpy as np
import yaml

import astra.trim
from astra import _core, log

# health_monitor.cpp registers these as counters; health.max_q_drift is a drift, not a count.
WARN_COUNTERS = ("health.nan", "health.quaternion", "health.domain", "health.input")
HEALTH_SIGNALS: frozenset[str] = frozenset((*WARN_COUNTERS, "health.max_q_drift"))

# The counter each haltable check increments (health_monitor.cpp). TRANSPORT and LOG have none
# and cannot be expected: a scenario has no transport, and a LOG halt describes the output.
COUNTER_OF_CHECK = {
    "NON_FINITE_STATE": "health.nan",
    "NON_FINITE_WRENCH": "health.nan",
    "NON_FINITE_ENVIRONMENT": "health.nan",
    "QUATERNION_NORM": "health.quaternion",
    "OUT_OF_DOMAIN": "health.domain",
    "NON_FINITE_INPUT": "health.input",
}

# D8's trim residual bound (docs/plans/pr6-trim.md), the bound cruise.yaml's trim was held to.
TRIM_RESIDUAL_BOUND = 1e-6

_TOP_KEYS = {"config", "overrides", "trim", "duration", "events", "checks", "expect_halt"}
_KINDS = ("within", "between", "angle_within")
_HALT_KEYS = {"check", "source", "frame"}
_INDEX = re.compile(r"0|[1-9][0-9]*")


class ScenarioError(Exception):
    """The scenario file is invalid (a collection error, never a skip)."""


class ScenarioFailedError(AssertionError):
    """A scenario ran and failed at least one assertion; the message lists every failure."""

    def __init__(self, scenario: Scenario, failures: list[str]) -> None:
        self.scenario = scenario
        self.failures = failures
        lines = "\n".join(f"  - {failure}" for failure in failures)
        super().__init__(f"scenario {scenario.name} ({scenario.path}) failed:\n{lines}")


@dataclasses.dataclass(frozen=True)
class Window:
    """``[first, stop)`` in frames, or the final row when ``stop`` is ``None``."""

    first: int
    stop: int | None


@dataclasses.dataclass(frozen=True)
class Check:
    index: int
    signal: str
    kind: str  # "within" or "angle_within": (target, tol); "between": (lo, hi)
    a: float
    b: float
    window: Window


@dataclasses.dataclass(frozen=True)
class Event:
    frame: int
    name: str
    value: float


@dataclasses.dataclass(frozen=True)
class ExpectedHalt:
    check: str
    source: str
    frame: int


@dataclasses.dataclass(frozen=True)
class TrimSetpoint:
    true_airspeed: float
    altitude: float


@dataclasses.dataclass(frozen=True, eq=False)
class Scenario:
    """A validated scenario. ``config`` is the base config with its overrides applied (no trim,
    no log); ``frames`` is ``duration / dt``; ``events`` are in frame order, file order within a
    frame."""

    name: str
    path: Path
    config_path: Path
    config: dict[str, Any]
    dt: float
    frames: int
    trim: TrimSetpoint | None
    events: tuple[Event, ...]
    checks: tuple[Check, ...]
    expect_halt: ExpectedHalt | None


@dataclasses.dataclass(frozen=True)
class ScenarioResult:
    """The run's loaded log and every failed assertion (empty when the scenario passed)."""

    log: log.RunLog
    failures: list[str]


# --- load ---------------------------------------------------------------------------------------


def load(path: str | Path, *, root: str | Path | None = None) -> Scenario:
    """Reads and validates the scenario at ``path`` (module docstring). ``config`` is resolved
    against ``root`` (default: the current directory). Raises ``ScenarioError`` naming the file
    and the key for anything invalid."""
    path = Path(path)
    base = Path.cwd() if root is None else Path(root)
    try:
        raw: Any = yaml.safe_load(path.read_text(encoding="utf-8"))
    except (OSError, yaml.YAMLError) as error:
        raise ScenarioError(f"{path}: cannot read the scenario ({error})") from error
    top = _closed(path, raw, "the scenario", required={"config", "duration"}, optional=_TOP_KEYS)

    config_path = base / _text(path, top["config"], "config")
    config = _base_config(path, config_path)
    overrides = _closed(path, top.get("overrides", {}), "overrides", required=set(), optional=None)
    for key, value in overrides.items():
        _override(path, config, key, value)

    try:
        sim = _core.Simulation.from_config(
            _core.Config.from_dict(config, origin=str(config_path)), _core.default_plugin_dir()
        )
    except _core.Error as error:
        raise ScenarioError(f"{path}: the config does not build: {error}") from error
    dt = sim.clock.dt
    signals = {name for name, _value, _unit, _divisor in sim.registry.signals}
    inputs = set(sim.input_names())

    frames = _frames(path, top["duration"], "duration", dt)
    if frames <= 0:
        raise ScenarioError(f"{path}: duration: must be greater than zero")
    expect_halt = _expect_halt(path, top.get("expect_halt"), frames)
    last_frame = frames if expect_halt is None else expect_halt.frame
    trim = _trim(path, top.get("trim"))
    events = _events(path, top.get("events", []), dt, frames, inputs, expect_halt)
    checks = _checks(path, top.get("checks", []), dt, frames, last_frame, signals)
    return Scenario(
        name=path.stem,
        path=path,
        config_path=config_path,
        config=config,
        dt=dt,
        frames=frames,
        trim=trim,
        events=events,
        checks=checks,
        expect_halt=expect_halt,
    )


def _closed(
    path: Path, value: Any, where: str, *, required: set[str], optional: set[str] | None
) -> dict[str, Any]:
    """``value`` as a map with string keys: every ``required`` key present, and (unless
    ``optional`` is ``None``) no key outside ``optional``."""
    if not isinstance(value, dict):
        raise ScenarioError(f"{path}: {where}: must be a map")
    mapping = cast(dict[Any, Any], value)
    for key in mapping:
        if not isinstance(key, str):
            raise ScenarioError(f"{path}: {where}: key {key!r} is not a string")
        if optional is not None and key not in optional:
            raise ScenarioError(
                f"{path}: {where}: unknown key {key!r} (allowed: {', '.join(sorted(optional))})"
            )
    missing = sorted(required - set(mapping))
    if missing:
        raise ScenarioError(f"{path}: {where}: missing {', '.join(missing)}")
    return cast(dict[str, Any], mapping)


def _text(path: Path, value: Any, where: str) -> str:
    if not isinstance(value, str) or not value:
        raise ScenarioError(f"{path}: {where}: must be a non-empty string")
    return value


def _number(path: Path, value: Any, where: str) -> float:
    if isinstance(value, bool) or not isinstance(value, int | float):
        raise ScenarioError(f"{path}: {where}: must be a number, got {value!r}")
    number = float(value)
    if not math.isfinite(number):
        raise ScenarioError(f"{path}: {where}: must be finite, got {value!r}")
    return number


def _frames(path: Path, value: Any, where: str, dt: float) -> int:
    """The frame count of the time ``value``: an exact multiple of ``dt`` in the decimals the
    author and the config wrote (module docstring, Times)."""
    seconds = _number(path, value, where)
    ratio = Fraction(repr(seconds)) / Fraction(repr(dt))
    if ratio.denominator != 1:
        raise ScenarioError(
            f"{path}: {where}: {seconds!r} s is not an exact multiple of dt = {dt!r} s"
        )
    return int(ratio)


def _base_config(path: Path, config_path: Path) -> dict[str, Any]:
    try:
        data: Any = yaml.safe_load(config_path.read_text(encoding="utf-8"))
    except (OSError, yaml.YAMLError) as error:
        raise ScenarioError(f"{path}: config: cannot read {config_path} ({error})") from error
    config = _closed(path, data, f"config {config_path}", required=set(), optional=None)
    if "transport" in config:
        raise ScenarioError(
            f"{path}: config: {config_path} has a transport block; scenarios are open loop "
            "(docs/plans/m5-tooling.md Q16)"
        )
    if "log" in config:
        raise ScenarioError(
            f"{path}: config: {config_path} has a log block; the harness writes the run's log "
            "itself"
        )
    return config


def _override(path: Path, config: dict[str, Any], key: str, value: Any) -> None:
    where = f"overrides.{key}"
    _number(path, value, where)
    node: Any = config
    segments = key.split(".")
    for i, segment in enumerate(segments):
        leaf = i == len(segments) - 1
        if isinstance(node, dict):
            mapping = cast(dict[str, Any], node)
            if segment not in mapping:
                raise ScenarioError(f"{path}: {where}: no key {segment!r} in the config")
            if leaf:
                _numeric_leaf(path, where, mapping[segment])
                mapping[segment] = value
                return
            node = mapping[segment]
        elif isinstance(node, list):
            items = cast(list[Any], node)
            if _INDEX.fullmatch(segment) is None or int(segment) >= len(items):
                raise ScenarioError(
                    f"{path}: {where}: {segment!r} is not an index of a {len(items)}-item list"
                )
            if leaf:
                _numeric_leaf(path, where, items[int(segment)])
                items[int(segment)] = value
                return
            node = items[int(segment)]
        else:
            raise ScenarioError(f"{path}: {where}: {segment!r} is below a scalar")


def _numeric_leaf(path: Path, where: str, old: Any) -> None:
    if isinstance(old, bool) or not isinstance(old, int | float):
        raise ScenarioError(f"{path}: {where}: the config's value {old!r} is not a number")


def _expect_halt(path: Path, value: Any, frames: int) -> ExpectedHalt | None:
    if value is None:
        return None
    where = "expect_halt"
    spec = _closed(path, value, where, required=_HALT_KEYS, optional=_HALT_KEYS)
    check = _text(path, spec["check"], f"{where}.check")
    if check not in COUNTER_OF_CHECK:
        raise ScenarioError(
            f"{path}: {where}.check: {check!r} is not one of {', '.join(COUNTER_OF_CHECK)}"
        )
    frame = spec["frame"]
    if isinstance(frame, bool) or not isinstance(frame, int) or not 0 <= frame < frames:
        raise ScenarioError(
            f"{path}: {where}.frame: must be an integer frame in [0, {frames}), got {frame!r}"
        )
    return ExpectedHalt(check, _text(path, spec["source"], f"{where}.source"), frame)


def _trim(path: Path, value: Any) -> TrimSetpoint | None:
    if value is None:
        return None
    keys = {"true_airspeed", "altitude"}
    spec = _closed(path, value, "trim", required=keys, optional=keys)
    return TrimSetpoint(
        _number(path, spec["true_airspeed"], "trim.true_airspeed"),
        _number(path, spec["altitude"], "trim.altitude"),
    )


def _events(
    path: Path,
    value: Any,
    dt: float,
    frames: int,
    inputs: set[str],
    expect_halt: ExpectedHalt | None,
) -> tuple[Event, ...]:
    if not isinstance(value, list):
        raise ScenarioError(f"{path}: events: must be a list")
    events: list[Event] = []
    keys = {"at", "write", "value"}
    for i, item in enumerate(cast(list[Any], value)):
        where = f"events[{i}]"
        spec = _closed(path, item, where, required=keys, optional=keys)
        frame = _frames(path, spec["at"], f"{where}.at", dt)
        if not 0 <= frame < frames:
            raise ScenarioError(f"{path}: {where}.at: must be in [0, duration)")
        if expect_halt is not None and frame > expect_halt.frame:
            raise ScenarioError(
                f"{path}: {where}.at: frame {frame} is after expect_halt.frame "
                f"{expect_halt.frame}, so it could never be written"
            )
        name = _text(path, spec["write"], f"{where}.write")
        if name not in inputs:
            raise ScenarioError(
                f"{path}: {where}.write: {name!r} is not an input (inputs: "
                f"{', '.join(sorted(inputs))})"
            )
        events.append(Event(frame, name, _number(path, spec["value"], f"{where}.value")))
    return tuple(sorted(events, key=lambda event: event.frame))  # stable: file order per frame


def _checks(
    path: Path, value: Any, dt: float, frames: int, last_frame: int, signals: set[str]
) -> tuple[Check, ...]:
    if not isinstance(value, list):
        raise ScenarioError(f"{path}: checks: must be a list")
    checks: list[Check] = []
    for i, item in enumerate(cast(list[Any], value)):
        where = f"checks[{i}]"
        spec = _closed(
            path, item, where, required={"signal", "window"}, optional={"signal", "window", *_KINDS}
        )
        signal = _text(path, spec["signal"], f"{where}.signal")
        if signal not in signals:
            raise ScenarioError(f"{path}: {where}.signal: {signal!r} is not a registered signal")
        kinds = [kind for kind in _KINDS if kind in spec]
        if len(kinds) != 1:
            raise ScenarioError(f"{path}: {where}: needs exactly one of {', '.join(_KINDS)}")
        kind = kinds[0]
        a, b = _bounds(path, spec[kind], f"{where}.{kind}", kind)
        window = _window(path, spec["window"], f"{where}.window", dt, frames, last_frame)
        checks.append(Check(i, signal, kind, a, b, window))
    return tuple(checks)


def _bounds(path: Path, value: Any, where: str, kind: str) -> tuple[float, float]:
    if kind == "between":
        spec = _closed(path, value, where, required={"lo", "hi"}, optional={"lo", "hi"})
        lo = _number(path, spec["lo"], f"{where}.lo")
        hi = _number(path, spec["hi"], f"{where}.hi")
        if lo > hi:
            raise ScenarioError(f"{path}: {where}: lo {lo!r} is above hi {hi!r}")
        return lo, hi
    spec = _closed(path, value, where, required={"target", "tol"}, optional={"target", "tol"})
    target = _number(path, spec["target"], f"{where}.target")
    tol = _number(path, spec["tol"], f"{where}.tol")
    if tol < 0.0 or (kind == "angle_within" and tol >= math.pi):
        limit = "[0, pi)" if kind == "angle_within" else "[0, inf)"
        raise ScenarioError(f"{path}: {where}.tol: {tol!r} is outside {limit}")
    return target, tol


def _window(path: Path, value: Any, where: str, dt: float, frames: int, last_frame: int) -> Window:
    if value == "final":
        if last_frame < 1:
            raise ScenarioError(f"{path}: {where}: the run logs no row, so it has no final row")
        return Window(0, None)
    keys = {"from", "to"}
    spec = _closed(path, value, where, required=keys, optional=keys)
    first = _frames(path, spec["from"], f"{where}.from", dt)
    stop = _frames(path, spec["to"], f"{where}.to", dt)
    if not 0 <= first < stop <= frames:
        raise ScenarioError(f"{path}: {where}: needs 0 <= from < to <= duration")
    if stop > last_frame:
        raise ScenarioError(
            f"{path}: {where}.to: the run halts at frame {last_frame} (expect_halt), so no row "
            "reaches this window's end"
        )
    return Window(first, stop)


# --- run ----------------------------------------------------------------------------------------


def run(
    scenario: Scenario, log_path: str | Path, *, plugin_dir: str | Path | None = None
) -> ScenarioResult:
    """Runs ``scenario`` with its log at ``log_path`` (which must not exist), finalizes the log
    explicitly, reads it back strictly and verifies it (module docstring). Raises for a harness or
    setup failure, after removing the log; returns the failed assertions otherwise."""
    log_path = Path(log_path)
    if log_path.exists():
        raise FileExistsError(f"{log_path}: the scenario's log path already exists")
    data = copy.deepcopy(scenario.config)
    if scenario.trim is not None:
        _apply_trim(scenario, data)
    data["log"] = {"path": str(log_path)}
    config = _core.Config.from_dict(data, origin=str(scenario.path))
    directory = _core.default_plugin_dir() if plugin_dir is None else str(plugin_dir)
    sim: _core.Simulation | None = _core.Simulation.from_config(config, directory)
    try:
        _drive(sim, scenario)
        _close_log(sim)
        run_log = log.load(log_path)  # sim is still alive here: no destructor close yet
    except BaseException:
        sim = None  # the destructor closes the file before it is removed
        log_path.unlink(missing_ok=True)
        raise
    return ScenarioResult(run_log, verify(scenario, run_log))


def _apply_trim(scenario: Scenario, data: dict[str, Any]) -> None:
    """Trims a copy of ``data`` (which has no ``log`` block yet) and writes the result into
    ``data``'s initial state and controls."""
    setpoint = cast(TrimSetpoint, scenario.trim)
    assert "log" not in data, "the trim copy must not carry the run's log block"
    with tempfile.TemporaryDirectory(prefix="astra_scenario_trim_") as directory:
        still = Path(directory) / scenario.config_path.name
        still.write_text(yaml.safe_dump(data, sort_keys=False), encoding="utf-8")
        result = astra.trim.trim_straight_and_level(
            still, setpoint.true_airspeed, setpoint.altitude
        )
    worst = float(np.max(np.abs(result.residual)))
    if worst > TRIM_RESIDUAL_BOUND:
        raise RuntimeError(
            f"{scenario.path}: trim residual {worst:.3e} exceeds D8's {TRIM_RESIDUAL_BOUND:g}"
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


def _drive(sim: _core.Simulation, scenario: Scenario) -> None:
    """Runs to ``scenario.frames`` or a halt, writing each event before the step of its frame
    (the loop ``astra.log.replay`` uses)."""
    events = scenario.events
    next_event = 0
    while True:
        current = sim.clock.frame
        while next_event < len(events) and events[next_event].frame == current:
            sim.write_input(events[next_event].name, events[next_event].value)
            next_event += 1
        if current >= scenario.frames:
            return
        stop = events[next_event].frame if next_event < len(events) else scenario.frames
        if sim.run(stop - current):
            return  # the halt is in the log; verify() judges it


def _close_log(sim: _core.Simulation) -> None:
    sim.close_log()


# --- verify -------------------------------------------------------------------------------------


def verify(scenario: Scenario, run_log: log.RunLog) -> list[str]:
    """Every failed assertion of ``scenario`` against ``run_log`` (module docstring)."""
    failures: list[str] = []
    expected = scenario.expect_halt
    frames_run = run_log.frames_run
    if expected is None:
        if frames_run != scenario.frames:
            failures.append(
                f"frames_run is {frames_run}, not duration / dt = {scenario.frames} frames"
            )
    elif frames_run != expected.frame:
        failures.append(f"frames_run is {frames_run}, not expect_halt.frame {expected.frame}")
    failures.extend(_halt_failures(expected, run_log))
    failures.extend(_counter_failures(expected, run_log))
    for check in scenario.checks:
        failure = _check_failure(scenario, check, run_log)
        if failure is not None:
            failures.append(failure)
    return failures


def _describe(halt: log.Halt | ExpectedHalt) -> str:
    return f"{halt.check} from {halt.source} at frame {halt.frame}"


def _halt_failures(expected: ExpectedHalt | None, run_log: log.RunLog) -> list[str]:
    halt = run_log.halt
    if expected is None:
        return [] if halt is None else [f"unexpected halt: {_describe(halt)}"]
    if halt is None:
        return [f"missed expect_halt: {_describe(expected)} did not happen"]
    failures: list[str] = []
    if (halt.check, halt.source, halt.frame) != (expected.check, expected.source, expected.frame):
        failures.append(f"halt {_describe(halt)} is not expect_halt {_describe(expected)}")
    if run_log.frames_run != halt.frame:
        failures.append(f"frames_run is {run_log.frames_run}, not the halt's frame {halt.frame}")
    return failures


def _counter_failures(expected: ExpectedHalt | None, run_log: log.RunLog) -> list[str]:
    failures: list[str] = []
    names = frozenset(run_log.health)
    if names != HEALTH_SIGNALS:
        failures.append(
            f"the log's health signals {sorted(names)} are not {sorted(HEALTH_SIGNALS)}; "
            "astra.scenario's WARN_COUNTERS must be updated deliberately"
        )
    own = None if expected is None else COUNTER_OF_CHECK[expected.check]
    for counter in WARN_COUNTERS:
        logged = _logged_maximum(run_log, counter)
        if logged is None or logged != 0.0:
            failures.append(
                f"{counter} reaches {logged!r} in a logged row: a nonzero warn counter"
                if logged is not None
                else f"{counter} is not a logged signal"
            )
        value = run_log.health.get(counter)
        if counter == own:
            if value is None or not value >= 1.0:
                failures.append(f"{counter} is {value!r} after the expected halt, not >= 1")
        elif value != 0.0:
            failures.append(f"{counter} is {value!r} at the end of the run: a nonzero warn counter")
    return failures


def _logged_maximum(run_log: log.RunLog, signal: str) -> float | None:
    """The largest value ``signal`` takes in any logged row (NaN if any row is NaN), or ``None``
    when it is not logged. Row k is logged at the end of frame k, so the rows cover every frame
    that completed; a halted frame has no row."""
    for table in run_log.rates.values():
        if signal in table.columns:
            values = np.asarray(cast(Any, table)[signal].to_numpy(), dtype=np.float64)
            return float(np.max(values)) if values.size else 0.0
    return None


def _check_failure(scenario: Scenario, check: Check, run_log: log.RunLog) -> str | None:
    label = f"checks[{check.index}] {check.signal} {_describe_check(check)}"
    table = next((t for t in run_log.rates.values() if check.signal in t.columns), None)
    if table is None:
        return f"{label}: the signal is not in the log"
    data = cast(Any, table)
    frames = np.asarray(data.index.to_numpy(), dtype=np.int64)
    values = np.asarray(data[check.signal].to_numpy(), dtype=np.float64)
    window = check.window
    if window.stop is None:
        if frames.size == 0:
            return f"{label} over final: the log has no rows"
        selected = np.array([frames.size - 1])
        span = f"final (frame {int(frames[-1])})"
    else:
        selected = np.flatnonzero((frames >= window.first) & (frames < window.stop))
        span = (
            f"[{window.first * scenario.dt:g}, {window.stop * scenario.dt:g}) s "
            f"(frames {window.first}..{window.stop - 1})"
        )
        divisor = int(frames[1] - frames[0]) if frames.size > 1 else 1
        wanted = len(range(-(-window.first // divisor) * divisor, window.stop, divisor))
        if selected.size != wanted:
            return f"{label} over {span}: the log has {selected.size} of its {wanted} rows"
    excess = violation(check.kind, check.a, check.b, values[selected])
    worst = int(np.argmax(excess))
    if excess[worst] <= 0.0:
        return None
    row = int(selected[worst])
    frame = int(frames[row])
    return (
        f"{label} over {span}: worst value {float(values[row])!r} at frame {frame} "
        f"(t = {frame * scenario.dt:g} s)"
    )


def _describe_check(check: Check) -> str:
    if check.kind == "between":
        return f"between [{check.a!r}, {check.b!r}]"
    return f"{check.kind} {check.a!r} +/- {check.b!r}"


def violation(kind: str, a: float, b: float, values: np.ndarray) -> np.ndarray:
    """How far each value is outside the check (positive fails, zero or less passes); a NaN is
    ``inf``, so it is always the worst. ``(a, b)`` is ``(target, tol)`` or ``(lo, hi)``."""
    x = np.asarray(values, dtype=np.float64)
    if kind == "within":
        excess = np.abs(x - a) - b
    elif kind == "between":
        excess = np.maximum(a - x, x - b)
    elif kind == "angle_within":
        excess = np.abs(wrap(x - a)) - b
    else:
        raise ValueError(f"unknown check kind {kind!r}")
    return np.where(np.isnan(excess), np.inf, excess)


def wrap(angle: np.ndarray) -> np.ndarray:
    """``angle`` wrapped to [-pi, pi]."""
    turn = 2.0 * math.pi
    return angle - turn * np.round(angle / turn)
