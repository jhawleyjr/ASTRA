"""astra.cli: the ``astra`` console script (M6 PR 3, docs/plans/m6-pr3-cli.md; docs/architecture.md
§12, §15 and §18).

Subcommands: ``run``, ``trim``, ``plot`` (M6 PR 5, docs/plans/m6-pr5-plots.md), ``mc analyze`` and
``mc rerun`` (M6 PR 6). ``report`` and ``mc size`` come with their tools (M6 plan).
``main(argv)`` returns the exit code and changes no signal handler; ``console()`` is the script.

``astra run <config> [--strict] [--log PATH]``
    Runs ``sim.frames`` frames open loop (``Simulation.run()``), closes the log with ``close_log()``
    (ADR-0027) and prints §15's post-run summary, plus wall time and real-time factor, to stdout.
    ``--log PATH`` adds ``log: { path: PATH }`` by appending it to the config file's own text, which
    is never converted through a Python dict. It exits with the first matching code:

    - 64 (``EX_USAGE``): a usage error, including ``--log`` for a config that already has a ``log``
      block (D2), or a config whose text the ``log`` block cannot be appended to.
    - 65 (``EX_DATAERR``): a ``ConfigError`` or ``PluginError``. That includes a ``transport``
      block, refused until M10 before anything is built, and an existing ``log.path`` or
      ``.partial``. It also includes any failure while the log is constructed, which ADR-0027 item
      1 makes a ``ConfigError`` naming ``log.path`` (D8).
    - 74 (``EX_IOERR``): the log could not be written, closed or published (a ``LogError``), or the
      summary could not be written to stdout (D4).
    - 1: a halt of any check. Under ``run()`` a LOG halt comes only from a failed drain write, and
      ``close_log()`` then raises that failure, so a LOG halt exits 74.
    - 2: ``--strict``, no halt, and a nonzero warn counter (``_runkit.WARN_COUNTERS``;
      ``health.max_q_drift`` is a drift, not a count).
    - 0: otherwise.

    Also in §15's table: 70 (``EX_SOFTWARE``) for an internal error, a
    defect, with its traceback on stderr (D3), never 1, which would read as a halt. A strict load of
    the log ``close_log()`` just published that raises is one.

    Streams. stdout holds only the summary, written once the run has executed, in one write and one
    flush after the whole text is built. It is never printed piecemeal. stderr holds the reason for
    every nonzero code, each reason starting ``astra run:`` (a core message quoted in one can carry
    HDF5's own lines); exit 0 writes nothing there. The summary, one ``key: value`` per line:

        config: <path>
        frames: <run> of <requested>
        halt: none | <CHECK> source=<source> frame=<frame> vehicle=<index>
        health.<name>: <repr>                     (every health.* signal, sorted)
        log: <absolute path> | none | not published (see stderr)
        signals:                                  (a published log only)
          <name> <min repr> <max repr> <unit>     (every logged signal, file order)
        wall time: <seconds, 3 decimals> s
        real-time factor: <frames x dt / wall time, 1 decimal>

    Wall time is measured around ``run()`` and ``close_log()`` and is printed, never stored
    (ADR-0025).

    Files (D1). ``astra run`` deletes no log file. A clean run, a ``--strict`` warning and a halt
    publish ``log.path``. On a 74 every file is as ``close_log()`` left it, and stderr names
    ``log.path`` and its ``.partial``. A later run with that path is refused (65) while the
    ``.partial`` exists. A publication refused for any reason but the identity check leaves a
    ``.partial`` holding ``complete = 1``, which ``astra.log.load(path, allow_incomplete=True)``
    reads and ``mv -n`` publishes.

``astra trim <config> [--airspeed V] [--altitude H]``
    Straight-and-level trim (``astra.trim``) at true airspeed V (m/s) and altitude H (m). Each
    defaults to the config's own initial state, |v_b| and the geodetic ``p_n[2]`` (D6). It prints
    YAML to stdout: the setpoint, ``elevator``, ``throttle``, ``alpha``, the 6-vector
    ``residual``, and the ``initial.state`` and ``vehicle.initial_controls.controls`` fragment
    ``_runkit.apply_trim`` writes (``euler_nb`` replaces any ``q_nb``). Floats are written by
    ``yaml.safe_dump``, whose text reads back exactly. Codes: 64 a usage error (a non-finite value,
    or an airspeed not above zero); 65 a config error (including a document that is not a map, a
    ``transport`` block, a flat Earth, a zero default airspeed, and a file ``astra.trim``'s own
    YAML 1.1 read refuses); 1 not trimmed (``astra.trim``'s non-convergence, or a residual above
    ``_runkit.TRIM_RESIDUAL_BOUND``); 74 stdout could not be written; 70 an internal error,
    including any other ``ValueError`` or ``RuntimeError`` from a trim; 0 trimmed.

``astra plot <log> <package> [--out DIR] [--format svg|png] [--html]``
    Renders a plot package (``astra.plot``) against a run log, read strictly with
    ``astra.log.load``. ``<package>`` is a file if it contains ``/`` or ends in ``.yaml`` or
    ``.yml``, and otherwise a framework package name (``python/astra/plots/``). It writes
    ``<package>-<figure>.svg`` (or ``.png``) per figure into DIR (default ``.``, created if
    missing), plus ``<package>.html`` with ``--html``, never overwriting a file, and prints each
    path written, one per line. Codes: 64 a usage error; 65 the package is refused
    (``PlotPackageError``), the log is refused (``LogFormatError`` or ``IncompleteLogError``, a
    missing file included), or an output file already exists (as ``astra run`` refuses an
    existing ``log.path``); 74 a write that fails, or stdout could not be written; 70 an internal
    error; 0 written. Nothing is written for a 65; on a 74 the files this run created are deleted.

``astra mc analyze <batch> [--criterion EXPR] [--metric COLUMN] [--signal NAME]... [--q Q]
[--out DIR] [--format svg|png]`` (M6 PR 6, docs/plans/m6-pr6-mc-analysis.md D9)
    Analyzes a batch with ``astra.mc_analysis``: the outcome counts under the criterion (default
    ``status == halt``), the KS ranking with Benjamini-Hochberg at Q (default 0.05), Spearman and
    PRCC against the metric when one is given, and the tree's rules, printed to stdout with the
    paths written. The figures (``mc-hist``, ``mc-scatter``, one ``mc-envelope-<signal>`` per
    ``--signal``) go to DIR (default ``.``), never overwriting a file. Codes: 64 a usage error
    (including a Q outside (0, 1)); 65 a refused batch, criterion, metric, signal or log, or an
    existing output file; 74 a failed write or stdout; 70 an internal error; 0 written. Nothing is
    written for a 65; on a 74 the files this command created are deleted.

``astra mc rerun <batch> <run> --out DIR [--package P]...`` (M6 plan Q22, D2 and D9)
    Re-executes one run of a batch from the batch's record (``astra.montecarlo.rerun``) into DIR,
    which is created and must be empty, and prints the comparison: the row, column by column, and
    the log's bytes when the batch still holds its log. Plugins come from the current build, so a
    rebuild can change the result. It then renders each package (default ``flight`` and
    ``health``) from the new log, never overwriting a file. Codes: 0 reproduced; 1 not reproduced
    (the log is kept); 64 a usage error; 65 a refused batch (including one written before M6 PR 6,
    which records no spec), run (not in the summary, an ``error`` row, or draws that differ from
    the summary), package or output directory; 74 a failed write; 70 an internal error. The
    comparison is printed before rendering; a render that then fails leaves the log, names the
    package on stderr and exits 65 (refused) or 74 (write), unless the run was not reproduced,
    whose 1 takes precedence. A run whose log cannot be written exits 74 with nothing compared, and
    a DIR that is the batch or lies inside it is refused (65); ``mc analyze`` refuses such a DIR
    too.
"""

