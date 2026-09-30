"""The ``astra`` console script (M6 PR 3, docs/plans/m6-pr3-cli.md): R14's exit-code table, each row
seen to fire, with the precedence between rows, what goes to stdout and stderr, and what is left on
disk on every path (D1: ``astra run`` deletes nothing); R15's trim part; C-14.

Subprocess tests run the installed script next to the interpreter, which shows it exists. In-process
tests call ``astra.cli.main`` and replace ``astra.cli._drive``, the run itself, to act during the
run: a file placed at ``log.path``, a ``.partial`` moved aside, an internal error. The core's fault
hooks are not bound (PR 5 D2), so a log write failure is a real one, from ``RLIMIT_FSIZE`` in a
subprocess.
"""

from __future__ import annotations

import functools
import gc
import os
import re
import resource
import signal
import subprocess
import sys
import time
from collections.abc import Callable, Sequence
from pathlib import Path
from typing import Any, cast

import numpy as np
import pytest
import yaml

import astra.trim
from astra import _core, _runkit, cli, log

ROOT = Path(__file__).resolve().parents[2]
ASTRA = Path(sys.executable).parent / "astra"
DUMMY = ROOT / "tests/configs/dummy_1000.yaml"
NAN_WRENCH = ROOT / "tests/configs/health_nan_wrench.yaml"
WARN = ROOT / "tests/python/scenario_fixtures/nan_wrench_warn_config.yaml"
HALT_AFTER_WARN = ROOT / "tests/python/cli_fixtures/halt_after_warn.yaml"
TORQUE_FREE = ROOT / "tests/configs/physics_torque_free.yaml"
TRANSPORT = ROOT / "tests/configs/no_alloc_transport.yaml"
FLAT_EARTH = ROOT / "tests/configs/no_alloc_aero_propulsion.yaml"
CRUISE = ROOT / "aircraft/notional_transport/configs/cruise.yaml"

FATE = (
    "astra run: the log was not published to '{path}'. astra run deleted nothing (ADR-0027 "
    "item 8): every file is as close_log() left it, and the error above says what it kept or "
    "touched. A later run with this path is refused while '{path}.partial' exists.\n"
)
COMPLETE_HINT = (
    "astra run: '{path}.partial' holds this run's complete log (complete = 1). Read it with "
    "astra.log.load(path, allow_incomplete=True), or publish it by hand with mv -n once "
    "'{path}' is clear.\n"
)


# --- helpers ------------------------------------------------------------------------------------


def astra_script(
    *args: str | Path, cwd: Path, preexec_fn: Callable[[], None] | None = None, **kwargs: Any
) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        [str(ASTRA), *map(str, args)],
        cwd=cwd,
        text=True,
        capture_output="stdout" not in kwargs,
        timeout=300,
        check=False,
        preexec_fn=preexec_fn,
        **kwargs,
    )


def run_main(capsys: pytest.CaptureFixture[str], *argv: str | Path) -> tuple[int, str, str]:
    code = cli.main([str(a) for a in argv])
    captured = capsys.readouterr()
    return code, captured.out, captured.err


def summary(stdout: str) -> dict[str, str]:
    """The summary's ``key: value`` lines, in order, without the ``signals:`` section's rows."""
    fields: dict[str, str] = {}
    for line in stdout.splitlines():
        if line.startswith("  "):
            continue
        key, _, value = line.partition(": ")
        assert key not in fields, f"{key} printed twice"
        fields[key] = value
    return fields


def expected_keys(*, signals: bool) -> list[str]:
    health = sorted(_runkit.HEALTH_SIGNALS)
    return [
        "config",
        "frames",
        "halt",
        *health,
        "log",
        *(["signals:"] if signals else []),
        "wall time",
        "real-time factor",
    ]


def signal_rows(stdout: str) -> list[list[str]]:
    return [line.split() for line in stdout.splitlines() if line.startswith("  ")]


def files(directory: Path) -> set[str]:
    return {p.name for p in directory.iterdir()}


def with_frames(source: Path, frames: int, directory: Path) -> Path:
    """A copy of ``source`` whose ``sim.frames`` is ``frames``."""
    text = source.read_text(encoding="utf-8")
    changed = re.sub(r"^  frames: \d+$", f"  frames: {frames}", text, count=1, flags=re.M)
    assert changed != text or f"frames: {frames}" in text
    target = directory / f"{source.stem}_{frames}.yaml"
    target.write_text(changed, encoding="utf-8")
    return target


