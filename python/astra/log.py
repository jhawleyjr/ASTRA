"""astra.log: read an ASTRA run log and replay it (M5 PR 5; ADR-0025, docs/plans/m5-tooling.md R3,
R8, R9).

The file is the one ``Simulation`` writes when its config has a ``log`` block (ADR-0025): one
``/rate_<divisor>`` group per rate group (``frame`` uint64, ``values`` float64 rows x signals with
``names`` and ``units`` attributes), provenance in root attributes, and ``/plugins``, ``/writes``,
``/summary`` and ``/final_state``. ``load`` reads it into pandas; ``replay`` re-executes the run it
describes and reports whether the new file is byte-identical.

h5py bundles its own libhdf5 (2.0.0 in the pinned wheel) beside the system 1.10 that
libastra_core links; the two coexist in one process in either import order (plan R8,
``tests/python/test_log.py``). This module is not imported by ``astra`` itself, so ``import astra``
never loads h5py.

What ``load``'s checks cover. A file without ``complete = 1`` is refused, and so is one whose
structure disagrees with its own ``/summary``: each ``/rate_<d>/frame`` must be exactly
``0, d, 2d, ...`` below ``frames_run`` (so its row count is ``ceil(frames_run / d)`` with no gap),
``values`` must have one row per frame and one column per name, a recorded halt must be at
``frames_run``, and ``/writes`` frames must never decrease and never exceed ``frames_run``. These
compare counts and frame numbers, not values. They cannot flag a ``Simulation`` that was dropped
mid-run, for example by a Python driver that raised: its destructor finalizes the log, and the file
is a consistent record of the frames that ran, with ``complete = 1``. A caller that expects a
number of frames compares ``RunLog.frames_run`` and ``RunLog.halt`` itself.
"""

from __future__ import annotations

import dataclasses
import itertools
import json
import math
import re
from pathlib import Path
from typing import Any, cast

import h5py  # pyright: ignore[reportMissingTypeStubs]
import numpy as np
import pandas as pd  # pyright: ignore[reportMissingTypeStubs]

from astra import _core

# ADR-0025's file layout as PR 4 writes it (run_log.cpp kFormatVersion).
FORMAT_VERSION = 1

_RATE_GROUP = re.compile(r"rate_([1-9][0-9]*)")


class LogFormatError(Exception):
    """The file is not a run log this module can read: it does not open, has no known
    ``log_format_version``, lacks an object the format requires, or (for ``replay``) its stored
    config cannot take a ``log`` block."""


class IncompleteLogError(Exception):
    """The log has no ``complete = 1``, or its structure disagrees with its own ``/summary``
    (module docstring). ``load(..., allow_incomplete=True)`` reads it anyway."""


class ReplayError(Exception):
    """``replay`` refused the log (a ``transport`` block, a ``LOG`` halt) or could not reproduce
    the run it records (an earlier halt, or a different final halt)."""


@dataclasses.dataclass(frozen=True)
class Halt:
    """The halt ``/summary`` records for the run's last call (``HaltReason`` fields)."""

    check: str
    source: str
    frame: int
    vehicle_index: int


@dataclasses.dataclass(frozen=True)
class RunLog:
    """A loaded run log. ``rates`` maps each divisor to a DataFrame indexed by ``frame`` whose
    first column ``t`` is ``frame * dt`` (ADR-0010: derived, never stored) followed by every
    signal in file order; ``units`` maps each signal to its unit string. The parts written only
    at close (``frames_run``, ``halt``, ``writes``, ``health``, ``minimum``, ``maximum``,
    ``final_*``) are ``None`` (or empty) in an incomplete log that lacks them."""

    path: Path
    complete: bool
    format_version: int
    config: str
    dt: float
    git_describe: str
    core_version: str
    core_api_version: int
    rates: dict[int, pd.DataFrame]
    units: dict[str, str]
    plugins: pd.DataFrame | None
    writes: pd.DataFrame | None
    frames_run: int | None
    halt: Halt | None
    health: dict[str, float]
    minimum: pd.Series | None
    maximum: pd.Series | None
    final_state: dict[str, pd.Series]
    final_frame: int | None
    final_after_halt: bool | None