from __future__ import annotations

import argparse
import json
import math
import os
import signal
import sys
import time
import traceback
from collections.abc import Sequence
from pathlib import Path
from typing import Any, NoReturn, cast

from astra import _core, _runkit

HALTED = 1
WARNED = 2
NOT_TRIMMED = 1
NOT_REPRODUCED = 1
RERUN_PACKAGES = ("flight", "health")  # astra mc rerun's default framework packages
EX_USAGE = 64
EX_DATAERR = 65
EX_SOFTWARE = 70
EX_IOERR = 74

# The core's ConfigError wording (errors.cpp): "config '<origin>' at '<key>': <message>".
_TRANSPORT_REFUSAL = (
    "config '{}' at 'transport': a transport block is refused until M10 (closed loop)"
)
# RunLog's LogError text (errors.cpp describe_log, run_log.cpp publish): "run log '<path>': the
# publication failed: <reason>". Every refusal but the identity check keeps a .partial with
# complete = 1 (ADR-0027 item 3).
_PUBLICATION = "': the publication failed: "
_IDENTITY = "the .partial is no longer this run's file"
# astra.trim's two expected failures (trim.py, trim_straight_and_level): its refusal of a flat Earth
# and scipy's non-convergence. Every other ValueError or RuntimeError from a trim is a defect (70).
_TRIM_FLAT_EARTH = "trim_straight_and_level: 'physical.earth' is "
_TRIM_NOT_CONVERGED = "trim_straight_and_level did not converge: "


class _Parser(argparse.ArgumentParser):
    """argparse, but a usage error exits 64 (``EX_USAGE``), not argparse's 2."""

    def error(self, message: str) -> NoReturn:
        self.print_usage(sys.stderr)
        self.exit(EX_USAGE, f"{self.prog}: error: {message}\n")


def _finite(text: str) -> float:
    value = float(text)
    if not math.isfinite(value):
        raise ValueError(text)
    return value


def _rate(text: str) -> float:
    value = float(text)
    if not 0.0 < value < 1.0:
        raise ValueError(text)
    return value


def _index(text: str) -> int:
    value = int(text)
    if value < 0:
        raise ValueError(text)
    return value


