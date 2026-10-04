"""astra.montecarlo: dispersed batches of open-loop runs (M5 PR 7; ADR-0025,
docs/plans/m5-tooling.md Q17 to Q20, R14 to R17, R15b).

A spec is one YAML file. ``run(spec, out_dir, workers=None)`` runs it: one HDF5 log per run
(``run_0000.h5``, ...) and ``summary.parquet``, one row per run. Every value is SI (ADR-0002).

Spec (Q17). Every map is closed: an unknown key at any level is an error (``SpecError``, naming the
file and the key).

- ``config``: path of the base config, relative to ``load``'s ``root``. A config with a
  ``transport`` block is refused (the Monte Carlo runs open loop); so is one with a ``log`` block,
  because the batch writes each run's log itself. The config must build: ``load`` builds a
  ``Simulation`` from it, with no log and no trim, to learn ``dt`` and the signal names. The config
  is read as the core reads it (``astra._yaml_route``, M6 PR 11): a value PyYAML reads differently
  (``off``, ``010``, ``1_000``) keeps the core's own text, so a dispersion of it is refused as not
  a number, and what text cannot carry (a null, a duplicate or merge key) is refused, naming the
  key. The spec itself is Python's own format, YAML 1.1 as ``yaml.safe_load`` reads it (D3):
  ``seed: 010`` is 8, ``runs: 1_000`` is 1000 and ``duration: 1:30`` is 90 s.
- ``trim``: optional ``{true_airspeed, altitude}``, the same trim as ``astra.scenario``'s: each run
  is trimmed after its draws are applied, on a copy of its config without the ``log`` block (and
  without ``transport``, which is refused anyway), so trim never creates the run's file.
- ``duration``: seconds, greater than zero, an exact multiple of ``dt`` (``astra.scenario``'s
  rule). Each run asks for ``duration / dt`` frames.
- ``runs``: an integer >= 1. ``seed``: the master seed, an integer in [0, 2^64).
- ``dispersions``: a non-empty map ``{dotted.path: distribution}``. The path follows
  ``astra.scenario``'s override rule: an existing numeric leaf only, never a new key, with integer
  segments indexing lists (``vehicle.models.0.r_aero_b_x_m``). With ``trim`` set, a path trim sets
  or assumes (``astra.scenario.TRIM_OWNED``) is refused, naming the key and the reason, because the
  draw would be discarded or leave the run untrimmed. ``physical.wind`` stays dispersible: trim is
  still-air by design, and wind is a disturbance from trim. A distribution is one of:
  - ``{uniform: {lo, hi}}``: the drawn value itself is uniform on [lo, hi), lo < hi, both finite
    (absolute bounds, not offsets from the config's value): ``lo + (hi - lo) * u``.
  - ``{normal: {sigma, mode, clip}}``, all three required, centered on the config's value
    ``base``. ``mode: absolute``: the value is ``base + sigma * z`` (sigma in the leaf's units).
    ``mode: relative``: ``base + (sigma * |base|) * z`` (sigma a fraction; ``base`` must not be
    zero). ``clip`` is in standard deviations (dimensionless, at least ``MIN_CLIP`` = 1, so at
    least 68 % of draws are kept and the redraw loop ends): a draw with ``|z| > clip`` is
    **redrawn**, never clamped, so the value is a normal truncated to ``base +/- clip`` standard
    deviations, with no extra probability at the bounds.
  - ``seed``: for an integer plugin seed (the config's value must be an integer): an integer
    uniform on [0, 2^53), exactly representable as a double, since the SignalRegistry logs a seed
    as one (the Dryden plugin refuses a seed above 2^53).
- ``signals``: optional list of registered signal names; the summary carries each one's minimum
  and maximum over the run (the log's ``/summary``).

Randomness (Q17). Run ``i``'s seed is the first 8 bytes, read big-endian, of SHA-256 over the
master seed then ``i``, each an unsigned 64-bit big-endian integer. It seeds one ``Rng`` for the
run: Python's ``random.Random`` (MT19937 seeded from an integer, a stable stream) with in-house
transforms mirroring ``astra::random::Rng`` (``core/include/astra/util/random.hpp``): uniform is
the top 53 bits of a 64-bit draw times 2^-53, and normal is Box-Muller with ``u1 = 1 - uniform``,
caching the second variate. No numpy distributions (numpy is unpinned and promises no stable
stream). The dispersions are drawn in spec order from that one stream, so a run's values depend
only on the spec and its index, never on the worker count or the completion order.

A run (``run_one``). Its drawn values replace the leaves of a copy of the base config; then trim,
if set; then the run, through the run pattern ``astra.scenario`` uses: the ``Simulation`` is built
with its log at ``out_dir/run_<i>.h5`` (written as ``run_<i>.h5.partial`` until ``close_log()``
publishes it, ADR-0027), runs ``duration / dt`` frames or to a halt, and its log is finalized by
an explicit ``close_log()`` (never the destructor) and read back strictly with ``astra.log.load``
(never ``allow_incomplete``) while the ``Simulation`` is alive, then checked (the run-length and
``LOG``-halt checks below) while it is still alive. If anything raises after the ``Simulation`` is
built, its log is discarded through ``discard_log()`` (``astra._runkit.discard_on_failure``: only
the run's own file, never by name). Every drawn value lands in the run's stored config text (the
log's ``config`` attribute) and in the summary. The row's ``status`` is:

- ``ok``: ran ``duration / dt`` frames without a halt.
- ``halt``: halted, with ``halt_check``, ``halt_source``, ``halt_frame`` and ``halt_vehicle``. A
  halt is a result, not a batch failure; its log is kept.
- ``error``: a failure before or outside the simulation, with ``error_type`` and
  ``error_message``, and no log counted (none is left on disk, unless ``discard_log()`` found the
  name no longer the run's own file, which its note in the message says). That covers a
  ``ConfigError`` from a drawn value, a trim that misses D8's residual bound, a log write or close
  failure, a log the strict load refuses, a ``LOG`` halt (a log write failure,
  ``LogHaltError``), and a failed consumer assertion (``RunLengthError``: ``frames_run`` must
  equal ``duration / dt`` without a halt, or the halt's own frame with one; ``astra._runkit``). A
  message naming ``out_dir`` has that prefix replaced by ``<out_dir>``, and one naming trim's
  temporary copy has its random directory replaced by ``<trim_copy>``, so the summary depends on
  neither.

The batch (``run``, Q20). ``out_dir`` is created if missing and must be empty. Runs are spread over
``workers`` processes (default ``os.cpu_count()``, never more than ``runs``) started with the
``spawn`` method, each building its own ``Config`` and ``Simulation``; workers ignore SIGINT, so the
parent alone handles an interrupt. If a worker dies, or the parent is interrupted or fails at any
point up to ``run``'s return (writing the summary and printing included), every worker is killed
and joined, no ``summary.parquet`` is left, and ``BatchError`` (or ``BatchInterrupted``, a
``KeyboardInterrupt``) is raised naming the runs not completed. While workers start and while they
are stopped, SIGINT is deferred to the end of that step, so an interrupt can neither leave a
started worker untracked nor abandon a stop half done (review R-003). An abort deletes nothing
(ADR-0027 item 8: once a run's ``Simulation`` is gone, no tool deletes its log). The stop SIGKILLs
every live worker, so **every run in flight leaves its ``run_<i>.h5.partial``**, not only a dead
worker's run: a killed run's file, which strict ``load`` refuses by name, and which
``load(allow_incomplete=True)`` reads as its certified rows. A run killed after its publication
leaves a complete ``run_<i>.h5``. The error names each leftover ``.partial`` (``partials``). This
changes M5, which removed the log of every run not completed. The logs of completed runs stay, and
each of them is complete and was loaded strictly.

``summary.parquet``: rows sorted by ``run``; columns ``run`` (int64), ``seed`` (uint64, the run's
seed), ``status``, one column per dispersion named by its path (float64, or int64 for ``seed``),
``error_type``, ``error_message``, ``frames_run``, ``halt_check``, ``halt_source``, ``halt_frame``,
``halt_vehicle``, then float64 columns ``health.<counter>`` (``/summary``), ``final.<state>``
(``/final_state``: the state after the last frame) and ``min.<signal>``/``max.<signal>`` for
``signals``, in the order the sorted rows first name them. A column a row has no value for is
null. Neither the summary nor the logs hold wall time (Q8); ``run`` prints the wall time and the
real-time factor to stdout. Its schema metadata is the batch's record, below.

The batch's record (M6 PR 6, docs/plans/m6-pr6-mc-analysis.md D2). ``summary.parquet``'s schema
metadata holds three keys: ``astra.montecarlo.spec``, the spec file's bytes as ``load`` read them;
``astra.montecarlo.config``, the base config's bytes as read; and ``astra.montecarlo.config_name``,
the config's file name only, never a path. Neither holds a path of the machine, so the summary of
one batch still does not depend on ``out_dir`` or the worker count. ``batch_spec`` rebuilds the
``Spec`` from them through ``loads``, which parses them exactly as ``load`` does (UTF-8, then
``yaml.safe_load``). A ``Spec`` changed after loading (``dataclasses.replace``) still records the
bytes it was loaded from; ``rerun``'s draw check refuses such a batch.

Rerun (``rerun``, ``astra mc rerun``; M6 plan Q22). Re-executes one run of a batch into a new,
empty directory from the batch's record alone, so it works with the batch's logs deleted. It
refuses a batch with no record (one written before M6 PR 6), a run not in the summary, and an
``error`` row, which left no log. Before running, it recomputes the run's seed and draws from the
record and checks each against the summary row. It then calls ``run_one`` and compares every
column of the new row with the recorded one, and, when the batch still holds the run's log, the
two logs byte for byte. The record is not a frozen build: plugins are loaded from the current
build, so a rebuild can change the result. The new log's provenance (``git_describe``, the plugin
table) and the row comparison show it when it does. Rerun never touches the batch's own files, and
it refuses an output directory that is the batch or lies inside it (symlinks resolved). A batch
written before M6 PR 11 whose config has a value PyYAML reads differently from the core ran with
PyYAML's reading; its rerun reads the core's, so the rows that value reaches are reported as
differences, and a dispersion of such a value is refused (the plan's D4). A run whose
log cannot be written (a ``LogError``, an ``OSError``, a ``LOG`` halt, or a ``ConfigError`` at
``log.path``, how ADR-0027 reports a log that cannot be created) raises
``RerunWriteError``: nothing was compared, so it is not reported as a row that differs.
"""

