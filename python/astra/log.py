"""astra.log: read an ASTRA run log and replay it (M5 PR 5; ADR-0025, docs/plans/m5-tooling.md R3,
R8, R9; finalization per ADR-0027, docs/plans/m6-pr2-finalization.md).

The file is the one ``Simulation`` writes when its config has a ``log`` block (ADR-0025): one
``/rate_<divisor>`` group per rate group (``frame`` uint64, ``values`` float64 rows x signals with
``names`` and ``units`` attributes, ``rows_committed`` on the group), provenance in root
attributes, and ``/plugins``, ``/writes``, ``/summary`` and ``/final_state``. ``load`` reads it into
pandas; ``replay`` re-executes the run it describes and reports whether the new file is
byte-identical.

h5py bundles its own libhdf5 (2.0.0 in the pinned wheel) beside the system 1.10 that
libastra_core links; the two coexist in one process in either import order (plan R8,
``tests/python/test_log.py``). This module is not imported by ``astra`` itself, so ``import astra``
never loads h5py.

Finalization (ADR-0027 item 6). A log is written as ``<log.path>.partial`` and reaches ``log.path``
only through ``close_log()``'s no-replace rename, so a file is *published* when its resolved name
does not end in ``.partial``. Its *state* is ``complete`` (``complete = 1``), ``destructor``
(``finalized_by = "destructor"``: the ``Simulation`` was dropped without ``close_log()``) or
``incomplete`` (neither: ``close_log()`` failed, or the run was killed).

- Strict (the default) refuses a path whose resolved name ends in ``.partial`` ("not published by
  close_log()"; a guard, not a proof: a hard link is not detected), refuses every state but
  complete, naming it, and applies the cross-checks.
- ``allow_incomplete=True`` reads a complete or destructor file whole, with the cross-checks, and an
  incomplete one as its provenance and each group's certified rows only (the rows below
  ``rows_committed``), ignoring anything written at close. It raises ``IncompleteLogError`` naming
  the group when ``rows_committed`` is missing or beyond either dataset's extent, when a chunk
  holding certified rows is unallocated, or when the certified ``frame`` column is not
  ``0, d, 2d, ...``.

The cross-checks on a finalized file: ADR-0025's structural checks (each ``/rate_<d>/frame`` is
exactly ``0, d, 2d, ...`` below ``frames_run``, ``values`` has one row per frame and one column per
name, a recorded halt is at ``frames_run``, ``/writes`` frames never decrease and never exceed
``frames_run``); ``final_state.frame == frames_run``; the rate groups' signal names, taken together,
equal ``/summary``'s, each exactly once; and each group's ``rows_committed`` equals its row count.
They compare counts, names and frame numbers, not values: a hand-edited value passes them.
"""

from __future__ import annotations

import collections
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

# The file layout run_log.cpp writes (RunLog::kFormatVersion): 2 since ADR-0027.
FORMAT_VERSION = 2

# ADR-0027 item 6: the three finalization states.
COMPLETE = "complete"
DESTRUCTOR = "destructor"
INCOMPLETE = "incomplete"

_STATE_REASON = {
    DESTRUCTOR: "finalized_by = destructor: the Simulation was dropped without close_log()",
    INCOMPLETE: "neither complete nor finalized_by: close_log() did not finish, or the run was "
    "killed",
}

PARTIAL_SUFFIX = ".partial"

_RATE_GROUP = re.compile(r"rate_([1-9][0-9]*)")


class LogFormatError(Exception):
    """The file is not a run log this module can read: it does not open, has no known
    ``log_format_version``, lacks an object the format requires, or (for ``replay``) its stored
    config cannot take a ``log`` block."""