def _parser() -> tuple[_Parser, dict[str, _Parser]]:
    parser = _Parser(prog="astra", description="ASTRA: run and analyze 6-DoF simulations.")
    commands = parser.add_subparsers(dest="command", required=True, metavar="command")
    run = commands.add_parser(
        "run",
        help="run a config open loop",
        description="Run a config open loop, close its log and print the post-run summary. "
        "Exit codes: 64 usage, 65 config, 74 log or stdout I/O, 1 halt, 2 --strict with a "
        "nonzero warn counter, 0 otherwise; 70 an internal error.",
    )
    run.add_argument("config", help="the YAML config")
    run.add_argument(
        "--strict", action="store_true", help="exit 2 when a warn counter is nonzero (no halt)"
    )
    run.add_argument("--log", metavar="PATH", help="write the run log to PATH (adds a log block)")
    trim = commands.add_parser(
        "trim",
        help="trim a config straight and level",
        description="Trim a config straight and level and print the result as YAML. Exit codes: "
        "64 usage, 65 config, 1 not trimmed, 74 stdout I/O, 0 trimmed; 70 an internal error.",
    )
    trim.add_argument("config", help="the YAML config")
    trim.add_argument(
        "--airspeed",
        type=_finite,
        metavar="V",
        help="true airspeed, m/s (default: |v_b| of the config's initial state)",
    )
    trim.add_argument(
        "--altitude",
        type=_finite,
        metavar="H",
        help="altitude, m (default: p_n[2] of the config's initial state)",
    )
    plot = commands.add_parser(
        "plot",
        help="plot a run log with a plot package",
        description="Render a plot package against a run log. Exit codes: 64 usage, 65 a refused "
        "package or log or an existing output file, 74 a failed write, 0 written; 70 an "
        "internal error.",
    )
    plot.add_argument("log", help="the run log (HDF5)")
    plot.add_argument(
        "package",
        help="a package file (a path containing / or ending in .yaml or .yml) or a framework "
        "package name",
    )
    plot.add_argument("--out", metavar="DIR", default=".", help="output directory (default: .)")
    plot.add_argument(
        "--format", choices=("svg", "png"), default="svg", help="figure file format (default: svg)"
    )
    plot.add_argument("--html", action="store_true", help="also write the interactive HTML page")
    mc = commands.add_parser(
        "mc",
        help="analyze a Monte Carlo batch, or rerun one of its runs",
        description="Monte Carlo batches (astra.montecarlo): analyze one, or rerun one of its "
        "runs.",
    )
    mc_commands = mc.add_subparsers(dest="mc_command", required=True, metavar="command")
    analyze = mc_commands.add_parser(
        "analyze",
        help="outcomes, rankings, the tree and figures of a batch",
        description="Analyze a batch: outcome counts, KS with Benjamini-Hochberg, Spearman and "
        "PRCC against a metric, the classification tree, and the figures. Exit codes: 64 "
        "usage, 65 a refused batch, criterion, metric, signal or log, or an existing output "
        "file, 74 a failed write, 0 written; 70 an internal error.",
    )
    analyze.add_argument("batch", help="the batch directory (summary.parquet and run logs)")
    analyze.add_argument(
        "--criterion",
        metavar="EXPR",
        help="the failure criterion (default: status == halt), e.g. 'max.v0.derived.alpha > 0.3'",
    )
    analyze.add_argument(
        "--metric", metavar="COLUMN", help="a numeric summary column for Spearman and PRCC"
    )
    analyze.add_argument(
        "--signal",
        action="append",
        default=[],
        metavar="NAME",
        help="draw this signal's envelope across the run logs (repeatable)",
    )
    analyze.add_argument(
        "--q",
        type=_rate,
        default=0.05,
        metavar="Q",
        help="the Benjamini-Hochberg false-discovery rate, in (0, 1) (default: 0.05)",
    )
    analyze.add_argument("--out", metavar="DIR", default=".", help="output directory (default: .)")
    analyze.add_argument(
        "--format", choices=("svg", "png"), default="svg", help="figure file format (default: svg)"
    )
    rerun = mc_commands.add_parser(
        "rerun",
        help="re-execute one run of a batch with full logging",
        description="Re-execute one run of a batch from the batch's record of its spec and "
        "config, into an empty directory, and compare the result with the batch. Plugins come "
        "from the current build, so a rebuild can change the result; the new log's provenance "
        "and the row comparison show it. Exit codes: 0 reproduced, 1 not reproduced, 64 usage, "
        "65 a refused batch, run, package or output directory, 74 a failed write; 70 an "
        "internal error.",
    )
    rerun.add_argument("batch", help="the batch directory")
    rerun.add_argument("run", type=_index, help="the run index")
    rerun.add_argument(
        "--out", metavar="DIR", required=True, help="output directory (created; must be empty)"
    )
    rerun.add_argument(
        "--package",
        action="append",
        metavar="P",
        help="a plot package to render from the new log (repeatable; default: "
        f"{', '.join(RERUN_PACKAGES)})",
    )
    size = _size_parser(mc_commands)
    return parser, {"run": run, "trim": trim, "plot": plot, "size": size}


def main(argv: Sequence[str] | None = None) -> int:
    """Runs one ``astra`` command and returns its exit code (module docstring)."""
    parser, commands = _parser()
    try:
        args = parser.parse_args(argv)
    except SystemExit as stop:  # --help (0) and every usage error (64, _Parser.error)
        return stop.code if isinstance(stop.code, int) else EX_USAGE
    try:
        if args.command == "run":
            return _run(args, commands["run"])
        if args.command == "plot":
            return _plot(args)
        if args.command == "mc":
            if args.mc_command == "size":
                return _mc_size(args, commands["size"])
            return _mc_analyze(args) if args.mc_command == "analyze" else _mc_rerun(args)
        return _trim(args, commands["trim"])
    except Exception:
        # D3: a defect. Printed here, then released with the frames that hold the Simulation.
        command = f"mc {args.mc_command}" if args.command == "mc" else args.command
        print(f"astra {command}: internal error (a defect):", file=sys.stderr)
        traceback.print_exc()
        return EX_SOFTWARE


def console() -> NoReturn:
    """The ``astra`` script. It restores SIGINT and SIGPIPE to their defaults first (D5):
    ``Simulation.run()`` holds the interpreter for the whole run, so Python's own handler would
    fire only after it, and then drop the unclosed run. With the default, Ctrl-C is a kill (ADR-0027
    item 5), and a closed pipe ends the process."""
    signal.signal(signal.SIGINT, signal.SIG_DFL)
    signal.signal(signal.SIGPIPE, signal.SIG_DFL)
    code = main()
    try:
        sys.stdout.flush()
    except OSError:
        # main() already reported the failed write (D4). Shutdown would flush the same buffer
        # again, print "Exception ignored" and change the code, so fd 1 goes to /dev/null first.
        devnull = os.open(os.devnull, os.O_WRONLY)
        os.dup2(devnull, sys.stdout.fileno())
        os.close(devnull)
    sys.exit(code)