def constructed_size(config: Path, directory: Path) -> int:
    """The size of the ``.partial`` right after ``from_config``, built exactly as ``astra run
    --log`` builds it (the provenance holds no path, so any name gives the same bytes)."""
    probe = directory / "probe.h5"
    sim = _core.Simulation.from_config(
        cli._with_log(config, str(probe)),  # pyright: ignore[reportPrivateUsage]
        _core.default_plugin_dir(),
    )
    size = Path(f"{probe}.partial").stat().st_size
    assert sim.discard_log().deleted
    return size


def file_size_limit(limit: int) -> Callable[[], None]:
    def apply() -> None:
        signal.signal(signal.SIGXFSZ, signal.SIG_IGN)  # a write past the limit fails with EFBIG
        resource.setrlimit(resource.RLIMIT_FSIZE, (limit, limit))

    return apply


def drive_then(action: Callable[[], None]) -> Callable[[_core.Simulation], _core.HaltReason]:
    """A ``_drive`` that runs the whole run, then acts before ``close_log()``."""

    def drive(sim: _core.Simulation) -> _core.HaltReason:
        halt = sim.run()
        action()
        return halt

    return drive


def assert_timing(fields: dict[str, str]) -> None:
    assert re.fullmatch(r"[0-9]+\.[0-9]{3} s", fields["wall time"])
    assert re.fullmatch(r"[0-9]+\.[0-9]|inf", fields["real-time factor"])


# --- row 0 and the summary ----------------------------------------------------------------------


def test_clean_run_exits_0_with_the_summary_and_nothing_on_stderr(tmp_path: Path) -> None:
    result = astra_script("run", DUMMY, cwd=tmp_path)
    assert result.returncode == 0
    assert result.stderr == ""
    fields = summary(result.stdout)
    assert list(fields) == expected_keys(signals=False)
    assert fields["config"] == str(DUMMY)
    assert fields["frames"] == "1000 of 1000"
    assert fields["halt"] == "none"
    assert fields["log"] == "none"
    assert_timing(fields)
    assert files(tmp_path) == set()


def test_clean_run_with_log_publishes_it_and_prints_its_summary_exactly(tmp_path: Path) -> None:
    out = tmp_path / "run.h5"
    result = astra_script("run", DUMMY, "--log", "run.h5", cwd=tmp_path)
    assert result.returncode == 0
    assert result.stderr == ""
    assert files(tmp_path) == {"run.h5"}
    run_log = log.load(out)
    assert run_log.published
    fields = summary(result.stdout)
    assert list(fields) == expected_keys(signals=True)
    assert fields["log"] == str(out)
    assert fields["frames"] == f"{run_log.frames_run} of 1000"
    for name, value in run_log.health.items():
        assert fields[name] == repr(float(value))  # exact: the text reads back to the same double
    assert run_log.minimum is not None
    assert run_log.maximum is not None
    rows = signal_rows(result.stdout)
    assert [row[0] for row in rows] == [str(n) for n in run_log.minimum.index]
    minimum = cast(Any, run_log.minimum)  # pandas ships partial types
    maximum = cast(Any, run_log.maximum)
    for name, low, high, unit in rows:
        assert low == repr(float(minimum[name]))
        assert high == repr(float(maximum[name]))
        assert unit == run_log.units[name]


def test_strict_ignores_max_q_drift_which_is_not_a_counter(tmp_path: Path) -> None:
    result = astra_script("run", TORQUE_FREE, "--strict", cwd=tmp_path)
    fields = summary(result.stdout)
    assert float(fields["health.max_q_drift"]) > 0.0
    assert result.returncode == 0
    assert result.stderr == ""


# --- rows 1 and 2 -------------------------------------------------------------------------------


