"""astra.montecarlo (M5 PR 7; docs/plans/m5-tooling.md R14 to R17, R15b, Q17 to Q20).

R14 is the Q19 case itself (64 re-trimmed 60 s runs, under ``just check``). R15 runs small
batches twice, with one worker and with several, into different directories. R16 checks the
sampler against recorded values and its moments. R15b and the carried PR 5/PR 6 rules each have a
test that shows the refusal or the error row firing: most drive ``run_one`` in this process, where
a monkeypatch reaches; the worker-death and interrupt tests drive the real pool.
"""

from __future__ import annotations

import dataclasses
import hashlib
import math
import os
import shutil
import signal
import threading
import time
from collections.abc import Iterator
from pathlib import Path
from typing import Any, cast

import h5py  # pyright: ignore[reportMissingTypeStubs]
import numpy as np
import pyarrow.parquet as pq  # pyright: ignore[reportMissingTypeStubs]
import pytest
import scipy.stats  # pyright: ignore[reportMissingTypeStubs]
import yaml

import astra.trim
from astra import _runkit, log, montecarlo
from astra.scenario import TRIM_OWNED

REPO = Path(__file__).resolve().parents[2]
FIXTURES = Path(__file__).resolve().parent / "montecarlo_fixtures"
CRUISE = "aircraft/notional_transport/configs/cruise.yaml"
STILL_AIR = REPO / "aircraft/notional_transport/configs/cruise_still_air.yaml"

Q19_DISPERSIONS = """\
dispersions:
  physical.mass.mass: {normal: {sigma: 0.02, mode: relative, clip: 3.0}}
  vehicle.models.0.r_aero_b_x_m: {uniform: {lo: 0.2556, hi: 0.4556}}
  physical.wind.seed: seed
"""
TRIM = "trim: {true_airspeed: 230.0, altitude: 10000.0}\n"


def _spec_file(
    tmp_path: Path,
    *,
    config: str = CRUISE,
    trim: bool = True,
    duration: str = "0.05",
    runs: int = 1,
    seed: int = 7,
    dispersions: str = Q19_DISPERSIONS,
    extra: str = "",
) -> Path:
    text = (
        f"config: {config}\n"
        + (TRIM if trim else "")
        + f"duration: {duration}\nruns: {runs}\nseed: {seed}\n"
        + dispersions
        + extra
    )
    path = tmp_path / "spec.yaml"
    path.write_text(text, encoding="utf-8")
    return path


def _spec(tmp_path: Path, **kwargs: Any) -> montecarlo.Spec:
    return montecarlo.load(_spec_file(tmp_path, **kwargs), root=REPO)


def _logs(directory: Path) -> list[Path]:
    return sorted(directory.glob("run_*.h5"))


# --- R14: the Q19 case --------------------------------------------------------------------------


@pytest.fixture
def q19_dir(tmp_path: Path) -> Iterator[Path]:
    # About 33 MB of log per 60 s run, 2.1 GB for the batch: removed after the test, pass or fail
    # (a fixture's teardown runs either way).
    out = tmp_path / "q19"
    try:
        yield out
    finally:
        shutil.rmtree(out, ignore_errors=True)


def test_montecarlo_q19_64_runs_complete(q19_dir: Path) -> None:
    spec = montecarlo.load(FIXTURES / "cruise_64.yaml", root=REPO)
    assert (spec.runs, spec.frames, spec.trim) == (64, 60000, _runkit.TrimSetpoint(230.0, 10000.0))
    returned = montecarlo.run(spec, q19_dir)
    names = sorted(path.name for path in q19_dir.iterdir())
    assert names == [*(montecarlo.log_name(i) for i in range(64)), montecarlo.SUMMARY]
    for path in _logs(q19_dir):
        with cast(Any, h5py.File(path, "r")) as file:
            assert int(file.attrs["complete"]) == 1
    summary = _columns(q19_dir)
    assert len(returned) == 64
    assert summary["run"] == list(range(64))
    assert summary["status"] == ["ok"] * 64
    assert summary["halt_check"] == [None] * 64
    assert summary["error_type"] == [None] * 64
    assert summary["frames_run"] == [60000] * 64


# --- R15: determinism ---------------------------------------------------------------------------