def _drive(sim: _core.Simulation) -> _core.HaltReason:
    """The run itself, a function of its own so a test can act during the run (test_cli.py)."""
    return sim.run()


def _usage(parser: _Parser, message: str) -> int:
    parser.print_usage(sys.stderr)
    print(f"{parser.prog}: error: {message}", file=sys.stderr)
    return EX_USAGE


def _say(command: str, message: str) -> None:
    print(f"astra {command}: {message}", file=sys.stderr)


def _write(command: str, text: str) -> bool:
    """Writes the whole of ``text`` to stdout and flushes it; on an ``OSError`` says so (D4)."""
    try:
        sys.stdout.write(text)
        sys.stdout.flush()
    except OSError as error:
        _say(command, f"cannot write to stdout: {error}")
        return False
    return True


# --- astra run ------------------------------------------------------------------------------------


class _LogBlockError(Exception):
    """``--log``'s block cannot be appended to the config's text; the message says why."""


def _with_log(config_path: Path, log: str) -> _core.Config:
    """The config file's own text with ``log: { path: <log> }`` appended at the top level, parsed
    by the core, so every other scalar keeps the core's own reading of the file (a Python YAML
    round trip would read ``off`` as a boolean)."""
    try:
        text = config_path.read_text(encoding="utf-8")
    except (OSError, UnicodeError) as error:
        raise _LogBlockError(f"cannot read {config_path} as UTF-8 text: {error}") from error
    separator = "" if text.endswith("\n") or not text else "\n"
    block = f"log:\n  path: {json.dumps(log, ensure_ascii=False)}\n"
    try:
        config = _core.Config.from_string(text + separator + block, str(config_path))
        found = config.text("log.path") if config.has("log.path") else None
    except (_core.ConfigError, UnicodeError) as error:
        raise _LogBlockError(f"cannot append a log block to {config_path}: {error}") from error
    if found != log:
        raise _LogBlockError(
            f"a log block appended to {config_path} does not reach its top-level map "
            "(a flow-style map or a second document?); add the log block to the file instead"
        )
    return config


def _existing_log_path(config: _core.Config) -> str:
    """The config's own ``log.path`` for D2's message, never raising: the conflict is 64 whatever
    the block holds (Codex review R-001: a nonscalar path made ``text()`` raise, exit 70)."""
    try:
        return repr(config.text("log.path")) if config.has("log.path") else "<none>"
    except _core.ConfigError:
        return "<not a string>"


def _run(args: argparse.Namespace, parser: _Parser) -> int:
    config_path = Path(args.config)
    try:
        config = _core.Config.from_file(config_path)
    except _core.ConfigError as error:
        _say("run", f"config error: {error}")
        return EX_DATAERR
    if args.log is not None and config.has("log"):
        return _usage(
            parser,
            f"--log {args.log!r}: {config_path} already has a log block (log.path "
            f"{_existing_log_path(config)}); remove one of the two",
        )
    if config.has("transport"):
        _say("run", f"config error: {_TRANSPORT_REFUSAL.format(config_path)}")
        return EX_DATAERR
    if args.log is not None:
        try:
            config = _with_log(config_path, args.log)
        except _LogBlockError as error:
            return _usage(parser, f"--log: {error}")
    try:
        sim = _core.Simulation.from_config(config, _core.default_plugin_dir())
    except _core.LogError as error:
        _say("run", f"log error: {error}")
        return EX_IOERR
    except _core.ConfigError as error:
        _say("run", f"config error: {error}")
        return EX_DATAERR
    except _core.PluginError as error:
        _say("run", f"plugin error: {error}")
        return EX_DATAERR
    # The core makes log.path absolute against this same working directory (ADR-0027 item 1).
    log_path = Path(config.text("log.path")).absolute() if sim.has_log else None

    started = time.perf_counter()
    halt = _drive(sim)
    log_error: _core.LogError | None = None
    try:
        sim.close_log()
    except _core.LogError as error:
        log_error = error
    wall = time.perf_counter() - started

    health = {
        name: value
        for name, value, _unit, _divisor in sim.registry.signals
        if name.startswith("health.")
    }
    if frozenset(health) != _runkit.HEALTH_SIGNALS:
        raise RuntimeError(
            f"the health signals {sorted(health)} are not {sorted(_runkit.HEALTH_SIGNALS)}; "
            "astra._runkit's WARN_COUNTERS must be updated deliberately"
        )
    published = log_path is not None and log_error is None
    text = _summary(config_path, sim, halt, health, log_path, published, wall)
    written = _write("run", text)

    if log_error is not None:
        assert log_path is not None  # a LogError at close needs a log
        _report_log_error(log_error, log_path)
    if log_error is not None or not written:
        return EX_IOERR
    if halt:
        _say(
            "run",
            f"halted: {halt.check.name} (source {halt.source}, frame {halt.frame}, "
            f"vehicle {halt.vehicle_index})",
        )
        return HALTED
    if args.strict:
        nonzero = [f"{name} = {health[name]!r}" for name in _runkit.WARN_COUNTERS if health[name]]
        if nonzero:
            _say("run", f"--strict: warn counters are nonzero: {', '.join(nonzero)}")
            return WARNED
    return 0


