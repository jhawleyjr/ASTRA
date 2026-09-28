"""astra.scenario, the scenario harness (M5 PR 6; ADR-0025, docs/plans/m5-tooling.md Q14 to Q16,
R11 to R13).

R11 is the collection meta-test. R12 is the negative fixtures (``scenario_fixtures/``) and the
collection errors. R13 is the seed scenarios themselves, which run as items under
``tests/scenarios/``. This file also shows each of Joe's PR 5 carry-over assertions firing: the
frames_run count, the halt's check, source and frame, the explicit ``close_log()``, and trim on a
copy without ``log``. It also tests the definition of "final", events replaying, and that the seed
scenarios' step values stay tied to the still-air config.
"""

from __future__ import annotations

import dataclasses
import math
import shutil
import subprocess
import sys
from pathlib import Path
from typing import Any

import numpy as np
import pytest
import yaml

import astra.trim
from astra import _core, log
from astra import scenario as harness

ROOT = Path(__file__).resolve().parents[2]
SCENARIOS = ROOT / "tests/scenarios"
FIXTURES = ROOT / "tests/python/scenario_fixtures"
STILL_AIR = ROOT / "aircraft/notional_transport/configs/cruise_still_air.yaml"
CRUISE = ROOT / "aircraft/notional_transport/configs/cruise.yaml"
STILL_AIR_REL = "aircraft/notional_transport/configs/cruise_still_air.yaml"


def _load(path: Path) -> harness.Scenario:
    return harness.load(path, root=ROOT)


def _write(tmp_path: Path, text: str, name: str = "scenario.yaml") -> Path:
    path = tmp_path / name
    path.write_text(text, encoding="utf-8")
    return path


def _run(scenario: harness.Scenario, tmp_path: Path) -> harness.ScenarioResult:
    return harness.run(scenario, tmp_path / f"{scenario.name}.h5")


def _pytest(*args: str, cwd: Path) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        [sys.executable, "-m", "pytest", "-p", "no:cacheprovider", *args],
        capture_output=True,
        text=True,
        timeout=300,
        check=False,
        cwd=cwd,
    )


# --- R11: collection ----------------------------------------------------------------------------


def test_scenario_collection_has_one_item_per_yaml_file() -> None:
    result = _pytest("--collect-only", "-q", "tests/scenarios", cwd=ROOT)
    assert result.returncode == 0, result.stdout + result.stderr
    collected = [line for line in result.stdout.splitlines() if "::" in line]
    files = sorted(SCENARIOS.rglob("*.yaml"))
    expected = [f"{p.relative_to(ROOT).as_posix()}::{p.stem}" for p in files]
    assert len(collected) == len(files)
    assert sorted(collected) == expected
    assert len(files) == 5  # the Q15 seed list; a new scenario updates this deliberately


def test_an_invalid_scenario_file_is_a_collection_error_not_a_skip(tmp_path: Path) -> None:
    directory = tmp_path / "scenarios"
    directory.mkdir()
    shutil.copyfile(SCENARIOS / "conftest.py", directory / "conftest.py")
    _write(directory, f"config: {STILL_AIR}\nduration: 0.01\ncheks: []\n", "unknown_key.yaml")
    _write(directory, f"config: {STILL_AIR}\nduration: 0.01\n", "suffix.yml")
    result = _pytest("-q", str(directory), cwd=tmp_path)
    output = result.stdout + result.stderr
    assert result.returncode == pytest.ExitCode.INTERRUPTED, output
    assert "unknown key 'cheks'" in output
    assert "scenario files use the .yaml suffix" in output
    assert "2 errors" in output
    assert "skipped" not in output


# --- R12: collection errors ---------------------------------------------------------------------

_CRUISE_HEAD = f"config: {STILL_AIR_REL}\n"