@pytest.fixture(scope="module")
def ok_batches(tmp_path_factory: pytest.TempPathFactory) -> tuple[montecarlo.Spec, Path, Path]:
    """The Q19 dispersions, re-trimmed, 8 runs of 1 s, with one worker and with four."""
    root = tmp_path_factory.mktemp("ok_batches")
    spec = _spec(root, duration="1.0", runs=8, seed=2026)
    montecarlo.run(spec, root / "one", workers=1)
    montecarlo.run(spec, root / "four", workers=4)
    return spec, root / "one", root / "four"


def _assert_identical(one: Path, four: Path) -> None:
    names = sorted(path.name for path in one.iterdir())
    assert names == sorted(path.name for path in four.iterdir())
    assert montecarlo.SUMMARY in names
    for name in names:
        assert (one / name).read_bytes() == (four / name).read_bytes(), name


def test_montecarlo_one_worker_and_four_write_identical_bytes(
    ok_batches: tuple[montecarlo.Spec, Path, Path],
) -> None:
    _, one, four = ok_batches
    assert len(_logs(one)) == 8
    _assert_identical(one, four)


def test_montecarlo_every_drawn_value_is_in_the_stored_config_and_the_summary(
    ok_batches: tuple[montecarlo.Spec, Path, Path],
) -> None:
    spec, one, _four = ok_batches
    summary = _columns(one)
    for index in range(spec.runs):
        seed, values = montecarlo.draw(spec, index)
        assert summary["seed"][index] == seed
        stored = yaml.safe_load(log.load(one / montecarlo.log_name(index)).config)
        for key, value in values.items():
            assert summary[key][index] == value, (index, key)
            assert _runkit.numeric_leaf(stored, key) == value, (index, key)
            assert type(_runkit.numeric_leaf(stored, key)) is type(value)


def test_montecarlo_a_different_master_seed_changes_every_dispersed_value(
    tmp_path: Path, ok_batches: tuple[montecarlo.Spec, Path, Path]
) -> None:
    spec, one, _four = ok_batches
    montecarlo.run(dataclasses.replace(spec, seed=spec.seed + 1), tmp_path / "b", workers=4)
    first = _columns(one)
    other = _columns(tmp_path / "b")
    for key in ("seed", *(dispersion.key for dispersion in spec.dispersions)):
        assert all(a != b for a, b in zip(first[key], other[key], strict=True)), key


# --- R15b: failures are reported ----------------------------------------------------------------

# Mass uniform on [-40000, 40000]: about half the draws are refused by the mass plugin's own
# ConfigError ("mass must be positive"); the rest build and run.
_BAD_MASS = "dispersions:\n  physical.mass.mass: {uniform: {lo: -40000.0, hi: 40000.0}}\n"


def test_montecarlo_an_invalid_drawn_value_is_an_error_row_and_the_batch_continues(
    tmp_path: Path,
) -> None:
    spec = _spec(tmp_path, trim=False, duration="0.01", runs=8, seed=11, dispersions=_BAD_MASS)
    montecarlo.run(spec, tmp_path / "one", workers=1)
    montecarlo.run(spec, tmp_path / "four", workers=4)
    # R15 on a batch with error rows: the summary does not depend on the worker count or out_dir.
    _assert_identical(tmp_path / "one", tmp_path / "four")
    rows = _rows(tmp_path / "one")
    assert [row["run"] for row in rows] == list(range(8))
    errors = [row for row in rows if row["status"] == "error"]
    fine = [row for row in rows if row["status"] != "error"]
    assert errors
    assert fine
    for row in errors:
        assert row["physical.mass.mass"] <= 0.0
        assert row["error_type"] == "ConfigError"
        assert "physical.mass.mass" in row["error_message"]
        assert str(tmp_path / "one") not in row["error_message"]  # the spec's own path may appear
        assert not (tmp_path / "one" / montecarlo.log_name(row["run"])).exists()
        assert row["frames_run"] is None
    for row in fine:
        assert row["physical.mass.mass"] > 0.0
        assert (tmp_path / "one" / montecarlo.log_name(row["run"])).exists()


def _rows(directory: Path) -> list[dict[str, Any]]:
    return cast(list[dict[str, Any]], pq.read_table(directory / montecarlo.SUMMARY).to_pylist())


def _columns(directory: Path) -> dict[str, list[Any]]:
    return cast(dict[str, list[Any]], pq.read_table(directory / montecarlo.SUMMARY).to_pydict())