def _summary(
    config_path: Path,
    sim: _core.Simulation,
    halt: _core.HaltReason,
    health: dict[str, float],
    log_path: Path | None,
    published: bool,
    wall: float,
) -> str:
    """The whole summary text (module docstring). A published log is read back strictly here,
    while ``sim`` is alive; a load that raises is a defect (70)."""
    lines = [f"config: {config_path}", f"frames: {sim.clock.frame} of {sim.frames}"]
    if halt:
        lines.append(
            f"halt: {halt.check.name} source={halt.source} frame={halt.frame} "
            f"vehicle={halt.vehicle_index}"
        )
    else:
        lines.append("halt: none")
    lines.extend(f"{name}: {health[name]!r}" for name in sorted(health))
    if log_path is None:
        lines.append("log: none")
    elif not published:
        lines.append("log: not published (see stderr)")
    else:
        from astra import log  # h5py and pandas, only for a published log

        run_log = log.load(log_path)
        lines.append(f"log: {log_path}")
        if run_log.minimum is None or run_log.maximum is None:
            raise RuntimeError(f"{log_path}: a complete log without /summary min and max")
        # pandas ships partial types; cast at this boundary as astra.log does.
        minimum = cast(Any, run_log.minimum)
        maximum = cast(Any, run_log.maximum)
        lines.append("signals:")
        for name in [str(label) for label in minimum.index.tolist()]:
            low = float(minimum[name])
            high = float(maximum[name])
            lines.append(f"  {name} {low!r} {high!r} {run_log.units[name]}")
    simulated = sim.clock.t
    factor = f"{simulated / wall:.1f}" if wall > 0.0 else "inf"
    lines.append(f"wall time: {wall:.3f} s")
    lines.append(f"real-time factor: {factor}")
    return "\n".join(lines) + "\n"


def _report_log_error(error: _core.LogError, log_path: Path) -> None:
    """D1's fate lines (J1): true in every 74 case, with the complete-log hint only for a
    publication refused for a reason other than the identity check."""
    message = str(error)
    partial = f"{log_path}.partial"
    _say("run", f"log error: {message}")
    _say(
        "run",
        f"the log was not published to '{log_path}'. astra run deleted nothing (ADR-0027 item 8): "
        "every file is as close_log() left it, and the error above says what it kept or touched. "
        f"A later run with this path is refused while '{partial}' exists.",
    )
    _, found, reason = message.partition(_PUBLICATION)
    if found and not reason.startswith(_IDENTITY):
        _say(
            "run",
            f"'{partial}' holds this run's complete log (complete = 1). Read it with "
            "astra.log.load(path, allow_incomplete=True), or publish it by hand with mv -n once "
            f"'{log_path}' is clear.",
        )


# --- astra trim -----------------------------------------------------------------------------------


def _trim(args: argparse.Namespace, parser: _Parser) -> int:
    airspeed: float | None = args.airspeed
    altitude: float | None = args.altitude
    if airspeed is not None and not airspeed > 0.0:
        return _usage(parser, f"--airspeed {airspeed!r}: must be above zero")
    config_path = Path(args.config)
    try:
        config = _core.Config.from_file(config_path)
    except _core.ConfigError as error:
        _say("trim", f"config error: {error}")
        return EX_DATAERR
    if not config.is_map():
        # astra.trim reads the file itself and expects a map (Codex review R-002: an empty or a
        # list document reached it and raised, exit 70).
        _say("trim", f"config error: config '{config_path}': the document must be a map")
        return EX_DATAERR
    if config.has("transport"):
        _say("trim", f"config error: {_TRANSPORT_REFUSAL.format(config_path)}")
        return EX_DATAERR

    import numpy as np
    import yaml

    import astra.trim

    try:
        if airspeed is None or altitude is None:
            airspeed, altitude = _initial_setpoint(config_path, airspeed, altitude)
        if not airspeed > 0.0:
            _say(
                "trim",
                f"config error: {config_path}: the initial airspeed |v_b| is {airspeed!r}; pass "
                "--airspeed or set initial.state.v_b",
            )
            return EX_DATAERR
        result = astra.trim.trim_straight_and_level(config_path, airspeed, altitude)
    except _core.ConfigError as error:
        _say("trim", f"config error: {error}")
        return EX_DATAERR
    except _core.PluginError as error:
        _say("trim", f"plugin error: {error}")
        return EX_DATAERR
    except yaml.YAMLError as error:  # astra.trim's own read of the file (YAML 1.1)
        _say("trim", f"config error: {config_path}: {error}")
        return EX_DATAERR
    except ValueError as error:
        if not str(error).startswith(_TRIM_FLAT_EARTH):
            raise  # any other ValueError is a defect (70), not a config error
        _say("trim", f"config error: {error}")
        return EX_DATAERR
    except RuntimeError as error:
        if not str(error).startswith(_TRIM_NOT_CONVERGED):
            raise  # any other RuntimeError, a core Error included, is a defect (70)
        _say("trim", f"not trimmed: {error}")
        return NOT_TRIMMED
    worst = float(np.max(np.abs(result.residual)))
    if worst > _runkit.TRIM_RESIDUAL_BOUND:
        _say(
            "trim",
            f"not trimmed: max|residual| {worst!r} exceeds D8's bound "
            f"{_runkit.TRIM_RESIDUAL_BOUND!r}",
        )
        return NOT_TRIMMED
    state, controls = _runkit.trim_fragment(result)
    document = {
        "true_airspeed": airspeed,
        "altitude": altitude,
        "elevator": result.elevator_rad,
        "throttle": result.throttle,
        "alpha": result.alpha_rad,
        "residual": [float(v) for v in result.residual],
        "initial": {"state": state},
        "vehicle": {"initial_controls": {"controls": controls}},
    }
    text = yaml.safe_dump(document, sort_keys=False, default_flow_style=None)
    return 0 if _write("trim", text) else EX_IOERR