from __future__ import annotations

import contextlib
import copy
import dataclasses
import hashlib
import math
import multiprocessing
import multiprocessing.connection
import os
import random
import re
import signal
import struct
import tempfile
import threading
import time
from collections.abc import Callable, Generator
from pathlib import Path
from typing import Any, cast

import pandas as pd  # pyright: ignore[reportMissingTypeStubs]
import pyarrow  # pyright: ignore[reportMissingTypeStubs] -- pyarrow 25.0.1 ships no py.typed
import pyarrow.parquet  # pyright: ignore[reportMissingTypeStubs]
import yaml

from astra import _core, _runkit, _yaml_route, log
from astra._runkit import TrimSetpoint

_pa = cast(Any, pyarrow)
_pq = cast(Any, pyarrow.parquet)

SUMMARY = "summary.parquet"
# summary.parquet's schema metadata: the batch's record (module docstring, The batch's record).
SPEC_KEY = b"astra.montecarlo.spec"
CONFIG_KEY = b"astra.montecarlo.config"
CONFIG_NAME_KEY = b"astra.montecarlo.config_name"
OUT_DIR_PLACEHOLDER = "<out_dir>"
TRIM_COPY_PLACEHOLDER = "<trim_copy>"
MIN_CLIP = 1.0  # standard deviations: at least 68 % of draws are kept (review R-002)