def test_montecarlo_an_error_message_naming_out_dir_is_recorded_with_a_placeholder(
    tmp_path: Path,
) -> None:
    spec = _spec(tmp_path, trim=False, duration="0.01")
    out = tmp_path / "out"
    out.mkdir()
    (out / montecarlo.log_name(0)).write_bytes(b"not ours")
    row = montecarlo.run_one(spec, 0, out)
    assert row["status"] == "error"
    assert row["error_type"] == "FileExistsError"
    assert row["error_message"].startswith(f"{montecarlo.OUT_DIR_PLACEHOLDER}/run_0000.h5")
    assert str(out) not in row["error_message"]
    assert (out / montecarlo.log_name(0)).read_bytes() == b"not ours"


def test_montecarlo_a_transport_config_is_refused(tmp_path: Path) -> None:
    config = "aircraft/notional_transport/configs/cruise_light_turbulence.yaml"
    with pytest.raises(montecarlo.SpecError, match="transport block"):
        _spec(tmp_path, config=config)


def _children() -> set[int]:
    """This process's spawned pool workers (the resource tracker excluded)."""
    pids: set[int] = set()
    for task in Path("/proc/self/task").iterdir():
        for pid in (task / "children").read_text().split():
            try:
                cmdline = Path(f"/proc/{pid}/cmdline").read_bytes()
            except FileNotFoundError:
                continue
            if b"spawn_main" in cmdline:
                pids.add(int(pid))
    return pids


def _alive(pid: int) -> bool:
    try:
        os.kill(pid, 0)
    except ProcessLookupError:
        return False
    return True


def _abort_batch_when_running(
    out: Path, action: Any, seen: set[int], fired: threading.Event
) -> threading.Thread:
    """A thread that waits for a run's log to appear (a worker is mid-run), records the workers,
    and fires ``action``."""

    def watch() -> None:
        deadline = time.monotonic() + 60.0
        while time.monotonic() < deadline:
            if out.exists() and any(out.glob("run_*.h5")):
                seen.update(_children())
                if seen:
                    action(seen)
                    fired.set()
                    return
            time.sleep(0.005)

    thread = threading.Thread(target=watch, daemon=True)
    thread.start()
    return thread


def _assert_aborted(out: Path, spec: montecarlo.Spec, incomplete: tuple[int, ...], seen: set[int]):
    assert incomplete
    assert not (out / montecarlo.SUMMARY).exists()
    for index in incomplete:
        assert not (out / montecarlo.log_name(index)).exists(), index
    for path in _logs(out):  # a completed run's log stays, and it is complete
        assert int(path.stem.split("_")[1]) not in incomplete
        assert log.load(path).frames_run == spec.frames
    assert not any(_alive(pid) for pid in seen)
    assert not _children()


def test_montecarlo_a_killed_worker_fails_the_batch_naming_incomplete_runs(tmp_path: Path) -> None:
    # A killed worker leaves a partial HDF5 file h5py cannot read (PR 5); the batch removes the log
    # of every run not completed, so what remains on disk is complete logs only.
    spec = _spec(tmp_path, trim=False, duration="300.0", runs=6, seed=3)
    out = tmp_path / "batch"
    seen: set[int] = set()
    fired = threading.Event()

    def kill_one(pids: set[int]) -> None:
        os.kill(min(pids), signal.SIGKILL)

    thread = _abort_batch_when_running(out, kill_one, seen, fired)
    with pytest.raises(montecarlo.BatchError, match="runs not completed: ") as caught:
        montecarlo.run(spec, out, workers=2)
    thread.join()
    assert fired.is_set()
    assert "died (exit code -9) during run " in str(caught.value)
    _assert_aborted(out, spec, caught.value.incomplete, seen)


def test_montecarlo_an_interrupt_fails_the_batch_naming_incomplete_runs(tmp_path: Path) -> None:
    spec = _spec(tmp_path, trim=False, duration="300.0", runs=6, seed=3)
    out = tmp_path / "batch"
    seen: set[int] = set()
    fired = threading.Event()
    main = threading.main_thread().ident
    assert main is not None

    def interrupt(_pids: set[int]) -> None:
        signal.pthread_kill(main, signal.SIGINT)  # the parent's main thread only

    thread = _abort_batch_when_running(out, interrupt, seen, fired)
    with pytest.raises(montecarlo.BatchInterrupted, match="interrupted; runs not completed: ") as c:
        montecarlo.run(spec, out, workers=2)
    thread.join()
    assert fired.is_set()
    _assert_aborted(out, spec, c.value.incomplete, seen)