def load(path: str | Path, *, allow_incomplete: bool = False) -> RunLog:
    """Reads the run log at ``path``. Raises ``LogFormatError`` for a file that is not a readable
    run log, and ``IncompleteLogError`` naming the failed check for one without ``complete = 1``
    or whose structure disagrees with its ``/summary`` (module docstring), unless
    ``allow_incomplete`` is set, which reads whatever the file holds."""
    path = Path(path)
    try:
        file = cast(Any, h5py.File(path, "r"))
    except OSError as error:
        raise LogFormatError(f"{path}: not a readable HDF5 file ({error})") from error
    with file:
        try:
            return _read(path, file, allow_incomplete)
        except (KeyError, OSError) as error:
            # h5py's errors for an object it cannot open or read. A failed close_log() leaves
            # such a file (a real RLIMIT_FSIZE failure opens but has no readable root object), so
            # it is refused as incomplete; allow_incomplete cannot read what is not there.
            raise IncompleteLogError(
                f"{path}: an HDF5 object cannot be read ({error}); the file was not closed cleanly"
            ) from error


def replay(path: str | Path, out: str | Path, *, plugin_dir: str | Path | None = None) -> bool:
    """Re-executes the run the complete log at ``path`` records (ADR-0025, Replay) and writes its
    log to ``out``, which must not exist. Returns whether ``out`` is byte-identical to ``path``.

    The ``Simulation`` is rebuilt from the stored config plus a ``log`` block naming ``out``
    (``plugin_dir`` defaults to ``astra._core.default_plugin_dir()``); ``/writes`` are applied
    through ``write_input`` before the step of their frame; ``frames_run`` frames run; for a
    halted log one more ``step()`` must reproduce the recorded halt.

    ``True`` needs the same build as well as a deterministic run: the file stores
    ``git_describe`` and each plugin's SHA-256, so a rebuilt plugin or a different (or dirty)
    tree gives ``False`` even when every simulated value matches.

    Raises ``ReplayError`` for a log from a run with a ``transport`` block or with a ``LOG``
    halt (neither can be reproduced), for a halt before ``frames_run``, and for a final halt that
    differs from the recorded one; ``out`` is then removed. Limits: writes made after a halted
    call carry the same frame as those before it, so all of them are applied before the step; a
    run that continued past a non-sticky halt halts early here and is refused.
    """
    path = Path(path)
    out = Path(out)
    log = load(path)
    stored = _core.Config.from_string(log.config, str(path))
    if stored.has("transport"):
        raise ReplayError(
            f"{path}: the run had a transport block; replaying its commands without the peer is "
            "not replay (ADR-0025)"
        )
    if log.halt is not None and log.halt.check == _core.HealthCheck.LOG.name:
        raise ReplayError(
            f"{path}: the run halted LOG at frame {log.halt.frame}; that halt depends on the "
            "output side (buffer_frames, drain calls, I/O), which the log does not record "
            "(ADR-0025)"
        )
    if not log.config.endswith("\n"):
        raise LogFormatError(f"{path}: the stored config does not end with a newline")
    if stored.has("log"):
        raise LogFormatError(f"{path}: the stored config has a top-level log key")
    text = f"{log.config}log:\n  path: {json.dumps(str(out))}\n"
    config = _core.Config.from_string(text, f"replay of {path}")
    directory = _core.default_plugin_dir() if plugin_dir is None else str(plugin_dir)
    sim: _core.Simulation | None = _core.Simulation.from_config(config, directory)
    try:
        _rerun(path, log, sim)
        sim.close_log()
    except BaseException:
        sim = None  # the destructor closes out before it is removed
        out.unlink(missing_ok=True)
        raise
    return out.read_bytes() == path.read_bytes()