# apply_trim's temporary directory, as it appears in a Config origin inside an error message.
_TRIM_COPY = re.compile(
    re.escape(os.path.join(tempfile.gettempdir(), _runkit.TRIM_DIR_PREFIX)) + r"[^/\s'\"]*"
)

_TOP_KEYS = {"config", "trim", "duration", "runs", "seed", "dispersions", "signals"}
_REQUIRED = {"config", "duration", "runs", "seed", "dispersions"}
_KINDS = ("uniform", "normal")
_FIXED_COLUMNS = (
    "run",
    "seed",
    "status",
    "error_type",
    "error_message",
    "frames_run",
    "halt_check",
    "halt_source",
    "halt_frame",
    "halt_vehicle",
)
_JOIN_S = 30.0  # a worker told to stop that has not exited by then is killed


class SpecError(Exception):
    """The Monte Carlo spec is invalid; the message names the file and the key."""


class RunLengthError(Exception):
    """A run's log fails the consumer assertion on ``frames_run`` (recorded in an ``error`` row)."""


class LogHaltError(Exception):
    """A run halted ``LOG``: its log write failed (recorded in an ``error`` row)."""


class BatchError(Exception):
    """A worker died or the parent failed; the batch wrote no summary. ``partials``: the
    ``run_<i>.h5.partial`` files the runs in flight left (module docstring, The batch)."""

    def __init__(
        self, message: str, incomplete: tuple[int, ...], partials: tuple[Path, ...] = ()
    ) -> None:
        super().__init__(message)
        self.incomplete = incomplete
        self.partials = partials


class BatchInterrupted(KeyboardInterrupt):
    """The batch was interrupted; it wrote no summary. ``partials`` as for ``BatchError``."""

    def __init__(
        self, message: str, incomplete: tuple[int, ...], partials: tuple[Path, ...] = ()
    ) -> None:
        super().__init__(message)
        self.incomplete = incomplete
        self.partials = partials


@dataclasses.dataclass(frozen=True)
class Dispersion:
    """One dispersed leaf. ``uniform``: ``(a, b) = (lo, hi)``. ``normal``: ``a`` is the config's
    value, ``b`` the standard deviation in the leaf's units, ``clip`` in standard deviations.
    ``seed``: no parameters."""

    key: str
    kind: str  # "uniform", "normal" or "seed"
    a: float = 0.0
    b: float = 0.0
    clip: float = 0.0


@dataclasses.dataclass(frozen=True, eq=False)
class Spec:
    """A validated spec. ``config`` is the base config (no log, no transport, no draws applied);
    ``frames`` is ``duration / dt``."""

    name: str
    path: Path
    config_path: Path
    config: dict[str, Any]
    dt: float
    frames: int
    runs: int
    seed: int
    trim: TrimSetpoint | None
    dispersions: tuple[Dispersion, ...]
    signals: tuple[str, ...]
    spec_bytes: bytes  # the spec file and the base config, as read (The batch's record)
    config_bytes: bytes


# --- sampler ------------------------------------------------------------------------------------


def run_seed(master: int, index: int) -> int:
    """The seed of run ``index``: the first 8 bytes (big-endian) of SHA-256 over ``master`` then
    ``index``, each an unsigned 64-bit big-endian integer."""
    digest = hashlib.sha256(struct.pack(">QQ", master, index)).digest()
    return int.from_bytes(digest[:8], "big")


class Rng:
    """``random.Random`` with the in-house transforms of ``astra::random::Rng``."""

    def __init__(self, seed: int) -> None:
        self._random = random.Random(seed)
        self._cached: float | None = None

    def bits53(self) -> int:
        """The top 53 bits of a 64-bit draw."""
        return self._random.getrandbits(64) >> 11

    def uniform01(self) -> float:
        """Uniform on [0, 1); exact in double."""
        return self.bits53() * 2.0**-53

    def uniform(self, lo: float, hi: float) -> float:
        return lo + (hi - lo) * self.uniform01()

    def normal(self) -> float:
        """Standard normal by Box-Muller; the second variate is cached for the next call."""
        if self._cached is not None:
            z, self._cached = self._cached, None
            return z
        u1 = 1.0 - self.uniform01()  # (0, 1]: log(u1) is finite
        u2 = self.uniform01()
        r = math.sqrt(-2.0 * math.log(u1))
        theta = 2.0 * math.pi * u2
        self._cached = r * math.sin(theta)
        return r * math.cos(theta)

    def truncated_normal(self, clip: float) -> float:
        """A standard normal redrawn until ``|z| <= clip``."""
        while True:
            z = self.normal()
            if abs(z) <= clip:
                return z


def draw(spec: Spec, index: int) -> tuple[int, dict[str, float | int]]:
    """Run ``index``'s seed and its drawn values, in spec order."""
    seed = run_seed(spec.seed, index)
    rng = Rng(seed)
    values: dict[str, float | int] = {}
    for dispersion in spec.dispersions:
        if dispersion.kind == "uniform":
            values[dispersion.key] = rng.uniform(dispersion.a, dispersion.b)
        elif dispersion.kind == "normal":
            z = rng.truncated_normal(dispersion.clip)
            values[dispersion.key] = dispersion.a + dispersion.b * z
        else:
            values[dispersion.key] = rng.bits53()
    return seed, values


# --- load ---------------------------------------------------------------------------------------