# --- R16: the sampler ---------------------------------------------------------------------------


def test_montecarlo_run_seed_is_sha256_of_master_and_index() -> None:
    for master, index in [(0, 0), (42, 0), (42, 1), (2**64 - 1, 63)]:
        data = master.to_bytes(8, "big") + index.to_bytes(8, "big")
        expected = int.from_bytes(hashlib.sha256(data).digest()[:8], "big")
        assert montecarlo.run_seed(master, index) == expected
    assert montecarlo.run_seed(2026, 0) == RECORDED_RUN_SEED


# Recorded once from this module's first implementation (2026-09-28) and frozen: any change to the
# seed derivation, the transforms or the draw order changes them.
RECORDED_RUN_SEED = 4468310911779008530
RECORDED_UNIFORM01 = [
    "0x1.236eb09444cb6p-1",
    "0x1.9ac27c61b1e2dp-1",
    "0x1.027c4d1c386b8p-4",
    "0x1.e2feb89414c30p-4",
]
RECORDED_NORMAL = [
    "0x1.ac9b68679fc2fp-2",
    "-0x1.3a7a90574afe7p+0",
    "0x1.10d01b9e03c5ep-2",
    "0x1.f31c8d73d9617p-3",
]
RECORDED_DRAWS = [
    (
        4468310911779008530,
        {
            "physical.mass.mass": "0x1.301bacf52c688p+15",
            "vehicle.models.0.r_aero_b_x_m": "0x1.bb4ddebbf7223p-2",
            "physical.wind.seed": 1050279339861655,
        },
    ),
    (
        9652433674882359189,
        {
            "physical.mass.mass": "0x1.2923127b3113ep+15",
            "vehicle.models.0.r_aero_b_x_m": "0x1.cb4b182e8cf8bp-2",
            "physical.wind.seed": 7754876887341533,
        },
    ),
    (
        1813731075045821313,
        {
            "physical.mass.mass": "0x1.2ee11d9f20dc2p+15",
            "vehicle.models.0.r_aero_b_x_m": "0x1.be9e116faa4d9p-2",
            "physical.wind.seed": 5036061378245273,
        },
    ),
]


def test_montecarlo_fixed_seed_draws_are_the_recorded_values_bitwise() -> None:
    rng = montecarlo.Rng(1)
    assert [rng.uniform01().hex() for _ in range(4)] == RECORDED_UNIFORM01
    rng = montecarlo.Rng(1)
    assert [rng.normal().hex() for _ in range(4)] == RECORDED_NORMAL
    spec = montecarlo.load(FIXTURES / "cruise_64.yaml", root=REPO)
    draws = [montecarlo.draw(spec, index) for index in range(3)]
    recorded = [
        (seed, {k: v.hex() if isinstance(v, float) else v for k, v in values.items()})
        for seed, values in draws
    ]
    assert recorded == RECORDED_DRAWS


def test_montecarlo_uniform01_is_the_top_53_bits_of_a_64_bit_draw() -> None:
    import random

    reference = random.Random(99)
    rng = montecarlo.Rng(99)
    for _ in range(1000):
        assert rng.uniform01() == (reference.getrandbits(64) >> 11) / 2.0**53


N = 100_000


def _within_5_se(samples: np.ndarray, mean: float, variance: float, fourth: float) -> None:
    """Sample mean and variance within 5 standard errors of theory (``fourth`` is the fourth
    central moment, for the variance's standard error)."""
    n = samples.size
    assert abs(samples.mean() - mean) <= 5.0 * math.sqrt(variance / n)
    assert abs(samples.var(ddof=1) - variance) <= 5.0 * math.sqrt((fourth - variance**2) / n)


def test_montecarlo_uniform_moments() -> None:
    rng = montecarlo.Rng(5)
    lo, hi = 0.2556, 0.4556
    samples = np.array([rng.uniform(lo, hi) for _ in range(N)])
    assert samples.min() >= lo
    assert samples.max() < hi
    width = hi - lo
    _within_5_se(samples, (lo + hi) / 2.0, width**2 / 12.0, width**4 / 80.0)