@pytest.mark.parametrize(
    ("text", "named"),
    [
        pytest.param(
            _CRUISE_HEAD + "duration: 0.01\ncheks: []\n", "unknown key 'cheks'", id="top-key"
        ),
        pytest.param(
            _CRUISE_HEAD + "duration: 0.01\nchecks:\n"
            "  - {signal: v0.derived.tas, windw: final, between: {lo: 0.0, hi: 1.0}}\n",
            "checks[0]: unknown key 'windw'",
            id="nested-key",
        ),
        pytest.param(
            _CRUISE_HEAD + "duration: 0.01\ntrim: {true_airspeed: 230.0, altitude: 10000.0, "
            "mach: 0.7}\n",
            "trim: unknown key 'mach'",
            id="trim-key",
        ),
        pytest.param(_CRUISE_HEAD + "checks: []\n", "missing duration", id="no-duration"),
        pytest.param(
            _CRUISE_HEAD + "duration: 0.01\nchecks:\n"
            "  - {signal: v0.derived.altitud, window: final, between: {lo: 0.0, hi: 1.0}}\n",
            "'v0.derived.altitud' is not a registered signal",
            id="unknown-signal",
        ),
        pytest.param(
            _CRUISE_HEAD + "duration: 0.01\nevents:\n  - {at: 0.0, write: controls.flaps, "
            "value: 0.1}\n",
            "'controls.flaps' is not an input",
            id="unknown-input",
        ),
        pytest.param(
            _CRUISE_HEAD + "duration: 0.0105\n",
            "duration: 0.0105 s is not an exact multiple of dt = 0.001 s",
            id="duration-not-multiple",
        ),
        pytest.param(
            _CRUISE_HEAD + "duration: 0.01\nevents:\n  - {at: 0.0005, write: controls.elevator, "
            "value: 0.0}\n",
            "events[0].at: 0.0005 s is not an exact multiple",
            id="event-not-multiple",
        ),
        pytest.param(
            _CRUISE_HEAD + "duration: 0.01\nchecks:\n  - {signal: v0.derived.tas, "
            "window: {from: 0.0, to: 0.0055}, between: {lo: 0.0, hi: 1000.0}}\n",
            "checks[0].window.to: 0.0055 s is not an exact multiple",
            id="window-not-multiple",
        ),
        pytest.param(
            _CRUISE_HEAD + "duration: 0.01\nchecks:\n  - {signal: v0.derived.tas, "
            "window: {from: 0.0, to: 0.02}, between: {lo: 0.0, hi: 1000.0}}\n",
            "needs 0 <= from < to <= duration",
            id="window-past-duration",
        ),
        pytest.param(
            _CRUISE_HEAD + "duration: 0.01\nevents:\n  - {at: 0.01, write: controls.elevator, "
            "value: 0.0}\n",
            "events[0].at: must be in [0, duration)",
            id="event-at-duration",
        ),
        pytest.param(
            _CRUISE_HEAD + "duration: 0.01\nevents:\n  - {at: 0.0, write: controls.elevator, "
            "value: .nan}\n",
            "events[0].value: must be finite",
            id="event-nan",
        ),
        pytest.param(
            _CRUISE_HEAD + "duration: 0.01\noverrides: {initial.state.p_n.3: 1.0}\n",
            "'3' is not an index of a 3-item list",
            id="override-index",
        ),
        pytest.param(
            _CRUISE_HEAD + "duration: 0.01\noverrides: {sim.dtt: 1.0}\n",
            "overrides.sim.dtt: no key 'dtt' in the config",
            id="override-new-key",
        ),
        pytest.param(
            _CRUISE_HEAD + "duration: 0.01\noverrides: {physical.earth: 1.0}\n",
            "is not a number",
            id="override-non-numeric-leaf",
        ),
        pytest.param(
            "config: tests/configs/no_alloc_transport.yaml\nduration: 0.01\n",
            "has a transport block",
            id="transport",
        ),
        pytest.param(
            _CRUISE_HEAD + "duration: 0.01\nexpect_halt: {check: LOG, source: log, frame: 1}\n",
            "expect_halt.check: 'LOG' is not one of",
            id="expect-log-halt",
        ),
        pytest.param(
            "config: tests/configs/health_nan_wrench.yaml\nduration: 0.6\n"
            "expect_halt: {check: NON_FINITE_WRENCH, source: tests/plugins/nan_wrench, "
            "frame: 500}\nchecks:\n  - {signal: health.nan, window: {from: 0.0, to: 0.501}, "
            "between: {lo: 0.0, hi: 0.0}}\n",
            "the run halts at frame 500 (expect_halt)",
            id="window-past-halt",
        ),
        pytest.param(
            _CRUISE_HEAD + "duration: 0.01\nchecks:\n  - {signal: v0.derived.psi, "
            "window: final, angle_within: {target: 0.0, tol: 3.5}}\n",
            "tol: 3.5 is outside [0, pi)",
            id="angle-tol",
        ),
        pytest.param(
            _CRUISE_HEAD + "duration: 0.01\nchecks:\n  - {signal: v0.derived.tas, "
            "window: final, between: {lo: 0.0, hi: 1.0e3}}\n",
            "got '1.0e3' (YAML 1.1 reads an exponent without a sign as a string",
            id="unsigned-exponent",
        ),
    ],
)
def test_an_invalid_scenario_is_refused_naming_the_key(
    tmp_path: Path, text: str, named: str
) -> None:
    with pytest.raises(harness.ScenarioError, match=None) as error:
        _load(_write(tmp_path, text))
    assert named in str(error.value)