def load(path: str | Path, *, root: str | Path | None = None) -> Spec:
    """Reads and validates the spec at ``path`` (module docstring). ``config`` is resolved against
    ``root`` (default: the current directory). Raises ``SpecError`` naming the file and the key.
    Both files are read as bytes and kept as read (``Spec.spec_bytes``, ``Spec.config_bytes``),
    which the batch records in its summary (The batch's record)."""
    path = Path(path)
    base = Path.cwd() if root is None else Path(root)
    try:
        spec_bytes = path.read_bytes()
    except OSError as error:
        raise SpecError(f"{path}: cannot read the spec ({error})") from error
    top = _top(path, spec_bytes)
    config_path = base / _runkit.text(path, top["config"], "config", error=SpecError)
    try:
        config_bytes = config_path.read_bytes()
    except OSError as error:
        raise SpecError(f"{path}: config: cannot read {config_path} ({error})") from error
    return _validated(path, spec_bytes, top, config_path, config_bytes)


def loads(spec_bytes: bytes, config_bytes: bytes, *, path: Path, config_path: Path) -> Spec:
    """Validates a spec and its base config given as bytes, as ``load`` reads them; ``path`` and
    ``config_path`` name them in messages and in the ``Config`` origin, and the spec's own
    ``config`` key is checked but not read (``batch_spec`` passes the recorded texts)."""
    top = _top(path, spec_bytes)
    _runkit.text(path, top["config"], "config", error=SpecError)
    return _validated(path, spec_bytes, top, config_path, config_bytes)


def _yaml(path: Path, data: bytes, failure: str, *, config_path: Path | None = None) -> Any:
    """``data`` decoded as UTF-8 and read by ``yaml.safe_load``; with ``config_path`` (the base
    config) through ``_yaml_route.load``, so the dict reads as the core reads the file."""
    try:
        text = data.decode("utf-8")
        if config_path is None:
            return yaml.safe_load(text)
        return _yaml_route.load(
            text, str(config_path), error=lambda message: SpecError(f"{path}: config: {message}")
        )
    except (UnicodeDecodeError, yaml.YAMLError) as error:
        raise SpecError(f"{path}: {failure} ({error})") from error


def _top(path: Path, spec_bytes: bytes) -> dict[str, Any]:
    raw = _yaml(path, spec_bytes, "cannot read the spec")
    return _runkit.closed_map(
        path, raw, "the spec", required=_REQUIRED, optional=_TOP_KEYS, error=SpecError
    )


def _validated(
    path: Path, spec_bytes: bytes, top: dict[str, Any], config_path: Path, config_bytes: bytes
) -> Spec:
    config = _base_config(path, config_path, config_bytes)
    trim = _runkit.trim_setpoint(path, top.get("trim"), error=SpecError)
    runs = _integer(path, top["runs"], "runs", 1, None)
    seed = _integer(path, top["seed"], "seed", 0, 2**64)
    dispersions = _dispersions(path, top["dispersions"], config, trim)

    try:
        sim = _core.Simulation.from_config(
            _core.Config.from_dict(config, origin=str(config_path)), _core.default_plugin_dir()
        )
    except _core.Error as error:
        raise SpecError(f"{path}: the config does not build: {error}") from error
    dt = sim.clock.dt
    registered = {name for name, _value, _unit, _divisor in sim.registry.signals}
    frames = _runkit.frames_of(path, top["duration"], "duration", dt, error=SpecError)
    if frames <= 0:
        raise SpecError(f"{path}: duration: must be greater than zero")
    signals = _signals(path, top.get("signals", []), registered)
    return Spec(
        name=path.stem,
        path=path,
        config_path=config_path,
        config=config,
        dt=dt,
        frames=frames,
        runs=runs,
        seed=seed,
        trim=trim,
        dispersions=dispersions,
        signals=signals,
        spec_bytes=spec_bytes,
        config_bytes=config_bytes,
    )


def _base_config(path: Path, config_path: Path, config_bytes: bytes) -> dict[str, Any]:
    data = _yaml(path, config_bytes, f"config: cannot read {config_path}", config_path=config_path)
    config = _runkit.closed_map(
        path, data, f"config {config_path}", required=set(), optional=None, error=SpecError
    )
    if "transport" in config:
        raise SpecError(
            f"{path}: config: {config_path} has a transport block; the Monte Carlo runs open "
            "loop (docs/plans/m5-tooling.md Q17)"
        )
    if "log" in config:
        raise SpecError(
            f"{path}: config: {config_path} has a log block; the Monte Carlo writes each run's "
            "log itself"
        )
    return config


def _integer(path: Path, value: Any, where: str, lo: int, hi: int | None) -> int:
    """An integer in ``[lo, hi)`` (``hi`` ``None``: no upper bound)."""
    if isinstance(value, bool) or not isinstance(value, int):
        raise SpecError(f"{path}: {where}: must be an integer, got {value!r}")
    if value < lo or (hi is not None and value >= hi):
        bound = "inf" if hi is None else str(hi)
        raise SpecError(f"{path}: {where}: {value!r} is outside [{lo}, {bound})")
    return value


def _dispersions(
    path: Path, value: Any, config: dict[str, Any], trim: TrimSetpoint | None
) -> tuple[Dispersion, ...]:
    mapping = _runkit.closed_map(
        path, value, "dispersions", required=set(), optional=None, error=SpecError
    )
    if not mapping:
        raise SpecError(f"{path}: dispersions: must name at least one leaf")
    dispersions: list[Dispersion] = []
    for key, distribution in mapping.items():
        where = f"dispersions.{key}"
        if key in _FIXED_COLUMNS:
            raise SpecError(f"{path}: {where}: {key!r} is a summary column name")
        if trim is not None:
            reason = _runkit.trim_owned_reason(key)
            if reason is not None:
                raise SpecError(
                    f"{path}: {where}: {reason}, so with trim set this dispersion would be "
                    "discarded or leave the run untrimmed"
                )
        try:
            base = _runkit.numeric_leaf(config, key)
        except _runkit.LeafError as error:
            raise SpecError(f"{path}: {where}: {error}") from None
        dispersions.append(_distribution(path, where, key, distribution, base))
    return tuple(dispersions)