def test_montecarlo_normal_moments_unclipped_and_clipped() -> None:
    rng = montecarlo.Rng(6)
    samples = np.array([rng.truncated_normal(50.0) for _ in range(N)])
    _within_5_se(samples, 0.0, 1.0, 3.0)
    # Clipped at 2 sigma, redrawn: a normal truncated to [-2, 2] (scipy's moments, independent).
    rng = montecarlo.Rng(7)
    samples = np.array([rng.truncated_normal(2.0) for _ in range(N)])
    assert np.abs(samples).max() <= 2.0
    truncated = scipy.stats.truncnorm(-2.0, 2.0)
    variance = float(truncated.var())
    fourth = float(truncated.moment(4))
    _within_5_se(samples, 0.0, variance, fourth)
    assert abs(variance - 1.0) > 5.0 * math.sqrt((fourth - variance**2) / N)  # it is not 1


# --- R17: a run replays -------------------------------------------------------------------------


def test_montecarlo_a_run_log_replays(
    tmp_path: Path, ok_batches: tuple[montecarlo.Spec, Path, Path]
) -> None:
    _, one, _four = ok_batches
    assert log.replay(one / montecarlo.log_name(3), tmp_path / "replay.h5") is True


# --- carried from PR 5 and PR 6 review: each rule is seen to fire -------------------------------