class IncompleteLogError(Exception):
    """``load`` refused the log (module docstring, Finalization): an unpublished ``.partial``
    name, a state other than complete, a failed cross-check, or a certified-row check of an
    incomplete file. The message names the check (and the group, for a certified-row check)."""


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
    signal in file order; ``units`` maps each signal to its unit string. ``state`` is the
    finalization state and ``published`` whether the resolved name is not a ``.partial``
    (ADR-0027). The parts written only at close (``frames_run``, ``halt``, ``writes``,
    ``health``, ``minimum``, ``maximum``, ``final_*``) are ``None`` (or empty) for an incomplete
    log, whose ``rates`` hold only its certified rows (``rows_committed`` per divisor)."""

    path: Path
    complete: bool
    state: str
    published: bool
    rows_committed: dict[int, int]
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
    """Reads the run log at ``path`` (module docstring, Finalization). Raises ``LogFormatError``
    for a file that is not a readable run log of this format version, and ``IncompleteLogError``
    naming the refusal: without ``allow_incomplete``, an unpublished ``.partial`` name or a state
    other than complete; in either mode, a failed cross-check or certified-row check."""
    path = Path(path)
    published = not path.resolve().name.endswith(PARTIAL_SUFFIX)
    if not allow_incomplete and not published:
        raise IncompleteLogError(
            f"{path}: not published by close_log(): its resolved name ends in {PARTIAL_SUFFIX} "
            "(ADR-0027); pass allow_incomplete=True to read it anyway"
        )
    try:
        file = cast(Any, h5py.File(path, "r"))
    except OSError as error:
        raise LogFormatError(f"{path}: not a readable HDF5 file ({error})") from error
    with file:
        try:
            return _read(path, file, allow_incomplete, published)
        except (KeyError, OSError) as error:
            # h5py's errors for an object it cannot open or read. A failed close_log() leaves
            # such a file (a real RLIMIT_FSIZE failure opens but has no readable root object), so
            # it is refused as incomplete; allow_incomplete cannot read what is not there.
            raise IncompleteLogError(
                f"{path}: an HDF5 object cannot be read ({error}); the file was not closed cleanly"
            ) from error


def replay(path: str | Path, out: str | Path, *, plugin_dir: str | Path | None = None) -> bool:
    """Re-executes the run the complete, published log at ``path`` records (ADR-0025, Replay;
    ADR-0027 item 7) and writes its log to ``out``. Returns whether ``out`` is byte-identical to
    ``path``; when it is not, ``out`` is deleted through ``discard_log()`` and ``False`` returned.

    ``path`` is loaded strictly: an unpublished ``.partial`` (even one holding ``complete = 1``)
    and a destructor or incomplete log are refused, naming the reason. ``out`` is made absolute and
    follows ``log.path``'s rules: an existing ``out``, an existing ``<out>.partial`` and a name
    ending in ``.partial`` are each refused (the core's ``ConfigError``). Replay closes with
    ``close_log()``, which publishes to ``out``.

    The ``Simulation`` is rebuilt from the stored config plus a ``log`` block naming ``out``
    (``plugin_dir`` defaults to ``astra._core.default_plugin_dir()``); ``/writes`` are applied
    through ``write_input`` before the step of their frame; ``frames_run`` frames run; for a
    halted log one more ``step()`` must reproduce the recorded halt.

    ``True`` needs the same build as well as a deterministic run: the file stores
    ``git_describe`` and each plugin's SHA-256, so a rebuilt plugin or a different (or dirty)
    tree gives ``False`` even when every simulated value matches.

    Raises ``ReplayError`` for a log from a run with a ``transport`` block or with a ``LOG``
    halt (neither can be reproduced), for a halt before ``frames_run``, and for a final halt that
    differs from the recorded one; replay's own log is then discarded (``discard_log()``: the
    ``.partial``, or ``out`` once published). Every delete goes through ``discard_log()``
    (ADR-0027 item 8, ``astra._runkit.discard_on_failure``): if it deletes nothing (the name is
    missing or no longer replay's file), the error carries a note saying so, and after a byte
    mismatch replay raises ``ReplayError`` naming the result instead of returning ``False``, so a
    caller never believes ``out`` was removed when it was not. Limits: writes made after a halted
    call carry the same frame as those before it, so all of them are applied before the step; a
    run that continued past a non-sticky halt halts early here and is refused.
    """
    from astra import _runkit  # here, not at the top: _runkit imports this module

    path = Path(path)
    out = Path(out).absolute()
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
    # A from_config that fails deletes the .partial it created itself (ADR-0027 item 8).
    sim = _core.Simulation.from_config(config, directory)
    try:
        _rerun(path, log, sim)
        sim.close_log()
    except BaseException as error:
        _runkit.discard_on_failure(sim, error)
        raise
    if _identical(out, path):
        return True
    result = sim.discard_log()
    if not result.deleted:
        raise ReplayError(
            f"{path}: the replay's log {out} is not byte-identical, and discard_log() did not "
            f"remove it ({result.outcome.name}: {result.detail})"
        )
    return False


def _identical(out: Path, path: Path) -> bool:
    """ADR-0025's determinism criterion: the published replay log and the original, byte for
    byte."""
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


@dataclasses.dataclass(frozen=True)
class _Provenance:
    """What every state exposes: the root attributes and /plugins, and the state itself."""

    path: Path
    state: str
    published: bool
    format_version: int
    config: str
    dt: float
    git_describe: str
    core_version: str
    core_api_version: int
    plugins: pd.DataFrame | None


def _assemble(
    provenance: _Provenance,
    *,
    rows_committed: dict[int, int],
    rates: dict[int, pd.DataFrame],
    units: dict[str, str],
    writes: pd.DataFrame | None,
    frames_run: int | None,
    halt: Halt | None,
    health: dict[str, float],
    minimum: pd.Series | None,
    maximum: pd.Series | None,
    final_state: dict[str, pd.Series],
    final_frame: int | None,
    final_after_halt: bool | None,
) -> RunLog:
    return RunLog(
        path=provenance.path,
        complete=provenance.state == COMPLETE,
        state=provenance.state,
        published=provenance.published,
        format_version=provenance.format_version,
        config=provenance.config,
        dt=provenance.dt,
        git_describe=provenance.git_describe,
        core_version=provenance.core_version,
        core_api_version=provenance.core_api_version,
        rows_committed=rows_committed,
        rates=rates,
        units=units,
        plugins=provenance.plugins,
        writes=writes,
        frames_run=frames_run,
        halt=halt,
        health=health,
        minimum=minimum,
        maximum=maximum,
        final_state=final_state,
        final_frame=final_frame,
        final_after_halt=final_after_halt,
    )


def _state(attrs: Any) -> str:
    if int(attrs.get("complete", 0)) == 1:
        return COMPLETE
    finalized_by = attrs.get("finalized_by")
    if finalized_by is not None and _text(finalized_by) == DESTRUCTOR:
        return DESTRUCTOR
    return INCOMPLETE


def _text(value: Any) -> str:
    return value.decode() if isinstance(value, bytes) else str(value)


def _read(path: Path, file: Any, allow_incomplete: bool, published: bool) -> RunLog:
    attrs = file.attrs
    version = attrs.get("log_format_version")
    if version is None or int(version) != FORMAT_VERSION:
        raise LogFormatError(
            f"{path}: log_format_version {version!r} is not {FORMAT_VERSION} (ADR-0027); not a "
            "run log this module reads"
        )
    state = _state(attrs)
    if not allow_incomplete and state != COMPLETE:
        raise IncompleteLogError(
            f"{path}: finalization state {state}: {_STATE_REASON[state]} (ADR-0027); pass "
            "allow_incomplete=True to read it anyway"
        )
    dt = float(_required(path, attrs, "dt"))
    provenance = _Provenance(
        path=path,
        state=state,
        published=published,
        format_version=int(version),
        config=str(_required(path, attrs, "config")),
        dt=dt,
        git_describe=str(_required(path, attrs, "git_describe")),
        core_version=str(_required(path, attrs, "core_version")),
        core_api_version=int(_required(path, attrs, "core_api_version")),
        plugins=_records(file.get("plugins")),
    )
    if state == INCOMPLETE:
        rates, units, committed = _certified_groups(path, file, dt)
        return _assemble(
            provenance,
            rows_committed=committed,
            rates=rates,
            units=units,
            writes=None,
            frames_run=None,
            halt=None,
            health={},
            minimum=None,
            maximum=None,
            final_state={},
            final_frame=None,
            final_after_halt=None,
        )
    return _finalized(path, file, provenance)


def _finalized(path: Path, file: Any, provenance: _Provenance) -> RunLog:
    """A complete or destructor file, read whole, with the cross-checks (module docstring)."""
    dt = provenance.dt
    summary = file.get("summary")
    frames_run = None if summary is None else _optional_int(summary.attrs.get("frames_run"))
    halt = None if summary is None else _halt(summary.attrs)
    writes = _records(file.get("writes"))
    rates: dict[int, pd.DataFrame] = {}
    units: dict[str, str] = {}
    committed: dict[int, int] = {}
    group_names: list[str] = []
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
        rows_committed = _optional_int(file[key].attrs.get("rows_committed"))
        if rows_committed is None:
            problems.append(f"/{key} has no rows_committed")
        else:
            committed[divisor] = rows_committed
            if rows_committed != frame.size:
                problems.append(
                    f"/{key} rows_committed {rows_committed} is not its row count {frame.size}"
                )
        rates[divisor] = _frame(path, key, frame, values, names, dt)
        units.update(zip(names, group_units, strict=False))
        group_names.extend(names)
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
    minimum = _named_series(summary, "min")
    problems.extend(_name_problems(group_names, minimum))
    final_frame, final_after_halt, final_state = _final_state(file.get("final_state"))
    if final_frame is None:
        problems.append("/final_state or its frame is missing")
    elif frames_run is not None and final_frame != frames_run:
        problems.append(f"final_state.frame {final_frame} is not frames_run {frames_run}")
    if problems:
        raise IncompleteLogError(
            f"{path}: inconsistent with its own /summary: {'; '.join(problems)}"
        )
    return _assemble(
        provenance,
        rows_committed=committed,
        rates=rates,
        units=units,
        writes=writes,
        frames_run=frames_run,
        halt=halt,
        health=_health(summary),
        minimum=minimum,
        maximum=_named_series(summary, "max"),
        final_state=final_state,
        final_frame=final_frame,
        final_after_halt=final_after_halt,
    )


def _name_problems(group_names: list[str], minimum: pd.Series | None) -> list[str]:
    """ADR-0027's name cross-check: the rate groups' names, taken together, equal /summary's,
    each exactly once."""
    if minimum is None:
        return ["/summary/min, whose names the rate groups are checked against, is missing"]
    problems: list[str] = []
    summary_names = [str(name) for name in cast(Any, minimum).index.tolist()]
    for label, names in (("the rate groups", group_names), ("/summary", summary_names)):
        repeated = sorted(name for name, count in collections.Counter(names).items() if count > 1)
        if repeated:
            problems.append(f"{label} name {', '.join(repeated)} more than once")
    missing = sorted(set(summary_names) - set(group_names))
    extra = sorted(set(group_names) - set(summary_names))
    if missing or extra:
        problems.append(
            "the rate groups' signal names are not /summary's "
            f"(only in /summary: {missing}; only in the rate groups: {extra})"
        )
    return problems


def _certified_groups(
    path: Path, file: Any, dt: float
) -> tuple[dict[int, pd.DataFrame], dict[str, str], dict[int, int]]:
    """An incomplete file's rate groups, each cut to its certified rows (ADR-0027 item 6)."""
    rates: dict[int, pd.DataFrame] = {}
    units: dict[str, str] = {}
    committed: dict[int, int] = {}
    for key in sorted(file.keys()):
        match = _RATE_GROUP.fullmatch(key)
        if match is None:
            continue
        divisor = int(match.group(1))
        group = file[key]
        if "frame" not in group or "values" not in group:
            raise LogFormatError(f"{path}: /{key} lacks its frame or values dataset")
        value = group.attrs.get("rows_committed")
        if value is None:
            raise IncompleteLogError(f"{path}: /{key}: rows_committed is missing")
        rows = int(value)
        frame_set, values_set = group["frame"], group["values"]
        if rows > frame_set.shape[0] or rows > values_set.shape[0]:
            raise IncompleteLogError(
                f"{path}: /{key}: rows_committed {rows} exceeds the extent of frame "
                f"({frame_set.shape[0]} rows) or values ({values_set.shape[0]} rows)"
            )
        for name, dataset in (("frame", frame_set), ("values", values_set)):
            _require_allocated(path, f"/{key}/{name}", dataset, rows)
        frame = np.asarray(frame_set[:rows], dtype=np.uint64)
        values = np.asarray(values_set[:rows], dtype=np.float64)
        if not np.array_equal(frame, np.arange(0, rows * divisor, divisor, dtype=np.uint64)):
            raise IncompleteLogError(
                f"{path}: /{key}: the certified frame column is not 0, {divisor}, "
                f"{2 * divisor}, ..."
            )
        names = _strings(values_set.attrs.get("names", []))
        group_units = _strings(values_set.attrs.get("units", []))
        if values.ndim != 2 or values.shape[1] != len(names) or len(group_units) != len(names):
            raise IncompleteLogError(
                f"{path}: /{key}: values has shape {values.shape} for {len(names)} names and "
                f"{len(group_units)} units"
            )
        rates[divisor] = _frame(path, key, frame, values, names, dt)
        units.update(zip(names, group_units, strict=True))
        committed[divisor] = rows
    if not rates:
        raise IncompleteLogError(f"{path}: no /rate_<divisor> group")
    return rates, units, committed


def _require_allocated(path: Path, name: str, dataset: Any, rows: int) -> None:
    """Every chunk holding one of the first ``rows`` rows is on disk (h5py's chunk information):
    an unallocated chunk would read back as fill values."""
    chunks = dataset.chunks
    if chunks is None:
        raise LogFormatError(f"{path}: {name} is not chunked")
    for start in range(0, rows, int(chunks[0])):
        coord = (start,) + (0,) * (dataset.ndim - 1)
        if dataset.id.get_chunk_info_by_coord(coord).byte_offset is None:
            raise IncompleteLogError(
                f"{path}: {name}: the chunk at row {start}, which holds certified rows, is not "
                "allocated"
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
