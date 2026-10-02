"""astra.cli: the ``astra`` console script (M6 PR 3, docs/plans/m6-pr3-cli.md; docs/architecture.md
§12, §15 and §18).

Subcommands: ``run``, ``trim`` and ``plot`` (M6 PR 5, docs/plans/m6-pr5-plots.md). ``report`` and
``mc`` come with their tools (M6 plan).
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
    return parser, {"run": run, "trim": trim, "plot": plot}


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
        return _trim(args, commands["trim"])
    except Exception:
        # D3: a defect. Printed here, then released with the frames that hold the Simulation.
        print(f"astra {args.command}: internal error (a defect):", file=sys.stderr)
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


if __name__ == "__main__":
    console()