def test_montecarlo_a_run_without_an_explicit_close_log_is_an_error_row_with_no_log(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    monkeypatch.setattr(montecarlo, "_close_log", lambda _sim: None)
    spec = _spec(tmp_path)
    row = montecarlo.run_one(spec, 0, tmp_path)
    assert row["status"] == "error"
    assert row["error_type"] in {"IncompleteLogError", "LogFormatError"}
    assert _logs(tmp_path) == []


def test_montecarlo_a_run_reads_its_log_strictly(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    # Without close_log() the writer still holds the file, so h5py cannot open it strictly or not;
    # the strict read is checked at the call itself.
    calls: list[dict[str, Any]] = []
    real = log.load

    def spy(path: Any, **kwargs: Any) -> log.RunLog:
        calls.append(kwargs)
        return real(path, **kwargs)

    monkeypatch.setattr(log, "load", spy)
    row = montecarlo.run_one(_spec(tmp_path), 0, tmp_path)
    assert row["status"] == "ok"
    assert calls == [{}]  # never allow_incomplete


def test_montecarlo_a_run_that_raises_mid_run_is_an_error_row_with_no_log(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    def broken(sim: Any, frames: int) -> None:
        assert not sim.run(10)
        raise RuntimeError("driver bug")

    monkeypatch.setattr(montecarlo, "_drive", broken)
    row = montecarlo.run_one(_spec(tmp_path), 0, tmp_path)
    assert (row["status"], row["error_type"], row["error_message"]) == (
        "error",
        "RuntimeError",
        "driver bug",
    )
    assert _logs(tmp_path) == []


def test_montecarlo_a_short_run_fails_the_consumer_assertion(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    monkeypatch.setattr(montecarlo, "_drive", lambda sim, frames: sim.run(frames - 1))
    row = montecarlo.run_one(_spec(tmp_path), 0, tmp_path)
    assert (row["status"], row["error_type"]) == ("error", "RunLengthError")
    assert row["error_message"] == "frames_run is 49, not duration / dt = 50 frames"
    assert _logs(tmp_path) == []


def _ceiling_spec(tmp_path: Path) -> montecarlo.Spec:
    """astra.scenario's ceiling-halt setup (tests/scenarios/atmosphere_ceiling_halt.yaml): a climb
    at 100 m/s from about 85950 m leaves ISA 1976's range near frame 500."""
    config = yaml.safe_load(STILL_AIR.read_text(encoding="utf-8"))
    state = config["initial"]["state"]
    state["p_n"][2] = 85950.0
    state["v_b"][0] = 230.0
    state["v_b"][2] = -100.0
    state["euler_nb"][1] = 0.0
    path = tmp_path / "ceiling.yaml"
    path.write_text(yaml.safe_dump(config, sort_keys=False), encoding="utf-8")
    dispersion = "dispersions:\n  initial.state.p_n.2: {uniform: {lo: 85950.0, hi: 85951.0}}\n"
    return _spec(tmp_path, config=str(path), trim=False, duration="1.0", dispersions=dispersion)


def test_montecarlo_a_halt_is_a_row_with_its_halt_and_its_log(tmp_path: Path) -> None:
    spec = _ceiling_spec(tmp_path)
    out = tmp_path / "out"
    out.mkdir()
    row = montecarlo.run_one(spec, 0, out)
    assert row["status"] == "halt"
    assert row["halt_check"] == "OUT_OF_DOMAIN"
    assert row["halt_source"] == "plugins/physical/atmosphere/isa1976"
    assert 0 < row["halt_frame"] < spec.frames
    assert row["frames_run"] == row["halt_frame"]
    assert log.load(out / montecarlo.log_name(0)).halt is not None


def _patched_load(monkeypatch: pytest.MonkeyPatch, change: Any) -> None:
    real = log.load
    monkeypatch.setattr(log, "load", lambda path, **kw: change(real(path, **kw)))


def test_montecarlo_a_halted_log_whose_frames_run_is_not_the_halt_frame_is_an_error_row(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    spec = _ceiling_spec(tmp_path)
    out = tmp_path / "out"
    out.mkdir()
    _patched_load(monkeypatch, lambda run_log: dataclasses.replace(run_log, frames_run=3))
    row = montecarlo.run_one(spec, 0, out)
    assert (row["status"], row["error_type"]) == ("error", "RunLengthError")
    assert row["error_message"].startswith("frames_run is 3, not the halt's frame ")
    assert _logs(out) == []


def test_montecarlo_a_log_halt_is_an_error_row_with_no_log(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    def log_halt(run_log: log.RunLog) -> log.RunLog:
        halt = log.Halt("LOG", "log", 10, 0)
        return dataclasses.replace(run_log, frames_run=10, halt=halt)

    _patched_load(monkeypatch, log_halt)
    row = montecarlo.run_one(_spec(tmp_path), 0, tmp_path)
    assert (row["status"], row["error_type"]) == ("error", "LogHaltError")
    assert _logs(tmp_path) == []


def _trim_owned_keys() -> list[str]:
    keys = []
    for prefix, _reason in TRIM_OWNED:
        leaf = prefix.endswith((".2", "elevator", "throttle", "aileron", "rudder"))
        keys.append(prefix if leaf else f"{prefix}.0")
    return keys


@pytest.mark.parametrize("key", _trim_owned_keys())
def test_montecarlo_with_trim_a_dispersion_of_a_trim_owned_leaf_is_refused(
    tmp_path: Path, key: str
) -> None:
    reason = dict(TRIM_OWNED)[next(p for p, _r in TRIM_OWNED if key.startswith(p))]
    dispersion = f"dispersions:\n  {key}: {{uniform: {{lo: 0.0, hi: 0.01}}}}\n"
    with pytest.raises(montecarlo.SpecError) as caught:
        _spec(tmp_path, dispersions=dispersion)
    assert f"dispersions.{key}: {reason}" in str(caught.value)
    assert "with trim set this dispersion would be discarded" in str(caught.value)
    # Without trim the same key is an ordinary dispersion (q_nb is not in cruise.yaml).
    if "q_nb" not in key:
        assert _spec(tmp_path, trim=False, dispersions=dispersion).dispersions[0].key == key


def test_montecarlo_with_trim_the_wind_stays_dispersible(tmp_path: Path) -> None:
    dispersion = "dispersions:\n  physical.wind.sigma_high: {uniform: {lo: 1.0, hi: 3.0}}\n"
    assert _spec(tmp_path, dispersions=dispersion).trim is not None
    assert _spec(tmp_path).dispersions[2] == montecarlo.Dispersion("physical.wind.seed", "seed")


def test_montecarlo_trim_works_on_a_copy_without_log_or_transport(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    real = astra.trim.trim_straight_and_level
    seen: list[tuple[dict[str, Any], bool]] = []
    log_path = tmp_path / montecarlo.log_name(0)

    def spy(config_path: Any, *args: Any, **kwargs: Any) -> Any:
        seen.append((yaml.safe_load(Path(config_path).read_text()), log_path.exists()))
        return real(config_path, *args, **kwargs)

    monkeypatch.setattr(astra.trim, "trim_straight_and_level", spy)
    spec = _spec(tmp_path)
    row = montecarlo.run_one(spec, 0, tmp_path)
    assert row["status"] == "ok"
    assert len(seen) == 1
    trimmed, log_existed = seen[0]
    assert "log" not in trimmed
    assert "transport" not in trimmed
    assert not log_existed
    _seed, values = montecarlo.draw(spec, 0)
    assert trimmed["physical"]["mass"]["mass"] == values["physical.mass.mass"]  # draws reach trim


# --- the spec's refusals ------------------------------------------------------------------------


@pytest.mark.parametrize(
    ("kwargs", "match"),
    [
        pytest.param({"extra": "workers: 2\n"}, "unknown key 'workers'", id="unknown-key"),
        pytest.param(
            {"dispersions": "dispersions:\n  physical.mass.mas: seed\n"},
            "dispersions.physical.mass.mas: no key 'mas' in the config",
            id="typo",
        ),
        pytest.param(
            {"dispersions": "dispersions:\n  physical.mass: {uniform: {lo: 1.0, hi: 2.0}}\n"},
            "dispersions.physical.mass: the config's value .* is not a number",
            id="not-a-leaf",
        ),
        pytest.param(
            {"dispersions": "dispersions:\n  vehicle.models.9.cl_0: seed\n"},
            "'9' is not an index of a 3-item list",
            id="bad-index",
        ),
        pytest.param(
            {"dispersions": "dispersions:\n  physical.mass.mass: {uniform: {lo: 2.0, hi: 2.0}}\n"},
            "needs lo < hi",
            id="lo-not-below-hi",
        ),
        pytest.param(
            {
                "dispersions": "dispersions:\n  physical.mass.mass: "
                "{normal: {sigma: 0.1, mode: percent, clip: 3.0}}\n"
            },
            "must be absolute or relative",
            id="bad-mode",
        ),
        pytest.param(
            {
                "dispersions": "dispersions:\n  physical.mass.mass: "
                "{normal: {sigma: 0.1, mode: relative}}\n"
            },
            "normal: missing clip",
            id="missing-clip",
        ),
        pytest.param(
            {
                "trim": False,
                "dispersions": "dispersions:\n  vehicle.initial_controls.controls.aileron: "
                "{normal: {sigma: 0.1, mode: relative, clip: 3.0}}\n",
            },
            "relative needs a nonzero config value",
            id="relative-zero-base",
        ),
        pytest.param(
            {"dispersions": "dispersions:\n  physical.mass.mass: seed\n"},
            "seed disperses an integer plugin seed",
            id="seed-on-a-float",
        ),
        pytest.param(
            {"dispersions": "dispersions:\n  physical.mass.mass: {triangular: {lo: 1.0}}\n"},
            "unknown key 'triangular'",
            id="unknown-distribution",
        ),
        pytest.param({"dispersions": "dispersions: {}\n"}, "at least one leaf", id="empty"),
        pytest.param({"extra": "signals: [v0.derived.nope]\n"}, "not a registered", id="signal"),
        pytest.param({"duration": "0.0005"}, "not an exact multiple of dt", id="duration"),
        pytest.param({"duration": "0.0"}, "greater than zero", id="zero-duration"),
        pytest.param({"runs": 0}, "runs: 0 is outside", id="zero-runs"),
        pytest.param({"seed": -1}, "seed: -1 is outside", id="negative-seed"),
        pytest.param({"seed": 2**64}, "is outside \\[0, 18446744073709551616\\)", id="big-seed"),
    ],
)
def test_montecarlo_an_invalid_spec_is_refused_naming_the_key(
    tmp_path: Path, kwargs: dict[str, Any], match: str
) -> None:
    with pytest.raises(montecarlo.SpecError, match=match):
        _spec(tmp_path, **kwargs)


def test_montecarlo_refuses_a_non_empty_out_dir_and_a_bad_worker_count(tmp_path: Path) -> None:
    spec = _spec(tmp_path, trim=False, duration="0.01")
    out = tmp_path / "out"
    out.mkdir()
    (out / "other").write_text("someone else's")
    with pytest.raises(FileExistsError, match="not empty"):
        montecarlo.run(spec, out)
    for workers in (0, True, 1.5):
        with pytest.raises(ValueError, match="workers must be an integer"):
            montecarlo.run(spec, tmp_path / "fresh", workers=workers)  # pyright: ignore[reportArgumentType]