def _distribution(path: Path, where: str, key: str, value: Any, base: int | float) -> Dispersion:
    if value == "seed":
        if isinstance(base, bool) or not isinstance(base, int):
            raise SpecError(
                f"{path}: {where}: seed disperses an integer plugin seed, but the config's value "
                f"is {base!r}"
            )
        return Dispersion(key, "seed")
    spec = _runkit.closed_map(
        path, value, where, required=set(), optional=set(_KINDS), error=SpecError
    )
    if len(spec) != 1:
        raise SpecError(f"{path}: {where}: needs exactly one of uniform, normal (or: seed)")
    if "uniform" in spec:
        keys = {"lo", "hi"}
        bounds = _runkit.closed_map(
            path, spec["uniform"], f"{where}.uniform", required=keys, optional=keys, error=SpecError
        )
        lo = _runkit.number(path, bounds["lo"], f"{where}.uniform.lo", error=SpecError)
        hi = _runkit.number(path, bounds["hi"], f"{where}.uniform.hi", error=SpecError)
        if not lo < hi:
            raise SpecError(f"{path}: {where}.uniform: needs lo < hi, got [{lo!r}, {hi!r}]")
        return Dispersion(key, "uniform", lo, hi)
    keys = {"sigma", "mode", "clip"}
    normal = _runkit.closed_map(
        path, spec["normal"], f"{where}.normal", required=keys, optional=keys, error=SpecError
    )
    sigma = _runkit.number(path, normal["sigma"], f"{where}.normal.sigma", error=SpecError)
    clip = _runkit.number(path, normal["clip"], f"{where}.normal.clip", error=SpecError)
    mode = normal["mode"]
    if sigma <= 0.0:
        raise SpecError(f"{path}: {where}.normal.sigma: must be greater than zero")
    if clip < MIN_CLIP:
        # A redraw keeps a fraction erf(clip / sqrt 2) of draws; a tiny clip would never finish.
        raise SpecError(
            f"{path}: {where}.normal.clip: {clip!r} is below {MIN_CLIP:g} standard deviation"
        )
    center = float(base)
    if mode == "absolute":
        scale = sigma
    elif mode == "relative":
        if center == 0.0:
            raise SpecError(
                f"{path}: {where}.normal.mode: relative needs a nonzero config value, got {base!r}"
            )
        scale = sigma * abs(center)
    else:
        raise SpecError(f"{path}: {where}.normal.mode: must be absolute or relative, got {mode!r}")
    return Dispersion(key, "normal", center, scale, clip)


def _signals(path: Path, value: Any, registered: set[str]) -> tuple[str, ...]:
    if not isinstance(value, list):
        raise SpecError(f"{path}: signals: must be a list")
    signals: list[str] = []
    for i, item in enumerate(cast(list[Any], value)):
        name = _runkit.text(path, item, f"signals[{i}]", error=SpecError)
        if name not in registered:
            raise SpecError(f"{path}: signals[{i}]: {name!r} is not a registered signal")
        if name in signals:
            raise SpecError(f"{path}: signals[{i}]: {name!r} is listed twice")
        signals.append(name)
    return tuple(signals)


# --- one run ------------------------------------------------------------------------------------


def log_name(index: int) -> str:
    return f"run_{index:04d}.h5"


def run_one(
    spec: Spec,
    index: int,
    out_dir: str | Path,
    *,
    reraise: Callable[[Exception], bool] | None = None,
) -> dict[str, Any]:
    """Draws, trims (if set), runs and reads back run ``index`` with its log in ``out_dir``, and
    returns its summary row (module docstring, A run). Only an ``ok`` or ``halt`` row leaves a
    log. An exception for which ``reraise`` returns true propagates instead of becoming an
    ``error`` row (its log already discarded); ``rerun`` uses it for write failures."""

    def accept(run_log: log.RunLog) -> None:
        # While the Simulation is alive, so a rejected log is discarded through discard_log().
        problems = _runkit.run_length_failures(run_log, spec.frames)
        if problems:
            raise RunLengthError("; ".join(problems))
        halt = run_log.halt
        if halt is not None and halt.check == _core.HealthCheck.LOG.name:
            raise LogHaltError(
                f"the run halted LOG ({halt.source}) at frame {halt.frame}: its log write failed"
            )

    out_dir = Path(out_dir).resolve()  # absolute, so _error_row replaces one exact prefix
    seed, values = draw(spec, index)
    row: dict[str, Any] = {"run": index, "seed": seed, "status": "ok", **values}
    log_path = out_dir / log_name(index)
    data = copy.deepcopy(spec.config)
    for key, value in values.items():
        _runkit.set_numeric_leaf(data, key, value)
    try:
        if spec.trim is not None:
            _runkit.apply_trim(data, spec.trim, origin=spec.path, config_name=spec.config_path.name)
        run_log = _runkit.run_logged(
            data,
            log_path,
            origin=f"{spec.path} run {index}",
            plugin_dir=None,
            # Late-bound lambdas: a test replaces _drive or _close_log on this module.
            drive=lambda sim: _drive(sim, spec.frames),
            close=lambda sim: _close_log(sim),
            accept=accept,
        )
    except Exception as error:
        if reraise is not None and reraise(error):
            raise
        return _error_row(row, error, out_dir)
    halt = run_log.halt
    row["frames_run"] = run_log.frames_run
    if halt is not None:
        row.update(
            status="halt",
            halt_check=halt.check,
            halt_source=halt.source,
            halt_frame=halt.frame,
            halt_vehicle=halt.vehicle_index,
        )
    for name, value in sorted(run_log.health.items()):
        row[name] = value
    for state in run_log.final_state.values():
        for name, value in state.items():
            row[f"final.{name}"] = float(value)
    minimum = cast(Any, run_log.minimum)
    maximum = cast(Any, run_log.maximum)
    for name in spec.signals:
        row[f"min.{name}"] = float(minimum[name])
        row[f"max.{name}"] = float(maximum[name])
    return row


def _drive(sim: _core.Simulation, frames: int) -> None:
    sim.run(frames)  # a halt ends the run early; it is in the log