def _initial_setpoint(
    config_path: Path, airspeed: float | None, altitude: float | None
) -> tuple[float, float]:
    """``airspeed`` and ``altitude``, each defaulting to the config's initial state (D6): |v_b| and
    ``p_n[2]``, from the same still-air ``Simulation`` ``astra.trim`` builds."""
    import numpy as np

    import astra.trim

    config = astra.trim._load_still_air_config(config_path)  # pyright: ignore[reportPrivateUsage]
    sim = _core.Simulation.from_config(config, _core.default_plugin_dir())
    state = np.array(sim.vehicles[0].state)
    velocity = state[_core.VELOCITY : _core.VELOCITY + _core.VELOCITY_SIZE]
    if airspeed is None:
        airspeed = float(np.linalg.norm(velocity))
    if altitude is None:
        altitude = float(state[_core.POSITION + _core.POSITION_SIZE - 1])
    return airspeed, altitude


# --- astra plot -----------------------------------------------------------------------------------


def _plot(args: argparse.Namespace) -> int:
    from astra import log, plot  # h5py, pandas and the plotting libraries, only for this command

    try:
        package = plot.load_package(plot.package_path(args.package))
    except plot.PlotPackageError as error:
        _say("plot", f"package error: {error}")
        return EX_DATAERR
    try:
        run_log = log.load(args.log)
    except (log.LogFormatError, log.IncompleteLogError) as error:
        _say("plot", f"log error: {error}")
        return EX_DATAERR
    try:
        written = plot.render(package, run_log, args.out, fmt=args.format, html=args.html)
    except plot.PlotPackageError as error:
        _say("plot", f"package error: {error}")
        return EX_DATAERR
    except plot.PlotOutputExistsError as error:
        _say("plot", f"output error: {error}")
        return EX_DATAERR
    except OSError as error:
        _say("plot", f"cannot write the figures: {error}")
        return EX_IOERR
    return 0 if _write("plot", "".join(f"{path}\n" for path in written)) else EX_IOERR


# --- astra mc -------------------------------------------------------------------------------------


def _mc_analyze(args: argparse.Namespace) -> int:
    from astra import mc_analysis, plot  # pyarrow, scipy and matplotlib, only for this command

    command = "mc analyze"
    try:
        batch = mc_analysis.load_batch(args.batch)
        analysis = mc_analysis.analyze(batch, args.criterion, args.metric, args.q)
    except (mc_analysis.BatchError, mc_analysis.CriterionError) as error:
        _say(command, f"refused: {error}")
        return EX_DATAERR
    try:
        written = mc_analysis.render_figures(
            batch, analysis.outcomes, args.signal, args.out, fmt=args.format
        )
    except (mc_analysis.EnvelopeError, mc_analysis.BatchError) as error:
        _say(command, f"refused: {error}")
        return EX_DATAERR
    except plot.PlotOutputExistsError as error:
        _say(command, f"output error: {error}")
        return EX_DATAERR
    except OSError as error:
        _say(command, f"cannot write the figures: {error}")
        return EX_IOERR
    text = analysis.text() + "figures:\n" + "".join(f"  {path}\n" for path in written)
    return 0 if _write(command, text) else EX_IOERR


def _mc_rerun(args: argparse.Namespace) -> int:
    from astra import log, montecarlo, plot  # h5py, pyarrow and matplotlib, only for this command

    command = "mc rerun"
    try:
        packages = [
            plot.load_package(plot.package_path(name)) for name in args.package or RERUN_PACKAGES
        ]
    except plot.PlotPackageError as error:
        _say(command, f"package error: {error}")
        return EX_DATAERR
    try:
        result = montecarlo.rerun(args.batch, args.run, args.out)
    except (montecarlo.SpecError, montecarlo.RerunError, FileExistsError) as error:
        _say(command, f"refused: {error}")
        return EX_DATAERR
    except montecarlo.RerunWriteError as error:
        _say(command, str(error))
        return EX_IOERR
    except OSError as error:
        _say(command, f"cannot create the output directory: {error}")
        return EX_IOERR
    # The comparison is what the command is for: printed before anything is rendered, and its
    # code (1, not reproduced) takes precedence over a render failure.
    code = 0 if result.reproduced else NOT_REPRODUCED
    if not _write(command, _rerun_text(result)):
        return EX_IOERR
    if result.log_path is None:
        return code
    try:
        run_log = log.load(result.log_path)
        written = [path for package in packages for path in plot.render(package, run_log, args.out)]
    except (plot.PlotPackageError, plot.PlotOutputExistsError) as error:
        _say(command, f"the log is kept; rendering refused: {error}")
        return code or EX_DATAERR
    except (log.LogFormatError, log.IncompleteLogError) as error:
        _say(command, f"the new log is refused: {error}")
        return code or EX_DATAERR
    except OSError as error:
        _say(command, f"the log is kept; cannot write the figures: {error}")
        return code or EX_IOERR
    if not _write(command, "figures:\n" + "".join(f"  {path}\n" for path in written)):
        return code or EX_IOERR
    return code