def _rerun(path: Path, log: RunLog, sim: _core.Simulation) -> None:
    frames_run = cast(int, log.frames_run)  # load() without allow_incomplete guarantees both
    writes = cast(Any, log.writes)
    pending: list[tuple[int, str, float]] = [
        (int(frame), str(name), float(value))
        for frame, name, value in zip(
            cast(list[Any], writes["frame"].tolist()),
            cast(list[Any], writes["name"].tolist()),
            cast(list[Any], writes["value"].tolist()),
            strict=True,
        )
    ]
    next_write = 0
    while True:
        current = sim.clock.frame
        while next_write < len(pending) and pending[next_write][0] == current:
            _, name, value = pending[next_write]
            _write(sim, name, value)
            next_write += 1
        if current == frames_run:
            break
        stop = pending[next_write][0] if next_write < len(pending) else frames_run
        if reason := sim.run(stop - current):
            raise ReplayError(
                f"{path}: replay halted {reason.check.name} ({reason.source}) at frame "
                f"{reason.frame}, before the recorded frames_run {frames_run}"
            )
    if log.halt is not None:
        reason = sim.step()
        got = (
            Halt(reason.check.name, reason.source, reason.frame, reason.vehicle_index)
            if reason
            else None
        )
        if got != log.halt:
            raise ReplayError(f"{path}: halt not reproduced: recorded {log.halt}, replay {got}")


def _write(sim: _core.Simulation, name: str, value: float) -> None:
    try:
        sim.write_input(name, value)
    except _core.Error:
        # A non-finite write is recorded before write_input raises (the next frame's latch halts
        # on it), so the original run raised here too; anything else is a real failure.
        if math.isfinite(value):
            raise


def _read(path: Path, file: Any, allow_incomplete: bool) -> RunLog:
    attrs = file.attrs
    version = attrs.get("log_format_version")
    if version is None or int(version) != FORMAT_VERSION:
        raise LogFormatError(
            f"{path}: log_format_version {version!r} is not {FORMAT_VERSION}; not a run log "
            "this module reads"
        )
    complete = int(attrs.get("complete", 0)) == 1
    if not allow_incomplete and not complete:
        raise IncompleteLogError(
            f"{path}: no complete = 1: close_log() did not finish (ADR-0025); pass "
            "allow_incomplete=True to read it anyway"
        )
    dt = float(_required(path, attrs, "dt"))
    summary = file.get("summary")
    frames_run = None if summary is None else _optional_int(summary.attrs.get("frames_run"))
    halt = None if summary is None else _halt(summary.attrs)
    writes = _records(file.get("writes"))
    rates: dict[int, pd.DataFrame] = {}
    units: dict[str, str] = {}
    problems: list[str] = []
    if summary is None or frames_run is None:
        problems.append("/summary or its frames_run is missing")
    for key in sorted(file.keys()):
        match = _RATE_GROUP.fullmatch(key)
        if match is None:
            continue
        divisor = int(match.group(1))
        frame, values, names, group_units = _rate_group(path, file[key], key)
        if frames_run is not None:
            expected = np.arange(0, frames_run, divisor, dtype=np.uint64)
            if frame.shape != expected.shape or not np.array_equal(frame, expected):
                problems.append(
                    f"/{key}/frame is not 0, {divisor}, ... below frames_run {frames_run} "
                    f"({frame.size} rows, expected {expected.size})"
                )
        if values.shape != (frame.size, len(names)):
            problems.append(
                f"/{key}/values has shape {values.shape}, expected ({frame.size}, {len(names)})"
            )
        if len(group_units) != len(names):
            problems.append(f"/{key}/values has {len(names)} names but {len(group_units)} units")
        rates[divisor] = _frame(path, key, frame, values, names, dt)
        units.update(zip(names, group_units, strict=False))
    if not rates:
        problems.append("no /rate_<divisor> group")
    if frames_run is not None and halt is not None and halt.frame != frames_run:
        problems.append(f"the halt is at frame {halt.frame}, not at frames_run {frames_run}")
    if frames_run is not None and writes is not None and len(writes) != 0:
        write_frames = [int(f) for f in cast(list[Any], cast(Any, writes)["frame"].tolist())]
        if any(b < a for a, b in itertools.pairwise(write_frames)):
            problems.append("/writes frames decrease")
        if max(write_frames) > frames_run:
            problems.append(f"a /writes frame is past frames_run {frames_run}")
    if writes is None:
        problems.append("/writes is missing")
    if not allow_incomplete and problems:
        raise IncompleteLogError(
            f"{path}: inconsistent with its own /summary: {'; '.join(problems)}; pass "
            "allow_incomplete=True to read it anyway"
        )
    final_frame, final_after_halt, final_state = _final_state(file.get("final_state"))
    return RunLog(
        path=path,
        complete=complete,
        format_version=int(version),
        config=str(_required(path, attrs, "config")),
        dt=dt,
        git_describe=str(_required(path, attrs, "git_describe")),
        core_version=str(_required(path, attrs, "core_version")),
        core_api_version=int(_required(path, attrs, "core_api_version")),
        rates=rates,
        units=units,
        plugins=_records(file.get("plugins")),
        writes=writes,
        frames_run=frames_run,
        halt=halt,
        health=_health(summary),
        minimum=_named_series(summary, "min"),
        maximum=_named_series(summary, "max"),
        final_state=final_state,
        final_frame=final_frame,
        final_after_halt=final_after_halt,
    )