def _close_log(sim: _core.Simulation) -> None:
    sim.close_log()


def _error_row(row: dict[str, Any], error: Exception, out_dir: Path) -> dict[str, Any]:
    """``out_dir`` is absolute (``run_one`` resolves it), so exactly one prefix is replaced, and a
    relative name cannot rewrite unrelated text. The trim copy's random temporary directory is
    replaced too (review R-001)."""
    message = "\n".join([str(error), *getattr(error, "__notes__", [])])
    message = message.replace(str(out_dir), OUT_DIR_PLACEHOLDER)
    message = _TRIM_COPY.sub(TRIM_COPY_PLACEHOLDER, message)
    row.update(status="error", error_type=type(error).__name__, error_message=message)
    return row


# --- the batch ----------------------------------------------------------------------------------


def run(spec: Spec | str | Path, out_dir: str | Path, workers: int | None = None) -> pd.DataFrame:
    """Runs every run of ``spec`` (a ``Spec`` or a spec path) into ``out_dir`` and writes
    ``summary.parquet`` (module docstring, The batch). Returns the summary. Raises
    ``BatchError``/``BatchInterrupted`` naming the runs not completed when a worker dies, the
    parent fails, or the batch is interrupted; no summary is then left."""
    spec = spec if isinstance(spec, Spec) else load(spec)
    out_dir = Path(out_dir).resolve()
    count = _worker_count(workers, spec.runs)
    out_dir.mkdir(parents=True, exist_ok=True)
    if any(out_dir.iterdir()):
        raise FileExistsError(f"{out_dir}: the batch's output directory is not empty")
    started = time.perf_counter()
    rows = _run_pool(spec, out_dir, count)  # every worker is stopped when it returns or raises
    path = out_dir / SUMMARY
    try:
        wall = time.perf_counter() - started
        ordered = [rows[index] for index in range(spec.runs)]
        summary = _write_summary(spec, ordered, path)
        simulated = sum(
            row["frames_run"] * spec.dt for row in ordered if row.get("frames_run") is not None
        )
        print(
            f"astra.montecarlo: {spec.name}: {spec.runs} runs on {count} workers in {wall:.2f} "
            f"s wall, {simulated:.1f} s simulated, real-time factor {simulated / wall:.1f}"
        )
    except BaseException as error:
        # Every run completed and its log stays; the summary is withdrawn (review R-004).
        with _sigint_deferred(reraise=False) as pending:
            path.unlink(missing_ok=True)
        raise _batch_error(spec, (), error, interrupted=bool(pending)) from error
    return summary


def _worker_count(workers: int | None, runs: int) -> int:
    if workers is None:
        workers = os.cpu_count() or 1
    checked: Any = workers  # callers are untyped Python; check the type at run time too
    if isinstance(checked, bool) or not isinstance(checked, int) or workers < 1:
        raise ValueError(f"workers must be an integer >= 1, got {workers!r}")
    return min(workers, runs)


@contextlib.contextmanager
def _sigint_deferred(*, reraise: bool = True) -> Generator[list[int]]:
    """Holds SIGINT until the block ends, then re-raises it under the previous handler
    (``reraise``) or only reports it in the yielded list. A no-op off the main thread, where
    Python delivers no signal."""
    pending: list[int] = []
    if threading.current_thread() is not threading.main_thread():
        yield pending
        return
    previous = signal.signal(signal.SIGINT, lambda signum, _frame: pending.append(signum))
    try:
        yield pending
    finally:
        signal.signal(signal.SIGINT, previous)
        if pending and reraise:
            signal.raise_signal(signal.SIGINT)


def _worker(conn: multiprocessing.connection.Connection, spec: Spec, out_dir: Path) -> None:
    """A pool process: runs each index it receives and sends back its row; stops on ``None``."""
    signal.signal(signal.SIGINT, signal.SIG_IGN)  # the parent alone handles an interrupt
    while True:
        try:
            index = conn.recv()
        except EOFError:
            return  # the parent is gone
        if index is None:
            return
        conn.send(run_one(spec, cast(int, index), out_dir))


class _WorkerDiedError(Exception):
    pass


def _run_pool(spec: Spec, out_dir: Path, count: int) -> dict[int, dict[str, Any]]:
    """Every run's row, by index. Returns only after every worker has exited and been joined;
    raises (``_batch_error``) after killing them."""
    context = multiprocessing.get_context("spawn")
    processes: list[Any] = []
    conns: list[multiprocessing.connection.Connection] = []
    rows: dict[int, dict[str, Any]] = {}
    pending = list(range(spec.runs - 1, -1, -1))  # popped from the end: run 0 first
    assigned: dict[int, int] = {}  # worker -> run
    try:
        with _sigint_deferred():  # a started worker is always in processes (review R-003)
            for _ in range(count):
                parent, child = context.Pipe()
                process = context.Process(
                    target=_worker, args=(child, spec, out_dir), name="astra.montecarlo worker"
                )
                conns.append(parent)
                processes.append(process)
                process.start()
                child.close()
        for worker in range(count):
            _assign(worker, conns, pending, assigned)
        while len(rows) < spec.runs:
            sentinels = [cast(int, process.sentinel) for process in processes]
            ready = multiprocessing.connection.wait([*conns, *sentinels])
            for worker, conn in enumerate(conns):
                if conn in ready:
                    try:
                        row = cast(dict[str, Any], conn.recv())
                        rows[cast(int, row["run"])] = row
                        del assigned[worker]
                        _assign(worker, conns, pending, assigned)
                    except (EOFError, OSError):
                        # A killed peer shows as EOF or as a reset socket (ECONNRESET, EPIPE).
                        raise _died(processes, worker, assigned) from None
            for worker, sentinel in enumerate(sentinels):
                if sentinel in ready:
                    raise _died(processes, worker, assigned)
        for conn in conns:
            conn.send(None)
        for process in processes:
            process.join(_JOIN_S)
        _stop(processes, conns)  # a worker that has not exited by now is killed
    except BaseException as error:
        _abort(processes, conns, spec, out_dir, rows, error)
    return rows