def test_a_config_with_a_log_block_is_refused(tmp_path: Path) -> None:
    data = yaml.safe_load(STILL_AIR.read_text(encoding="utf-8"))
    data["log"] = {"path": str(tmp_path / "theirs.h5")}
    config = _write(tmp_path, yaml.safe_dump(data, sort_keys=False), "with_log.yaml")
    with pytest.raises(harness.ScenarioError, match="has a log block"):
        _load(_write(tmp_path, f"config: {config}\nduration: 0.01\n"))


def test_every_haltable_check_has_its_counter() -> None:
    names = {check.name for check in _core.HealthCheck}
    assert set(harness.COUNTER_OF_CHECK) == names - {"NONE", "TRANSPORT", "LOG"}
    assert set(harness.COUNTER_OF_CHECK.values()) == set(harness.WARN_COUNTERS)


# --- R12: run-time failures ---------------------------------------------------------------------


def test_a_violated_check_fails_naming_scenario_check_signal_window_worst_value_and_frame(
    tmp_path: Path,
) -> None:
    scenario = _load(FIXTURES / "violated_check.yaml")
    result = _run(scenario, tmp_path)
    tas = result.log.rates[1]["v0.derived.tas"].to_numpy()
    worst = int(np.argmax(tas))
    message = str(harness.ScenarioFailedError(scenario, result.failures))
    assert "scenario violated_check" in message
    assert "checks[0] v0.derived.tas between [0.0, 100.0]" in message
    assert "over [0, 0.05) s (frames 0..49)" in message
    assert f"worst value {float(tas[worst])!r} at frame {worst}" in message
    assert len(result.failures) == 1


def test_an_unexpected_halt_fails(tmp_path: Path) -> None:
    result = _run(_load(FIXTURES / "unexpected_halt.yaml"), tmp_path)
    assert (
        "unexpected halt: NON_FINITE_WRENCH from tests/plugins/nan_wrench at frame 500"
        in result.failures
    )
    assert "frames_run is 500, not duration / dt = 600 frames" in result.failures


def test_a_nonzero_warn_counter_fails(tmp_path: Path) -> None:
    result = _run(_load(FIXTURES / "warn_counter.yaml"), tmp_path)
    assert result.log.halt is None
    assert result.log.frames_run == 600
    assert any(
        f.startswith("health.nan reaches") and "a nonzero warn counter" in f
        for f in result.failures
    ), result.failures
    assert any(
        f.startswith("health.nan is") and "at the end of the run" in f for f in result.failures
    ), result.failures
    assert len(result.failures) == 2


def test_a_missed_expect_halt_fails(tmp_path: Path) -> None:
    result = _run(_load(FIXTURES / "missed_halt.yaml"), tmp_path)
    assert (
        "missed expect_halt: NON_FINITE_WRENCH from tests/plugins/nan_wrench at frame 300 did "
        "not happen" in result.failures
    )
    assert "frames_run is 400, not expect_halt.frame 300" in result.failures
    assert "health.nan is 0.0 after the expected halt, not >= 1" in result.failures


def test_an_expected_halt_at_the_wrong_frame_fails(tmp_path: Path) -> None:
    result = _run(_load(FIXTURES / "wrong_halt_frame.yaml"), tmp_path)
    assert result.failures == [
        "frames_run is 500, not expect_halt.frame 499",
        "halt NON_FINITE_WRENCH from tests/plugins/nan_wrench at frame 500 is not expect_halt "
        "NON_FINITE_WRENCH from tests/plugins/nan_wrench at frame 499",
    ]


@pytest.fixture(scope="module")
def nan_wrench_halt(tmp_path_factory: pytest.TempPathFactory) -> tuple[harness.Scenario, Any]:
    """The unexpected-halt fixture's log, with the matching expect_halt, which passes."""
    scenario = dataclasses.replace(
        _load(FIXTURES / "unexpected_halt.yaml"),
        expect_halt=harness.ExpectedHalt("NON_FINITE_WRENCH", "tests/plugins/nan_wrench", 500),
    )
    result = _run(scenario, tmp_path_factory.mktemp("nan_wrench"))
    assert result.failures == []
    return scenario, result.log