def _required(path: Path, attrs: Any, name: str) -> Any:
    value = attrs.get(name)
    if value is None:
        raise LogFormatError(f"{path}: the root attribute {name} is missing")
    return value


def _optional_int(value: Any) -> int | None:
    return None if value is None else int(value)


def _strings(values: Any) -> list[str]:
    return [v.decode() if isinstance(v, bytes) else str(v) for v in list(values)]


def _halt(attrs: Any) -> Halt | None:
    check = attrs.get("halt_check")
    if check is None or str(check) == _core.HealthCheck.NONE.name:
        return None
    return Halt(
        str(check),
        str(attrs.get("halt_source", "")),
        int(attrs.get("halt_frame", 0)),
        int(attrs.get("halt_vehicle", 0)),
    )


def _rate_group(
    path: Path, group: Any, key: str
) -> tuple[np.ndarray, np.ndarray, list[str], list[str]]:
    if "frame" not in group or "values" not in group:
        raise LogFormatError(f"{path}: /{key} lacks its frame or values dataset")
    dataset = group["values"]
    frame = np.asarray(group["frame"][()], dtype=np.uint64)
    values = np.asarray(dataset[()], dtype=np.float64)
    names = _strings(dataset.attrs.get("names", []))
    units = _strings(dataset.attrs.get("units", []))
    return frame, values, names, units


def _frame(
    path: Path, key: str, frame: np.ndarray, values: np.ndarray, names: list[str], dt: float
) -> pd.DataFrame:
    if "t" in names:
        raise LogFormatError(f"{path}: /{key} has a signal named t, which load() derives")
    if values.ndim != 2 or values.shape[1] != len(names):
        values = np.empty((0, len(names)))  # a mismatched shape, already reported as a problem
        frame = frame[:0]
    elif values.shape[0] != frame.size:
        rows = min(values.shape[0], frame.size)
        values, frame = values[:rows], frame[:rows]
    data = pd.DataFrame(values, columns=pd.Index(names), index=pd.Index(frame, name="frame"))
    data.insert(0, "t", frame.astype(np.float64) * dt)  # VirtualClock::t(): double(frame) * dt
    return data


def _records(dataset: Any) -> pd.DataFrame | None:
    if dataset is None:
        return None
    table = dataset[()]
    columns: dict[str, Any] = {}
    for name in cast(tuple[str, ...], table.dtype.names):
        column = table[name]
        columns[name] = _strings(column) if column.dtype.kind == "O" else column
    return pd.DataFrame(columns)


def _health(summary: Any) -> dict[str, float]:
    if summary is None:
        return {}
    return {
        str(name): float(value)
        for name, value in summary.attrs.items()
        if str(name).startswith("health.")
    }


def _named_series(summary: Any, name: str) -> pd.Series | None:
    if summary is None or name not in summary:
        return None
    dataset = summary[name]
    return pd.Series(
        np.asarray(dataset[()], dtype=np.float64),
        index=pd.Index(_strings(dataset.attrs.get("names", []))),
    )


def _final_state(group: Any) -> tuple[int | None, bool | None, dict[str, pd.Series]]:
    if group is None:
        return None, None, {}
    states: dict[str, pd.Series] = {}
    for vehicle in sorted(group.keys()):
        dataset = group[vehicle]
        states[str(vehicle)] = pd.Series(
            np.asarray(dataset[()], dtype=np.float64),
            index=pd.Index(_strings(dataset.attrs.get("names", []))),
        )
    after_halt = group.attrs.get("after_halt")
    return (
        _optional_int(group.attrs.get("frame")),
        None if after_halt is None else int(after_halt) == 1,
        states,
    )