def _died(processes: list[Any], worker: int, assigned: dict[int, int]) -> _WorkerDiedError:
    """The error for a worker whose pipe closed or whose process ended before it was told to
    stop. Its pipe can close a moment before its exit status is ready, so it is joined briefly."""
    process = processes[worker]
    process.join(_JOIN_S)
    doing = f"during run {assigned[worker]}" if worker in assigned else "while idle"
    return _WorkerDiedError(f"worker {worker} died (exit code {process.exitcode}) {doing}")


def _assign(
    worker: int,
    conns: list[multiprocessing.connection.Connection],
    pending: list[int],
    assigned: dict[int, int],
) -> None:
    if pending:
        index = pending.pop()
        assigned[worker] = index
        conns[worker].send(index)


def _stop(processes: list[Any], conns: list[multiprocessing.connection.Connection]) -> None:
    """Kills and joins every worker still alive, closes every pipe, and empties both lists, with
    SIGINT held until all of that is done (review R-003); safe to call twice."""
    with _sigint_deferred():
        for process in processes:
            if process.is_alive():  # False for a worker whose start() failed
                process.kill()
        for process in processes:
            if process.pid is not None:
                process.join()
            process.close()
        processes.clear()
        for conn in conns:
            conn.close()
        conns.clear()


def _abort(
    processes: list[Any],
    conns: list[multiprocessing.connection.Connection],
    spec: Spec,
    out_dir: Path,
    rows: dict[int, dict[str, Any]],
    error: BaseException,
) -> None:
    """Kills the pool and raises naming the runs not completed and the ``.partial`` files the runs
    in flight left. Deletes nothing (module docstring, The batch)."""
    incomplete = tuple(index for index in range(spec.runs) if index not in rows)
    with _sigint_deferred(reraise=False) as pending:
        _stop(processes, conns)
    partials = tuple(
        path
        for index in incomplete
        if (path := out_dir / f"{log_name(index)}{log.PARTIAL_SUFFIX}").exists()
    )
    interrupted = bool(pending)
    raise _batch_error(
        spec, incomplete, error, interrupted=interrupted, partials=partials
    ) from error


def _batch_error(
    spec: Spec,
    incomplete: tuple[int, ...],
    error: BaseException,
    *,
    interrupted: bool,
    partials: tuple[Path, ...] = (),
) -> BatchError | BatchInterrupted:
    runs = ", ".join(str(index) for index in incomplete) or "none"
    left = (
        f"; left in place, not published: {', '.join(path.name for path in partials)}"
        if partials
        else ""
    )
    if interrupted or isinstance(error, KeyboardInterrupt):
        return BatchInterrupted(
            f"{spec.path}: the batch was interrupted; runs not completed: {runs}{left}; no "
            "summary was written",
            incomplete,
            partials,
        )
    return BatchError(
        f"{spec.path}: the batch failed ({type(error).__name__}: {error}); runs not completed: "
        f"{runs}{left}; no summary was written",
        incomplete,
        partials,
    )


# --- summary ------------------------------------------------------------------------------------


def _write_summary(spec: Spec, rows: list[dict[str, Any]], path: Path) -> pd.DataFrame:
    """Builds the summary with an explicit schema and its DataFrame, then writes it under a
    temporary name and renames it: the file carries the summary's name only once complete."""
    fields: list[tuple[str, Any]] = [
        ("run", _pa.int64()),
        ("seed", _pa.uint64()),
        ("status", _pa.string()),
    ]
    for dispersion in spec.dispersions:
        kind = _pa.int64() if dispersion.kind == "seed" else _pa.float64()
        fields.append((dispersion.key, kind))
    fields += [
        ("error_type", _pa.string()),
        ("error_message", _pa.string()),
        ("frames_run", _pa.int64()),
        ("halt_check", _pa.string()),
        ("halt_source", _pa.string()),
        ("halt_frame", _pa.int64()),
        ("halt_vehicle", _pa.int64()),
    ]
    known = {name for name, _type in fields}
    for row in rows:
        for name in row:
            if name not in known:
                known.add(name)
                fields.append((name, _pa.float64()))
    schema = _pa.schema(fields, metadata=record(spec))
    columns = {name: [row.get(name) for row in rows] for name, _type in fields}
    table = _pa.Table.from_pydict(columns, schema=schema)
    summary = cast(pd.DataFrame, table.to_pandas())
    partial = path.with_name(f".{path.name}.partial")
    try:
        _pq.write_table(table, partial)
        partial.replace(path)
    finally:
        partial.unlink(missing_ok=True)
    return summary


# --- the batch's record and rerun (M6 PR 6, docs/plans/m6-pr6-mc-analysis.md D2) ------------------


class RerunError(Exception):
    """``rerun`` refused the run before executing it; the message names the summary and why."""


class RerunWriteError(Exception):
    """``rerun`` could not write the run's log (a ``LogError``, an ``OSError``, a ``LOG`` halt, or a
    ``ConfigError`` at ``log.path``), so nothing was compared; the run's own file is already
    discarded (review R-004)."""


# The core's ConfigError wording (errors.cpp describe_config): "config '<origin>' at '<key>': ...".
# ADR-0027 item 1 makes any failure while the log is constructed (its create, its provenance write)
# a ConfigError at log.path; in a rerun's own empty directory that is a failed write.
_AT_LOG_PATH = " at 'log.path': "


def _write_failure(error: Exception) -> bool:
    """What a rerun reports as a failed write rather than as a row that differs (review R-004):
    a ``LogError``, an ``OSError``, a ``LOG`` halt, or a ``ConfigError`` at ``log.path``."""
    if isinstance(error, _core.LogError | OSError | LogHaltError):
        return True
    return isinstance(error, _core.ConfigError) and _AT_LOG_PATH in str(error)