def _rerun_text(result: Any) -> str:
    lines = [
        f"run: {result.run}",
        f"log: {result.log_path.resolve() if result.log_path is not None else 'none'}",
        "row: reproduced"
        if not result.differences
        else f"row: differs in {', '.join(result.differences)}",
    ]
    for name in result.differences:
        lines.append(
            f"  {name}: recorded {result.recorded.get(name)!r}, rerun {result.row.get(name)!r}"
        )
    if result.log_identical is None:
        lines.append("log bytes: not compared (the batch holds no log of this run)")
    else:
        lines.append(
            f"log bytes: {'identical' if result.log_identical else 'differ'} to the batch's"
        )
    lines.append(f"reproduced: {'yes' if result.reproduced else 'no'}")
    return "\n".join(lines) + "\n"


# --- astra mc size (M6 PR 7, docs/plans/m6-pr7-mc-sizing.md D9) ---------------------------------


def _decimal(text: str) -> str:
    """A decimal kept as written: ``astra.mc_sizing`` reads the text itself (Joe's V1 decision)."""
    from astra import mc_sizing

    mc_sizing.exact("value", text)  # a malformed or non-finite number is a usage error
    return text


def _count(text: str) -> int:
    value = int(text)
    if value < 1:
        raise ValueError(text)
    return value


def _size_parser(mc_commands: Any) -> _Parser:
    size = mc_commands.add_parser(
        "size",
        help="size a batch before it runs, or check a claim against one",
        description="How many runs a batch needs, by the claim it answers (astra.mc_sizing). "
        "Without --batch, the option's formula gives the minimum N. With --batch, probability "
        "and percentile give a calibrated verdict and the N that would decide it; precision and "
        "ranking give a nominal projection, never a verdict. A pilot only sizes: run the final "
        "batch with another master seed and take the verdict from it alone. Exit codes: 64 usage "
        "(including a value outside its domain), 65 a refused batch, criterion, metric or claims "
        "file, 74 stdout I/O, 0 printed; 70 an internal error.",
    )
    options = size.add_subparsers(dest="option", required=True, metavar="option")
    probability = options.add_parser(
        "probability", help="P(fail) <= p_max: zero failures, or a verdict on a batch"
    )
    probability.add_argument("--p-max", type=_decimal, required=True, metavar="P")
    probability.add_argument(
        "--criterion", metavar="EXPR", help="(with --batch) default: status == halt"
    )
    probability.add_argument(
        "--producer-risk", type=_decimal, metavar="B", help="(with --batch) default 0.10"
    )
    percentile = options.add_parser(
        "percentile", help="P(metric > limit) <= 1 - G: Wilks, or a verdict on a batch"
    )
    percentile.add_argument("--quantile", type=_decimal, required=True, metavar="G")
    percentile.add_argument("--two-sided", action="store_true", help="(formula) both sides")
    percentile.add_argument("--order", type=_count, metavar="K", help="(formula) default 1")
    percentile.add_argument("--metric", metavar="COLUMN", help="(with --batch)")
    percentile.add_argument("--limit", type=_decimal, metavar="L", help="(with --batch)")
    percentile.add_argument(
        "--side", choices=("upper", "lower"), help="(with --batch) default upper"
    )
    percentile.add_argument(
        "--producer-risk", type=_decimal, metavar="B", help="(with --batch) default 0.10"
    )
    precision = options.add_parser(
        "precision", help="a statistic within a tolerance: Clopper-Pearson, or a projection"
    )
    precision.add_argument("--p", type=_decimal, metavar="P", help="(formula) assumed probability")
    precision.add_argument("--half-width", type=_decimal, metavar="H", help="(formula)")
    precision.add_argument(
        "--statistic",
        choices=("mean", "std", "percentile", "probability", "correlation"),
        help="(with --batch)",
    )
    precision.add_argument("--tolerance", type=_decimal, metavar="T", help="(with --batch)")
    precision.add_argument(
        "--relative", action="store_true", help="(with --batch) relative to the estimate"
    )
    precision.add_argument("--metric", metavar="COLUMN", help="(with --batch)")
    precision.add_argument("--criterion", metavar="EXPR", help="(with --batch) probability")
    precision.add_argument("--parameter", metavar="NAME", help="(with --batch) correlation")
    precision.add_argument(
        "--quantile", type=_decimal, metavar="G", help="(with --batch) percentile"
    )
    ranking = options.add_parser(
        "ranking", help="detect a rank correlation: Fisher z, or a projection on a batch"
    )
    ranking.add_argument("--r", type=_decimal, metavar="R", help="(formula) 0 < |R| < 1")
    ranking.add_argument("--parameters", type=_count, metavar="M", help="(formula) ranked")
    ranking.add_argument("--alpha", type=_decimal, metavar="A", help="(formula) default 0.05")
    ranking.add_argument("--power", type=_decimal, metavar="P", help="(formula) default 0.8")
    ranking.add_argument("--metric", metavar="COLUMN", help="(with --batch)")
    claim = ranking.add_mutually_exclusive_group()
    claim.add_argument("--top", action="store_true", help="(with --batch) a top driver")
    claim.add_argument(
        "--null", type=_decimal, metavar="R_MIN", help="(with --batch) no |rho| >= R_MIN"
    )
    for parser in (probability, percentile, precision, ranking):
        parser.add_argument("--confidence", type=_decimal, metavar="C", help="default 0.95")
        parser.add_argument("--batch", metavar="DIR", help="check the claim against this batch")
        parser.add_argument(
            "--convergence", action="store_true", help="(with --batch) every prefix"
        )
        parser.add_argument(
            "--every", type=_count, default=10, metavar="K", help="(--convergence) default 10"
        )
    check = options.add_parser("check", help="check every claim of a claims file against a batch")
    check.add_argument("batch", help="the batch directory (summary.parquet)")
    check.add_argument("claims", help="the claims file (YAML)")
    return size