def test_halt_exits_1_names_the_check_and_publishes_the_log(tmp_path: Path) -> None:
    result = astra_script("run", NAN_WRENCH, "--log", "run.h5", cwd=tmp_path)
    assert result.returncode == 1
    fields = summary(result.stdout)
    assert fields["frames"] == "500 of 1000"
    assert fields["halt"] == "NON_FINITE_WRENCH source=tests/plugins/nan_wrench frame=500 vehicle=0"
    assert result.stderr == (
        "astra run: halted: NON_FINITE_WRENCH (source tests/plugins/nan_wrench, frame 500, "
        "vehicle 0)\n"
    )
    assert files(tmp_path) == {"run.h5"}
    halt = log.load(tmp_path / "run.h5").halt
    assert halt is not None
    assert (halt.check, halt.source, halt.frame) == (
        "NON_FINITE_WRENCH",
        "tests/plugins/nan_wrench",
        500,
    )


@pytest.mark.parametrize("strict", [False, True])
def test_halt_after_warnings_exits_1_with_or_without_strict(tmp_path: Path, strict: bool) -> None:
    result = astra_script("run", HALT_AFTER_WARN, *(["--strict"] if strict else []), cwd=tmp_path)
    assert result.returncode == 1
    fields = summary(result.stdout)
    assert float(fields["health.domain"]) > 0.0  # counted under warn before the halt
    assert fields["halt"].startswith(
        "NON_FINITE_WRENCH source=tests/plugins/nan_wrench frame=1500 "
    )
    assert result.stderr.startswith("astra run: halted: NON_FINITE_WRENCH")
    assert "--strict" not in result.stderr


def test_warnings_without_a_halt_exit_2_under_strict(tmp_path: Path) -> None:
    result = astra_script("run", WARN, "--strict", "--log", "run.h5", cwd=tmp_path)
    assert result.returncode == 2
    assert summary(result.stdout)["health.nan"] == "4500.0"
    assert result.stderr == "astra run: --strict: warn counters are nonzero: health.nan = 4500.0\n"
    assert files(tmp_path) == {"run.h5"}


def test_warnings_without_strict_exit_0(tmp_path: Path) -> None:
    result = astra_script("run", WARN, cwd=tmp_path)
    assert result.returncode == 0
    assert result.stderr == ""
    assert summary(result.stdout)["health.nan"] == "4500.0"


# --- row 74: a refused publication and a failed write ------------------------------------------


@pytest.mark.parametrize(
    ("config", "flags", "halt"),
    [
        (DUMMY, [], "none"),
        (NAN_WRENCH, [], "NON_FINITE_WRENCH source=tests/plugins/nan_wrench frame=500 vehicle=0"),
        (WARN, ["--strict"], "none"),
    ],
    ids=["clean", "74-over-1", "74-over-2"],
)
def test_file_placed_at_log_path_during_the_run_exits_74_and_keeps_the_partial(
    tmp_path: Path,
    monkeypatch: pytest.MonkeyPatch,
    capsys: pytest.CaptureFixture[str],
    config: Path,
    flags: list[str],
    halt: str,
) -> None:
    out = tmp_path / "run.h5"
    foreign = b"not this run's log\n"

    def collide() -> None:
        out.write_bytes(foreign)

    monkeypatch.setattr(cli, "_drive", drive_then(collide))
    code, stdout, stderr = run_main(capsys, "run", config, *flags, "--log", out)
    assert code == 74
    fields = summary(stdout)
    assert fields["halt"] == halt
    assert fields["log"] == "not published (see stderr)"
    assert "signals:" not in fields
    lines = stderr.splitlines(keepends=True)
    assert len(lines) == 3
    assert lines[0].startswith(f"astra run: log error: run log '{out}': the publication failed: ")
    assert "(EEXIST)" in lines[0]
    assert lines[1] == FATE.format(path=out)
    assert lines[2] == COMPLETE_HINT.format(path=out)
    assert out.read_bytes() == foreign  # the foreign file is untouched
    assert files(tmp_path) == {"run.h5", "run.h5.partial"}
    kept = log.load(f"{out}.partial", allow_incomplete=True)
    assert (kept.state, kept.published) == (log.COMPLETE, False)