def inside(path: Path, directory: Path) -> bool:
    """Whether ``path`` is ``directory`` or lies under it, after resolving symlinks and ``..``
    (review R-002: no tool writes into the batch it reads)."""
    resolved = path.resolve()
    base = directory.resolve()
    return resolved == base or base in resolved.parents


@dataclasses.dataclass(frozen=True)
class RerunResult:
    """What ``rerun`` did. ``recorded`` is the summary's row and ``row`` the rerun's (``run_one``);
    ``differences`` names every column where they differ (null equals null, NaN equals NaN).
    ``log_path`` is the rerun's log, or ``None`` when the run left none. ``log_identical`` compares
    it with the batch's own log byte for byte, or is ``None`` when either is missing."""

    run: int
    out_dir: Path
    log_path: Path | None
    recorded: dict[str, Any]
    row: dict[str, Any]
    differences: tuple[str, ...]
    log_identical: bool | None

    @property
    def reproduced(self) -> bool:
        return not self.differences and self.log_identical is not False


def record(spec: Spec) -> dict[bytes, bytes]:
    """The batch's record of ``spec``: summary.parquet's schema metadata (The batch's record)."""
    return {
        SPEC_KEY: spec.spec_bytes,
        CONFIG_KEY: spec.config_bytes,
        CONFIG_NAME_KEY: spec.config_path.name.encode("utf-8"),
    }


def batch_spec(batch_dir: str | Path) -> Spec:
    """The spec a batch recorded in its summary, validated again by ``loads`` against the current
    build. Raises ``SpecError`` when the summary cannot be read or records no spec (a batch from
    before M6 PR 6)."""
    summary = Path(batch_dir) / SUMMARY
    try:
        schema: Any = _pq.read_schema(summary)
    except (OSError, ValueError) as error:  # pyarrow's ArrowInvalid is a ValueError
        raise SpecError(f"{summary}: cannot read the summary ({error})") from error
    metadata = cast(dict[bytes, bytes], schema.metadata or {})
    keys = (SPEC_KEY, CONFIG_KEY, CONFIG_NAME_KEY)
    missing = [key.decode() for key in keys if key not in metadata]
    if missing:
        raise SpecError(
            f"{summary}: the batch records no spec ({', '.join(missing)} missing): it was written "
            "before M6 PR 6, so its runs cannot be rerun; run the batch again to record one"
        )
    name = metadata[CONFIG_NAME_KEY].decode("utf-8", errors="replace")
    if not name or "/" in name or name in (".", ".."):
        raise SpecError(f"{summary}: the recorded config name {name!r} is not a file name")
    return loads(metadata[SPEC_KEY], metadata[CONFIG_KEY], path=summary, config_path=Path(name))


def rerun(batch_dir: str | Path, index: int, out_dir: str | Path) -> RerunResult:
    """Re-executes run ``index`` of the batch in ``batch_dir`` into ``out_dir`` from the batch's
    record (module docstring, Rerun). Raises ``SpecError`` (no record, or a record the current
    build refuses), ``RerunError`` (no such run, an ``error`` row, or a seed or draw that differs
    from the summary) and ``FileExistsError`` (``out_dir`` not empty), each before anything runs."""
    batch_dir = Path(batch_dir)
    summary = batch_dir / SUMMARY
    spec = batch_spec(batch_dir)
    recorded = _recorded_row(summary, index)
    if recorded.get("status") == "error":
        raise RerunError(
            f"{summary}: run {index} is an error row ({recorded.get('error_type')}): it left no "
            "log to rerun"
        )
    seed, values = draw(spec, index)
    for name, value in {"seed": seed, **values}.items():
        if name not in recorded or not _same(recorded[name], value):
            raise RerunError(
                f"{summary}: run {index}: {name}: the summary records {recorded.get(name)!r}, but "
                f"the recorded spec draws {value!r}"
            )
    out_dir = Path(out_dir)
    if inside(out_dir, batch_dir):
        raise RerunError(
            f"{out_dir}: is the batch {batch_dir} or inside it; rerun never writes into a batch"
        )
    out_dir.mkdir(parents=True, exist_ok=True)
    if any(out_dir.iterdir()):
        raise FileExistsError(f"{out_dir}: the rerun's output directory is not empty")
    try:
        row = run_one(spec, index, out_dir, reraise=_write_failure)
    except Exception as error:
        if not _write_failure(error):
            raise
        raise RerunWriteError(
            f"run {index}: the rerun's log could not be written ({type(error).__name__}: "
            f"{error}); it was not compared"
        ) from error
    columns = [*recorded, *(name for name in row if name not in recorded)]
    differences = tuple(name for name in columns if not _same(recorded.get(name), row.get(name)))
    new_log = out_dir / log_name(index)
    original = batch_dir / log_name(index)
    log_path = new_log if new_log.exists() else None
    identical = None
    if log_path is not None and original.exists():
        identical = original.read_bytes() == log_path.read_bytes()
    return RerunResult(index, out_dir, log_path, recorded, row, differences, identical)


def _recorded_row(summary: Path, index: int) -> dict[str, Any]:
    try:
        rows = cast(list[dict[str, Any]], _pq.read_table(summary).to_pylist())
    except (OSError, ValueError) as error:
        raise SpecError(f"{summary}: cannot read the summary ({error})") from error
    for row in rows:
        if row.get("run") == index:
            return row
    raise RerunError(f"{summary}: run {index} is not in the summary ({len(rows)} rows)")


def _same(recorded: Any, rerun: Any) -> bool:
    """Exact equality, with null equal to null and NaN equal to NaN."""
    if recorded is None or rerun is None:
        return recorded is None and rerun is None
    both_nan = (
        isinstance(recorded, float)
        and isinstance(rerun, float)
        and math.isnan(recorded)
        and math.isnan(rerun)
    )
    return both_nan or bool(recorded == rerun)