_BATCH_ONLY = {
    "probability": ("criterion", "producer_risk", "convergence"),
    "percentile": ("metric", "limit", "side", "producer_risk", "convergence"),
    "precision": (
        "statistic",
        "tolerance",
        "relative",
        "metric",
        "criterion",
        "parameter",
        "quantile",
        "convergence",
    ),
    "ranking": ("metric", "top", "null", "convergence"),
}
_FORMULA_ONLY = {
    "probability": (),
    "percentile": ("two_sided", "order"),
    "precision": ("p", "half_width"),
    "ranking": ("r", "parameters", "alpha", "power"),
}


def _given(args: argparse.Namespace, name: str) -> bool:
    return getattr(args, name, None) not in (None, False)


def _mc_size(args: argparse.Namespace, parser: _Parser) -> int:
    from astra import mc_analysis, mc_sizing  # scipy and pyarrow, only for this command

    command = "mc size"
    refused = (mc_sizing.SizingError, mc_analysis.BatchError, mc_analysis.CriterionError)
    if args.option == "check":
        try:
            claims = mc_sizing.load_claims(args.claims)
            table = mc_sizing.check(mc_analysis.load_batch(args.batch), claims)
        except refused as error:
            _say(command, f"refused: {error}")
            return EX_DATAERR
        return 0 if _write(command, table.text()) else EX_IOERR
    option = args.option
    wrong = _FORMULA_ONLY[option] if args.batch is not None else _BATCH_ONLY[option]
    extra = [name for name in wrong if _given(args, name)]
    if extra:
        flag = f"--{extra[0].replace('_', '-')}"
        reason = "is for the formula mode" if args.batch is not None else "needs --batch"
        return _usage(parser, f"{option}: {flag} {reason}")
    try:
        if args.batch is None:
            return 0 if _write(command, _size_formula(args)) else EX_IOERR
        claim = _size_claim(args)
    except mc_sizing.SizingError as error:
        return _usage(parser, f"{option}: {error}")
    try:
        batch = mc_analysis.load_batch(args.batch)
        text = mc_sizing.check(batch, [claim]).text()
        if args.convergence:
            text += mc_sizing.convergence(batch, claim).text(args.every)
    except refused as error:
        _say(command, f"refused: {error}")
        return EX_DATAERR
    return 0 if _write(command, text) else EX_IOERR


def _or(value: str | None, default: str) -> str:
    return default if value is None else value


def _size_formula(args: argparse.Namespace) -> str:
    from astra import mc_sizing

    confidence = _or(args.confidence, mc_sizing.DEFAULT_CONFIDENCE)
    option = cast(str, args.option)
    if option == "probability":
        runs = mc_sizing.probability_runs(args.p_max, confidence)
        inputs = f"p_max {args.p_max}, confidence {confidence}"
    elif option == "percentile":
        order = cast(int, args.order or 1)
        runs = mc_sizing.percentile_runs(
            args.quantile, confidence, two_sided=args.two_sided, order=order
        )
        sides = "two-sided" if args.two_sided else "one-sided"
        inputs = f"coverage {args.quantile}, confidence {confidence}, {sides}, order {order}"
    elif option == "precision":
        if args.p is None or args.half_width is None:
            raise mc_sizing.SizingError("--p and --half-width are required without --batch")
        runs = mc_sizing.precision_runs(args.p, args.half_width, confidence)
        inputs = f"p {args.p}, half-width {args.half_width}, confidence {confidence}"
    else:
        if args.r is None or args.parameters is None:
            raise mc_sizing.SizingError("--r and --parameters are required without --batch")
        alpha = _or(args.alpha, "0.05")
        power = _or(args.power, "0.8")
        runs = mc_sizing.ranking_runs(args.r, args.parameters, alpha, power)
        inputs = f"r {args.r}, parameters {args.parameters}, alpha {alpha}, power {power}"
    method = mc_sizing.ASSUMPTIONS[option]
    return f"option: {option}\nruns: {runs}\ninputs: {inputs}\nmethod: {method}\n"


def _size_claim(args: argparse.Namespace) -> Any:
    from astra import mc_sizing

    confidence = _or(args.confidence, mc_sizing.DEFAULT_CONFIDENCE)
    risk = _or(getattr(args, "producer_risk", None), mc_sizing.DEFAULT_PRODUCER_RISK)
    option = args.option
    if option == "probability":
        return mc_sizing.ProbabilityClaim(args.p_max, args.criterion, confidence, risk)
    if option == "percentile":
        if args.metric is None or args.limit is None:
            raise mc_sizing.SizingError("--metric and --limit are required with --batch")
        side = args.side or "upper"
        return mc_sizing.PercentileClaim(
            args.metric, args.quantile, args.limit, side, confidence, risk
        )
    if option == "precision":
        if args.statistic is None or args.tolerance is None:
            raise mc_sizing.SizingError("--statistic and --tolerance are required with --batch")
        return mc_sizing.PrecisionClaim(
            args.statistic,
            args.tolerance,
            args.relative,
            args.metric,
            args.criterion,
            args.parameter,
            args.quantile,
            confidence,
        )
    if args.metric is None:
        raise mc_sizing.SizingError("--metric is required with --batch")
    if args.null is not None:
        return mc_sizing.RankingClaim(args.metric, "null", args.null, confidence)
    if not args.top:
        raise mc_sizing.SizingError("--top or --null is required with --batch")
    return mc_sizing.RankingClaim(args.metric, "top", None, confidence)


if __name__ == "__main__":
    console()