@pytest.mark.parametrize(
    ("expected", "named"),
    [
        pytest.param(
            harness.ExpectedHalt("NON_FINITE_STATE", "tests/plugins/nan_wrench", 500),
            "is not expect_halt NON_FINITE_STATE",
            id="check",
        ),
        pytest.param(
            harness.ExpectedHalt("NON_FINITE_WRENCH", "tests/plugins/other", 500),
            "is not expect_halt NON_FINITE_WRENCH from tests/plugins/other",
            id="source",
        ),
    ],
)
def test_an_expected_halt_with_the_wrong_check_or_source_fails(
    nan_wrench_halt: tuple[harness.Scenario, Any], expected: harness.ExpectedHalt, named: str
) -> None:
    scenario, run_log = nan_wrench_halt
    failures = harness.verify(dataclasses.replace(scenario, expect_halt=expected), run_log)
    assert len(failures) == 1
    assert named in failures[0]


# --- Joe's PR 5 carry-over assertions, each seen to fire ----------------------------------------


def test_frames_run_must_equal_duration_over_dt_exactly(tmp_path: Path) -> None:
    scenario = _load(FIXTURES / "violated_check.yaml")
    run_log = _run(dataclasses.replace(scenario, checks=()), tmp_path).log
    assert run_log.frames_run == 50
    longer = dataclasses.replace(scenario, frames=51, checks=())
    assert harness.verify(longer, run_log) == ["frames_run is 50, not duration / dt = 51 frames"]


def test_frames_run_must_equal_the_expected_halt_frame(
    nan_wrench_halt: tuple[harness.Scenario, Any],
) -> None:
    scenario, run_log = nan_wrench_halt
    short = dataclasses.replace(run_log, frames_run=499)
    failures = harness.verify(scenario, short)
    assert "frames_run is 499, not expect_halt.frame 500" in failures
    assert "frames_run is 499, not the halt's frame 500" in failures


