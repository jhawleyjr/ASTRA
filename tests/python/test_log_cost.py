"""R19b (M6 PR 2, docs/plans/m6-pr2-finalization.md; ADR-0027 item 2): the end-to-end cost of a logged
run, measured before and after the per-block flush. Wall clock in the test, the median of three
repeats, printed; no speed is asserted.

Opt-in (Joe's J10): the module skips unless ``ASTRA_MEASURE_LOG_COST=1`` is set, so the full gate and
the acceptance filter do not run it and the gate time recorded against the M6 plan's 1001 s and
59.3 s is PR 2's own cost, not this benchmark's. Run it with::

    ASTRA_MEASURE_LOG_COST=1 .venv/bin/pytest tests/python/test_log_cost.py -s

It calls only ``Simulation.run``, ``step``, ``flush_log`` and ``close_log``, which exist before and
after PR 2, so the same file measures both. Each case checks the frames it ran before its number is
reported."""

import os
import statistics
import time
from collections.abc import Callable
from pathlib import Path
from typing import Any

import pytest
import yaml

from astra import _core, log

pytestmark = pytest.mark.skipif(
    os.environ.get("ASTRA_MEASURE_LOG_COST") != "1",
    reason="R19b's log-cost benchmark is opt-in: set ASTRA_MEASURE_LOG_COST=1 to run it",
)

ROOT = Path(__file__).resolve().parents[2]
CRUISE = ROOT / "aircraft/notional_transport/configs/cruise.yaml"
SLOW_RATE = {"plugin": "tests/plugins/slow_rate", "divisor": 10}
REPEATS = 3


def _cruise(slow_rate: bool) -> dict[str, Any]:
    data = yaml.safe_load(CRUISE.read_text(encoding="utf-8"))
    if slow_rate:
        data["vehicle"]["models"].append(dict(SLOW_RATE))
    return data


def _run_and_close(sim: _core.Simulation, frames: int) -> None:
    assert not sim.run(frames)
    sim.close_log()


def _step_flush_and_close(sim: _core.Simulation, frames: int) -> None:
    for _ in range(frames):
        assert not sim.step()
        assert not sim.flush_log()
    sim.close_log()


def _median_seconds(
    tmp_path: Path,
    name: str,
    data: dict[str, Any],
    frames: int,
    drive: Callable[[_core.Simulation, int], None],
) -> float:
    times: list[float] = []
    for repeat in range(REPEATS):
        path = tmp_path / f"{name}_{repeat}.h5"
        config = _core.Config.from_dict({**data, "log": {"path": str(path)}}, origin=name)
        sim = _core.Simulation.from_config(config, _core.default_plugin_dir())
        started = time.perf_counter()
        drive(sim, frames)
        elapsed = time.perf_counter() - started
        del sim
        assert log.load(path).frames_run == frames  # the frames ran before the number counts
        times.append(elapsed)
        path.unlink()
    return statistics.median(times)


@pytest.mark.parametrize(
    ("name", "slow_rate", "frames", "pattern"),
    [
        ("cruise_1", False, 1, "run"),
        ("cruise_100k", False, 100_000, "run"),
        ("cruise_slow_rate_1", True, 1, "run"),
        ("cruise_slow_rate_100k", True, 100_000, "run"),
        ("cruise_100k_step_flush", False, 100_000, "step_flush"),  # Session's pattern (C-18)
    ],
)
def test_log_cost_end_to_end(
    tmp_path: Path, name: str, slow_rate: bool, frames: int, pattern: str
) -> None:
    drive = _run_and_close if pattern == "run" else _step_flush_and_close
    seconds = _median_seconds(tmp_path, name, _cruise(slow_rate), frames, drive)
    print(
        f"R19b {name}: {frames} frames, {pattern} + close_log, median of {REPEATS}: "
        f"{seconds * 1e3:.2f} ms"
    )