def test_partial_replaced_during_the_run_exits_74_and_touches_nothing(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    out = tmp_path / "run.h5"
    partial = Path(f"{out}.partial")
    moved = tmp_path / "moved.h5"
    foreign = b"a second run's file\n"

    def replace() -> None:
        partial.rename(moved)
        partial.write_bytes(foreign)

    monkeypatch.setattr(cli, "_drive", drive_then(replace))
    code, stdout, stderr = run_main(capsys, "run", DUMMY, "--log", out)
    assert code == 74
    assert summary(stdout)["log"] == "not published (see stderr)"
    lines = stderr.splitlines(keepends=True)
    assert len(lines) == 2  # no complete-log hint: the .partial name holds a foreign file
    assert "the .partial is no longer this run's file" in lines[0]
    assert lines[1] == FATE.format(path=out)
    assert partial.read_bytes() == foreign
    assert files(tmp_path) == {"run.h5.partial", "moved.h5"}
    assert log.load(moved, allow_incomplete=True).state == log.COMPLETE


def test_failed_drain_write_is_a_log_halt_that_exits_74_not_1(tmp_path: Path) -> None:
    config = with_frames(DUMMY, 5000, tmp_path)  # past the first high-water drain (2048 rows)
    limit = constructed_size(config, tmp_path) + 1024  # below one 1024-row block
    result = astra_script(
        "run", config, "--log", "run.h5", cwd=tmp_path, preexec_fn=file_size_limit(limit)
    )
    assert result.returncode == 74
    fields = summary(result.stdout)
    assert re.fullmatch(r"LOG source=log frame=[0-9]+ vehicle=0", fields["halt"])
    assert fields["log"] == "not published (see stderr)"
    out = tmp_path / "run.h5"
    # The core's message carries HDF5's own text, which can span lines.
    assert result.stderr.startswith("astra run: log error: run log 'run.h5': the rows of /rate_1 ")
    assert result.stderr.endswith(FATE.format(path=out))
    assert "complete = 1" not in result.stderr
    assert "halted" not in result.stderr
    assert files(tmp_path) == {config.name, "run.h5.partial"}


def test_failed_write_at_close_exits_74_and_keeps_the_partial(tmp_path: Path) -> None:
    limit = constructed_size(DUMMY, tmp_path) + 1024  # 1000 frames: every row is written at close
    result = astra_script(
        "run", DUMMY, "--log", "run.h5", cwd=tmp_path, preexec_fn=file_size_limit(limit)
    )
    assert result.returncode == 74
    assert summary(result.stdout)["halt"] == "none"
    out = tmp_path / "run.h5"
    assert result.stderr.startswith("astra run: log error: run log 'run.h5': ")
    assert result.stderr.endswith(FATE.format(path=out))
    assert "complete = 1" not in result.stderr
    assert files(tmp_path) == {"run.h5.partial"}


def test_failed_write_at_construction_is_a_config_error_65_not_74(tmp_path: Path) -> None:
    limit = constructed_size(DUMMY, tmp_path) - 1  # the provenance flush fails (D8)
    result = astra_script(
        "run", DUMMY, "--log", "run.h5", cwd=tmp_path, preexec_fn=file_size_limit(limit)
    )
    assert result.returncode == 65
    assert result.stdout == ""
    assert result.stderr.startswith("astra run: config error: ")
    assert "'log.path'" in result.stderr
    assert result.stderr.count("\n") == 1
    assert files(tmp_path) == set()  # the core deleted the .partial it created


# --- row 65 -------------------------------------------------------------------------------------


@pytest.mark.parametrize("with_log", [False, True])
def test_transport_block_exits_65_naming_transport_before_anything_is_built(
    tmp_path: Path, with_log: bool
) -> None:
    result = astra_script(
        "run", TRANSPORT, *(["--log", "run.h5"] if with_log else []), cwd=tmp_path
    )
    assert result.returncode == 65
    assert result.stdout == ""
    assert result.stderr == (
        f"astra run: config error: config '{TRANSPORT}' at 'transport': a transport block is "
        "refused until M10 (closed loop)\n"
    )
    assert files(tmp_path) == set()


@pytest.mark.parametrize(
    ("text", "prefix", "names"),
    [
        (None, "config error", "missing.yaml"),
        ("sim: [\n", "config error", "malformed YAML"),
        ("{dummy}bogus: 1\n", "config error", "bogus"),
        (
            "sim: {{dt: 0.001, frames: 10}}\n"
            "vehicle:\n  models:\n    - {{plugin: tests/plugins/nope}}\n",
            "plugin error",
            "tests/plugins/nope",
        ),
    ],
    ids=["missing", "malformed", "unknown-key", "unknown-plugin"],
)
def test_config_errors_exit_65(
    tmp_path: Path, capsys: pytest.CaptureFixture[str], text: str | None, prefix: str, names: str
) -> None:
    config = tmp_path / "missing.yaml"
    if text is not None:
        config = tmp_path / "config.yaml"
        config.write_text(text.format(dummy=DUMMY.read_text(encoding="utf-8")), encoding="utf-8")
    code, stdout, stderr = run_main(capsys, "run", config)
    assert code == 65
    assert stdout == ""
    assert stderr.startswith(f"astra run: {prefix}: ")
    assert names in stderr


@pytest.mark.parametrize("existing", ["run.h5", "run.h5.partial"])
def test_existing_log_path_or_partial_exits_65_and_is_untouched(
    tmp_path: Path, capsys: pytest.CaptureFixture[str], existing: str
) -> None:
    held = b"the user's own file\n"
    (tmp_path / existing).write_bytes(held)
    code, stdout, stderr = run_main(capsys, "run", DUMMY, "--log", tmp_path / "run.h5")
    assert code == 65
    assert stdout == ""
    assert stderr.startswith("astra run: config error: ")
    assert "'log.path'" in stderr
    assert (tmp_path / existing).read_bytes() == held
    assert files(tmp_path) == {existing}


# --- row 64 -------------------------------------------------------------------------------------


@pytest.mark.parametrize(
    "argv",
    [
        [],
        ["run"],
        ["run", str(DUMMY), "--bogus"],
        ["run", str(DUMMY), "extra"],
        ["run", str(DUMMY), "--log"],
        ["bogus"],
        ["run", "missing.yaml", "--bogus"],  # 64 over 65
    ],
    ids=[
        "none",
        "no-config",
        "unknown-option",
        "extra",
        "log-no-value",
        "unknown-command",
        "64-over-65",
    ],
)
def test_usage_errors_exit_64(
    tmp_path: Path,
    monkeypatch: pytest.MonkeyPatch,
    capsys: pytest.CaptureFixture[str],
    argv: list[str],
) -> None:
    monkeypatch.chdir(tmp_path)
    code, stdout, stderr = run_main(capsys, *argv)
    assert code == 64
    assert stdout == ""
    assert stderr.startswith("usage: astra")
    assert re.search(r"^astra( run)?: error: ", stderr, flags=re.M)
    assert files(tmp_path) == set()


def test_usage_error_exits_64_from_the_script(tmp_path: Path) -> None:
    result = astra_script("run", cwd=tmp_path)
    assert result.returncode == 64
    assert result.stdout == ""
    assert result.stderr.startswith("usage: astra run")


@pytest.mark.parametrize(
    ("block", "named"),
    [
        ("log:\n  path: own.h5\n", "(log.path 'own.h5')"),
        ("log:\n  path: []\n", "(log.path <not a string>)"),  # Codex review R-001
        ("log:\n  buffer_frames: 8\n", "(log.path <none>)"),
        ("log: 3\n", "(log.path <none>)"),
    ],
    ids=["path", "nonscalar-path", "no-path", "scalar-block"],
)
def test_log_option_against_a_config_log_block_exits_64_whatever_it_holds(
    tmp_path: Path, capsys: pytest.CaptureFixture[str], block: str, named: str
) -> None:
    config = tmp_path / "logged.yaml"
    config.write_text(DUMMY.read_text(encoding="utf-8") + block, encoding="utf-8")
    code, stdout, stderr = run_main(capsys, "run", config, "--log", tmp_path / "cli.h5")
    assert code == 64
    assert stdout == ""
    assert stderr.startswith("usage: astra run")
    assert named in stderr
    assert f"'{tmp_path / 'cli.h5'}'" in stderr
    assert files(tmp_path) == {"logged.yaml"}


def test_log_option_that_cannot_reach_the_top_level_map_exits_64(
    tmp_path: Path, capsys: pytest.CaptureFixture[str]
) -> None:
    config = tmp_path / "flow.yaml"
    config.write_text(
        "{sim: {dt: 0.001, frames: 10}, vehicle: {models: [{plugin: tests/plugins/dummy}]}}\n",
        encoding="utf-8",
    )
    code, stdout, stderr = run_main(capsys, "run", config, "--log", tmp_path / "run.h5")
    assert code == 64
    assert stdout == ""
    assert "astra run: error: --log: " in stderr
    assert files(tmp_path) == {"flow.yaml"}


def test_log_option_keeps_the_cores_own_reading_of_the_config(tmp_path: Path) -> None:
    # YAML 1.1 (yaml.safe_load) reads "off" as False, which the core refuses; the appended block
    # leaves the file's text to the core's parser, so the action stays "off".
    config = tmp_path / "off.yaml"
    text = NAN_WRENCH.read_text(encoding="utf-8")
    config.write_text(
        text.replace("  frames: 1000\n", "  frames: 1000\n  health: { nan: off }\n"),
        encoding="utf-8",
    )
    result = astra_script("run", config, "--log", "run.h5", cwd=tmp_path)
    assert result.returncode == 0
    assert result.stderr == ""
    fields = summary(result.stdout)
    assert (fields["frames"], fields["health.nan"]) == ("1000 of 1000", "0.0")
    assert files(tmp_path) == {"off.yaml", "run.h5"}


def test_help_exits_0_on_stdout(capsys: pytest.CaptureFixture[str]) -> None:
    code, stdout, stderr = run_main(capsys, "run", "--help")
    assert code == 0
    assert stdout.startswith("usage: astra run")
    assert stderr == ""


def test_module_entry_point_is_the_script(tmp_path: Path) -> None:
    result = subprocess.run(
        [sys.executable, "-m", "astra.cli", "run"],
        cwd=tmp_path,
        capture_output=True,
        text=True,
        timeout=60,
        check=False,
    )
    assert result.returncode == 64
    assert result.stderr.startswith("usage: astra run")


# --- outside the table: 70, stdout failures and signals (D3 to D5) ------------------------------


def test_internal_error_during_the_run_exits_70_and_leaves_the_destructors_partial(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    def fail(sim: _core.Simulation) -> _core.HaltReason:
        sim.run(10)
        raise RuntimeError("injected defect")

    monkeypatch.setattr(cli, "_drive", fail)
    out = tmp_path / "run.h5"
    code, stdout, stderr = run_main(capsys, "run", DUMMY, "--log", out)
    assert code == 70
    assert stdout == ""
    assert stderr.startswith("astra run: internal error (a defect):\n")
    assert "RuntimeError: injected defect" in stderr
    gc.collect()  # a traceback can hold the Simulation; the destructor has run after this
    assert files(tmp_path) == {"run.h5.partial"}
    assert log.load(f"{out}.partial", allow_incomplete=True).state == log.DESTRUCTOR


def test_strict_load_of_the_published_log_that_raises_exits_70(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    def refuse(path: str | Path, *, allow_incomplete: bool = False) -> log.RunLog:
        raise log.IncompleteLogError(f"{path}: injected refusal")

    monkeypatch.setattr(log, "load", refuse)
    out = tmp_path / "run.h5"
    code, stdout, stderr = run_main(capsys, "run", DUMMY, "--log", out)
    assert code == 70
    assert stdout == ""  # the summary is built whole before any of it is written
    assert "IncompleteLogError" in stderr
    gc.collect()
    assert files(tmp_path) == {"run.h5"}  # published before the load; nothing deleted


def test_stdout_write_failure_exits_74_after_the_log_is_published(tmp_path: Path) -> None:
    with open("/dev/full", "w", encoding="utf-8") as full:
        result = astra_script(
            "run", DUMMY, "--log", "run.h5", cwd=tmp_path, stdout=full, stderr=subprocess.PIPE
        )
    assert result.returncode == 74
    assert result.stderr.startswith("astra run: cannot write to stdout: ")
    assert "Exception ignored" not in result.stderr
    assert files(tmp_path) == {"run.h5"}
    assert log.load(tmp_path / "run.h5").published


def test_closed_stdout_pipe_ends_the_script_by_sigpipe_after_publishing(tmp_path: Path) -> None:
    read_end, write_end = os.pipe()
    os.close(read_end)
    try:
        result = astra_script(
            "run", DUMMY, "--log", "run.h5", cwd=tmp_path, stdout=write_end, stderr=subprocess.PIPE
        )
    finally:
        os.close(write_end)
    assert result.returncode == -signal.SIGPIPE
    assert files(tmp_path) == {"run.h5"}


def test_sigint_is_a_kill_that_leaves_only_the_partial(tmp_path: Path) -> None:
    config = with_frames(DUMMY, 1_000_000_000, tmp_path)  # far longer than the test waits
    partial = tmp_path / "run.h5.partial"
    process = subprocess.Popen(
        [str(ASTRA), "run", str(config), "--log", "run.h5"],
        cwd=tmp_path,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )
    try:
        deadline = time.monotonic() + 60.0
        while not partial.exists():
            assert process.poll() is None, "the run ended before the signal"
            assert time.monotonic() < deadline, "no .partial appeared"
            time.sleep(0.01)
        process.send_signal(signal.SIGINT)
        code = process.wait(timeout=60)
    finally:
        if process.poll() is None:
            process.kill()
            process.wait()
    assert code == -signal.SIGINT
    assert files(tmp_path) == {config.name, "run.h5.partial"}


def test_health_signals_are_the_ones_the_cli_classifies() -> None:
    sim = _core.Simulation.from_config(_core.Config.from_file(DUMMY), _core.default_plugin_dir())
    names = {name for name, _value, _unit, _divisor in sim.registry.signals}
    assert {n for n in names if n.startswith("health.")} == _runkit.HEALTH_SIGNALS


# --- astra trim (R15, trim part) ----------------------------------------------------------------


def trim_values(document: dict[str, Any]) -> list[float]:
    state = document["initial"]["state"]
    controls = document["vehicle"]["initial_controls"]["controls"]
    return [
        document["true_airspeed"],
        document["altitude"],
        document["elevator"],
        document["throttle"],
        document["alpha"],
        *document["residual"],
        *state["p_n"],
        *state["v_b"],
        *state["euler_nb"],
        *state["omega_b"],
        controls["elevator"],
        controls["throttle"],
    ]


def core_values(text: str) -> list[float]:
    """The same values read by the core's own YAML parser, as a config would read them."""
    config = _core.Config.from_string(text, "astra trim output")

    def listed(key: str, count: int) -> list[float]:
        return [config.number(f"{key}.{i}") for i in range(count)]

    return [
        config.number("true_airspeed"),
        config.number("altitude"),
        config.number("elevator"),
        config.number("throttle"),
        config.number("alpha"),
        *listed("residual", 6),
        *listed("initial.state.p_n", 3),
        *listed("initial.state.v_b", 3),
        *listed("initial.state.euler_nb", 3),
        *listed("initial.state.omega_b", 3),
        config.number("vehicle.initial_controls.controls.elevator"),
        config.number("vehicle.initial_controls.controls.throttle"),
    ]


def bits(values: Sequence[float]) -> list[str]:
    return [float(v).hex() for v in values]


def expected_trim(airspeed: float, altitude: float) -> list[float]:
    result = astra.trim.trim_straight_and_level(CRUISE, airspeed, altitude)
    state, controls = _runkit.trim_fragment(result)
    return [
        airspeed,
        altitude,
        result.elevator_rad,
        result.throttle,
        result.alpha_rad,
        *(float(v) for v in result.residual),
        *state["p_n"],
        *state["v_b"],
        *state["euler_nb"],
        *state["omega_b"],
        controls["elevator"],
        controls["throttle"],
    ]


def test_trim_output_is_exact_to_the_last_digit(tmp_path: Path) -> None:
    result = astra_script("trim", CRUISE, "--airspeed", "230", "--altitude", "10000", cwd=tmp_path)
    assert result.returncode == 0
    assert result.stderr == ""
    document = yaml.safe_load(result.stdout)
    assert list(document) == [
        "true_airspeed",
        "altitude",
        "elevator",
        "throttle",
        "alpha",
        "residual",
        "initial",
        "vehicle",
    ]
    expected = bits(expected_trim(230.0, 10000.0))
    assert bits(trim_values(document)) == expected
    assert bits(core_values(result.stdout)) == expected
    assert files(tmp_path) == set()


def test_trim_setpoint_defaults_to_the_configs_initial_state(tmp_path: Path) -> None:
    config = _core.Config.from_file(CRUISE)
    v_b = [config.number(f"initial.state.v_b.{i}") for i in range(3)]
    airspeed = float(np.linalg.norm(v_b))
    altitude = config.number("initial.state.p_n.2")
    result = astra_script("trim", CRUISE, cwd=tmp_path)
    assert result.returncode == 0
    assert result.stderr == ""
    assert bits(trim_values(yaml.safe_load(result.stdout))) == bits(
        expected_trim(airspeed, altitude)
    )


@pytest.mark.parametrize(
    "argv",
    [
        ["trim", str(CRUISE), "--airspeed", "abc"],
        ["trim", str(CRUISE), "--airspeed", "inf"],
        ["trim", str(CRUISE), "--airspeed", "-1"],
        ["trim", str(CRUISE), "--altitude", "nan"],
        ["trim", str(CRUISE), "--bogus"],
        ["trim"],
    ],
    ids=["not-a-number", "infinite", "negative", "nan-altitude", "unknown-option", "no-config"],
)
def test_trim_usage_errors_exit_64(capsys: pytest.CaptureFixture[str], argv: list[str]) -> None:
    code, stdout, stderr = run_main(capsys, *argv)
    assert code == 64
    assert stdout == ""
    assert stderr.startswith("usage: astra")  # argparse reports an unknown option at the top level


@pytest.mark.parametrize(
    ("config", "names"),
    [(FLAT_EARTH, "physical.earth"), (TRANSPORT, "'transport'")],
    ids=["flat-earth", "transport"],
)
def test_trim_config_errors_exit_65(
    capsys: pytest.CaptureFixture[str], config: Path, names: str
) -> None:
    code, stdout, stderr = run_main(capsys, "trim", config, "--airspeed", "100", "--altitude", "0")
    assert code == 65
    assert stdout == ""
    assert stderr.startswith("astra trim: config error: ")
    assert names in stderr


@pytest.mark.parametrize(
    "setpoint", [[], ["--airspeed", "100", "--altitude", "0"]], ids=["default", "explicit"]
)
@pytest.mark.parametrize(
    "text", ["", "- 1\n- 2\n", "just a scalar\n"], ids=["empty", "list", "scalar"]
)
def test_trim_of_a_document_that_is_not_a_map_exits_65(
    tmp_path: Path, capsys: pytest.CaptureFixture[str], text: str, setpoint: list[str]
) -> None:
    # Codex review R-002: astra.trim reads the file itself and raised on these (exit 70).
    config = tmp_path / "config.yaml"
    config.write_text(text, encoding="utf-8")
    code, stdout, stderr = run_main(capsys, "trim", config, *setpoint)
    assert code == 65
    assert stdout == ""
    assert stderr == f"astra trim: config error: config '{config}': the document must be a map\n"


def test_trim_above_the_residual_bound_exits_1(
    monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    # A real solve stopped early: xtol = 0.1 leaves max|residual| near 3e-5, above D8's 1e-6.
    loose = functools.partial(astra.trim.trim_straight_and_level, xtol=0.1)
    monkeypatch.setattr(astra.trim, "trim_straight_and_level", loose)
    code, stdout, stderr = run_main(capsys, "trim", CRUISE)
    assert code == 1
    assert stdout == ""
    assert stderr.startswith("astra trim: not trimmed: max|residual| ")
    assert "exceeds D8's bound 1e-06" in stderr


@pytest.mark.parametrize(
    ("raised", "code"),
    [
        (RuntimeError("trim_straight_and_level did not converge: injected"), 1),
        (_core.Error("x"), 70),
    ],
    ids=["no-convergence", "core-error-is-a-defect"],
)
def test_trim_failures_map_to_their_codes(
    monkeypatch: pytest.MonkeyPatch,
    capsys: pytest.CaptureFixture[str],
    raised: Exception,
    code: int,
) -> None:
    def fail(*_args: object, **_kwargs: object) -> astra.trim.TrimResult:
        raise raised

    monkeypatch.setattr(astra.trim, "trim_straight_and_level", fail)
    exit_code, stdout, _stderr = run_main(capsys, "trim", CRUISE)
    assert exit_code == code
    assert stdout == ""


def test_trim_stdout_failure_exits_74(tmp_path: Path) -> None:
    with open("/dev/full", "w", encoding="utf-8") as full:
        result = astra_script("trim", CRUISE, cwd=tmp_path, stdout=full, stderr=subprocess.PIPE)
    assert result.returncode == 74
    assert result.stderr.startswith("astra trim: cannot write to stdout: ")