def test_a_run_without_an_explicit_close_log_is_refused_and_leaves_no_file(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    # The harness keeps its Simulation alive until load() returns, so without close_log() the file
    # is still open and not complete. load() refuses it, and the run never falls back to the
    # destructor's complete = 1.
    monkeypatch.setattr(harness, "_close_log", lambda _sim: None)
    path = tmp_path / "run.h5"
    scenario = dataclasses.replace(_load(FIXTURES / "violated_check.yaml"), checks=())
    with pytest.raises((log.LogFormatError, log.IncompleteLogError)):
        harness.run(scenario, path)
    assert not path.exists()


def test_an_aborted_run_leaves_no_log_that_could_look_complete(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    # A harness failure mid-run drops the Simulation, whose destructor would finalize the file
    # with complete = 1 (PLAN.md's finalization-marker open item), so the harness removes it.
    def broken(sim: Any, scenario: Any) -> None:
        assert not sim.run(10)
        raise RuntimeError("harness bug")

    monkeypatch.setattr(harness, "_drive", broken)
    path = tmp_path / "run.h5"
    with pytest.raises(RuntimeError, match="harness bug"):
        harness.run(_load(FIXTURES / "violated_check.yaml"), path)
    assert not path.exists()


def test_a_run_refuses_an_existing_log_path_and_leaves_it_alone(tmp_path: Path) -> None:
    path = tmp_path / "run.h5"
    path.write_bytes(b"someone else's file")
    scenario = _load(FIXTURES / "violated_check.yaml")
    with pytest.raises(FileExistsError):
        harness.run(scenario, path)
    assert path.read_bytes() == b"someone else's file"


def test_trim_works_on_a_copy_without_log_so_it_never_creates_the_run_file(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    real = astra.trim.trim_straight_and_level
    path = tmp_path / "run.h5"
    seen: list[tuple[dict[str, Any], bool]] = []

    def spy(config_path: Any, *args: Any, **kwargs: Any) -> Any:
        seen.append((yaml.safe_load(Path(config_path).read_text()), path.exists()))
        return real(config_path, *args, **kwargs)

    monkeypatch.setattr(astra.trim, "trim_straight_and_level", spy)
    scenario = dataclasses.replace(_load(SCENARIOS / "cruise_hold.yaml"), frames=10, checks=())
    result = harness.run(scenario, path)
    assert len(seen) == 1
    trimmed_config, log_existed = seen[0]
    assert "log" not in trimmed_config
    assert not log_existed
    assert result.failures == []
    assert path.exists()  # the run itself did write it


# --- "final", events and replay -----------------------------------------------------------------


def test_final_is_the_last_logged_row_not_final_state(tmp_path: Path) -> None:
    # Flat Earth at 100 m/s north: p_n[0] grows 0.1 m per frame, so row frames_run - 1 and
    # /final_state (the state after the last frame) differ.
    text = (
        "config: tests/configs/physics_ballistic_level.yaml\nduration: 0.05\nchecks:\n"
        "  - {signal: v0.state.p_n_0, window: final, between: {lo: -1.0, hi: 0.0}}\n"
    )
    scenario = _load(_write(tmp_path, text))
    result = _run(scenario, tmp_path)
    run_log = result.log
    assert run_log.frames_run == 50
    last = float(run_log.rates[1]["v0.state.p_n_0"].iloc[-1])
    after = float(next(iter(run_log.final_state.values())).iloc[_core.POSITION])
    assert result.failures == [
        f"checks[0] v0.state.p_n_0 between [-1.0, 0.0] over final (frame 49): worst value "
        f"{last!r} at frame 49 (t = 0.049 s)"
    ]
    assert after != last
    assert math.isclose(after - last, 0.1, rel_tol=1e-9)


def test_events_are_written_before_the_step_of_their_frame_and_the_log_replays(
    tmp_path: Path,
) -> None:
    text = (
        _CRUISE_HEAD + "duration: 0.2\nevents:\n"
        "  - {at: 0.1, write: controls.elevator, value: 0.02}\n"
        "  - {at: 0.0, write: controls.throttle, value: 0.5}\n"
    )
    scenario = _load(_write(tmp_path, text))
    assert [(e.frame, e.name) for e in scenario.events] == [
        (0, "controls.throttle"),
        (100, "controls.elevator"),
    ]
    path = tmp_path / "run.h5"
    result = harness.run(scenario, path)
    assert result.failures == []
    writes = result.log.writes
    assert writes is not None
    assert writes["frame"].tolist() == [0, 100]
    assert writes["name"].tolist() == ["controls.throttle", "controls.elevator"]
    assert writes["value"].tolist() == [0.5, 0.02]
    elevator = result.log.rates[1]["v0.controls.elevator"]
    assert elevator.loc[99] == 0.026674474137989946  # the config's trim value
    assert elevator.loc[100] == 0.02
    assert log.replay(path, tmp_path / "replay.h5") is True


# --- the seed scenarios and their base config ---------------------------------------------------


def test_the_still_air_config_is_cruise_without_its_wind() -> None:
    cruise = yaml.safe_load(CRUISE.read_text(encoding="utf-8"))
    still = yaml.safe_load(STILL_AIR.read_text(encoding="utf-8"))
    del cruise["physical"]["wind"]
    assert still == cruise


def _controls(config: dict[str, Any]) -> dict[str, Any]:
    return config["vehicle"]["initial_controls"]["controls"]


# Each step scenario's stated delta (its YAML comment): the event value is the config's value plus
# exactly this, so the config and the scenarios cannot drift apart.
_STEPS = {
    "elevator_step": ("controls.elevator", "elevator", -0.01),
    "aileron_step": ("controls.aileron", "aileron", 0.01),
    "throttle_step": ("controls.throttle", "throttle", 0.1),
}


@pytest.mark.parametrize("name", sorted(_STEPS))
def test_a_step_scenario_writes_the_config_value_plus_its_stated_delta(name: str) -> None:
    scenario = _load(SCENARIOS / f"{name}.yaml")
    write, key, delta = _STEPS[name]
    base = _controls(yaml.safe_load(STILL_AIR.read_text(encoding="utf-8")))[key]
    assert scenario.config_path == STILL_AIR
    assert [(e.frame, e.name, e.value) for e in scenario.events] == [(1000, write, base + delta)]


def test_the_cruise_hold_pitch_target_is_the_config_trim_alpha() -> None:
    scenario = _load(SCENARIOS / "cruise_hold.yaml")
    alpha = yaml.safe_load(STILL_AIR.read_text(encoding="utf-8"))["initial"]["state"]["euler_nb"][1]
    theta = next(c for c in scenario.checks if c.signal == "v0.derived.theta")
    assert theta.a == alpha


# --- check arithmetic ---------------------------------------------------------------------------


def test_angle_within_wraps_across_pi() -> None:
    values = np.array([-math.pi + 1e-3, math.pi - 1e-3, 0.0])
    excess = harness.violation("angle_within", math.pi, 2e-3, values)
    assert excess[0] <= 0.0
    assert excess[1] <= 0.0
    assert excess[2] > 0.0


def test_a_nan_is_always_the_worst_value() -> None:
    values = np.array([1e9, math.nan, -1e9])
    for kind in ("within", "between", "angle_within"):
        excess = harness.violation(kind, 0.0, 1.0, values)
        assert int(np.argmax(excess)) == 1
        assert excess[1] == math.inf


def test_between_bounds_are_inclusive() -> None:
    excess = harness.violation("between", 1.0, 2.0, np.array([1.0, 2.0, 0.999, 2.001]))
    assert list(excess <= 0.0) == [True, True, False, False]
