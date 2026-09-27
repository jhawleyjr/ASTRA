"""astra.log (M5 PR 5; ADR-0025, docs/plans/m5-tooling.md R3, R8, R9): load, its refusals, and
replay.

R8's other half, that the core refuses to log once the *system* libhdf5 was initialized before it,
is PR 4's ``test_simulation.py::test_a_log_is_refused_when_libhdf5_was_initialized_before_astra``
(also selected by ``-k 'log or replay'``); the h5py-first child here is the case that must not
trigger it."""

import json
import math
import shutil
import subprocess
import sys
from collections.abc import Callable
from pathlib import Path
from typing import Any

import h5py
import numpy as np
import pytest
import yaml

from astra import _core, log

ROOT = Path(__file__).resolve().parents[2]
CRUISE = ROOT / "aircraft/notional_transport/configs/cruise.yaml"  # Dryden, seed 42
NAN_WRENCH = ROOT / "tests/configs/health_nan_wrench.yaml"
LOOPBACK = ROOT / "tests/configs/no_alloc_transport.yaml"


def _config(source: Path, path: Path, **log_settings: Any) -> "_core.Config":
    d = yaml.safe_load(source.read_text())
    d["log"] = {"path": str(path), **log_settings}
    return _core.Config.from_dict(d, origin=str(source))


def _sim(source: Path, path: Path, **log_settings: Any) -> "_core.Simulation":
    return _core.Simulation.from_config(
        _config(source, path, **log_settings), _core.default_plugin_dir()
    )


def _cruise_log(path: Path, frames: int = 3000) -> Path:
    sim = _sim(CRUISE, path)
    assert not sim.run(frames)
    sim.close_log()
    return path


def _child(code: str, *args: str) -> dict[str, Any]:
    child = subprocess.run(
        [sys.executable, "-c", code, *args],
        capture_output=True,
        text=True,
        timeout=120,
        check=False,
        cwd=ROOT,
    )
    assert child.returncode == 0, child.stderr
    return json.loads(child.stdout.strip().splitlines()[-1])


def _tampered(source: Path, target: Path, edit: Callable[[Any], None]) -> Path:
    shutil.copyfile(source, target)
    with h5py.File(target, "r+") as f:
        edit(f)
    return target


# --- R8: load -----------------------------------------------------------------------------------


def test_load_gives_a_frame_indexed_table_with_t_units_and_the_run_record(tmp_path: Path) -> None:
    path = tmp_path / "run.h5"
    sim = _sim(CRUISE, path)
    sim.write_input("controls.throttle", 0.55)
    assert not sim.run(1200)
    sim.write_input("controls.elevator", -0.01)
    assert not sim.run(1300)
    signals = sim.registry.signals
    health = {name: value for name, value, _, _ in signals if name.startswith("health.")}
    final = np.array(sim.vehicles[0].state, copy=True)
    sim.close_log()

    run = log.load(path)
    assert run.complete
    assert list(run.rates) == [1]
    table = run.rates[1]
    frames = np.arange(2500, dtype=np.uint64)
    assert table.index.name == "frame"
    assert np.array_equal(table.index.to_numpy(), frames)
    # t is frame * dt (ADR-0010), bitwise, as VirtualClock::t() computes it.
    assert np.array_equal(table["t"].to_numpy(), frames.astype(np.float64) * 0.001)
    assert table["t"].iloc[2499] == 2499 * 0.001
    assert list(table.columns) == ["t", *(name for name, _, _, _ in signals)]
    assert run.units == {name: unit for name, _, unit, _ in signals}
    assert run.dt == 0.001
    assert run.frames_run == 2500
    assert run.halt is None
    stored = _core.Config.from_string(run.config, "stored")
    assert stored.has("sim")
    assert not stored.has("log")  # ADR-0025: the config is stored without its log block
    assert run.core_version == _core.VERSION
    assert run.core_api_version == _core.core_api_version()
    assert run.git_describe
    assert run.writes is not None
    assert run.writes["frame"].tolist() == [0, 1200]
    assert run.writes["name"].tolist() == ["controls.throttle", "controls.elevator"]
    assert run.writes["value"].tolist() == [0.55, -0.01]
    assert run.health == health
    assert run.plugins is not None
    assert len(run.plugins) > 0
    assert all(len(digest) == 64 for digest in run.plugins["sha256"])
    assert run.final_frame == 2500
    assert run.final_after_halt is False
    assert np.array_equal(run.final_state["v0"].to_numpy(), final)
    assert run.minimum is not None
    assert run.maximum is not None
    for name in table.columns[1:]:
        column = table[name].to_numpy()
        assert np.array_equal(run.minimum[name], np.nanmin(column), equal_nan=True), name
        assert np.array_equal(run.maximum[name], np.nanmax(column), equal_nan=True), name


_H5PY_FIRST = """
import json, sys
from pathlib import Path
import h5py
# h5py's own libhdf5 is initialized and used before astra._core (and libastra_core) loads.
with h5py.File(sys.argv[2], "w") as probe:
    probe.attrs["written_by"] = "h5py"
assert "astra._core" not in sys.modules
import yaml
from astra import _core
d = yaml.safe_load(open(sys.argv[3]))
d["log"] = {"path": sys.argv[1]}
sim = _core.Simulation.from_config(_core.Config.from_dict(d), _core.default_plugin_dir())
halt = sim.run(1500)
sim.close_log()
with h5py.File(sys.argv[1], "r") as f:
    complete = int(f.attrs["complete"])
    rows = int(f["rate_1/frame"].shape[0])
from astra import log
run = log.load(sys.argv[1])
maps = Path("/proc/self/maps").read_text()
print(json.dumps({
    "has_log": sim.has_log, "halted": bool(halt), "complete": complete, "rows": rows,
    "loaded_frames_run": run.frames_run, "h5py_hdf5": h5py.version.hdf5_version,
    "system_libhdf5_mapped": "libhdf5_serial.so.103" in maps,
    "h5py_libhdf5_mapped": "h5py.libs/libhdf5-" in maps,
}))
"""


def test_a_log_written_after_h5py_initialized_its_libhdf5_reads_back_with_h5py(
    tmp_path: Path,
) -> None:
    # Joe's PR 4 carry-over (plan R8): h5py's bundled libhdf5 is a separate library, so it does
    # not trip the core's refusal (PR 4 F2), which fires only for the system libhdf5.
    facts = _child(_H5PY_FIRST, str(tmp_path / "run.h5"), str(tmp_path / "probe.h5"), str(CRUISE))
    assert facts["has_log"]
    assert not facts["halted"]
    assert facts["complete"] == 1
    assert facts["rows"] == 1500
    assert facts["loaded_frames_run"] == 1500
    # Two HDF5 runtimes in one process: the system 1.10 (libastra_core) and h5py's own.
    assert facts["system_libhdf5_mapped"]
    assert facts["h5py_libhdf5_mapped"]
    assert not facts["h5py_hdf5"].startswith("1.10.")


# --- R3: row coherence at t_k -------------------------------------------------------------------


def _quaternion_product(a: np.ndarray, b: np.ndarray) -> np.ndarray:
    # Hamilton, scalar first (ADR-0001), row-wise; frames.hpp's quaternion_product.
    aw, ax, ay, az = a.T
    bw, bx, by, bz = b.T
    return np.stack(
        [
            aw * bw - ax * bx - ay * by - az * bz,
            aw * bx + ax * bw + ay * bz - az * by,
            aw * by - ax * bz + ay * bw + az * bx,
            aw * bz + ax * by - ay * bx + az * bw,
        ],
        axis=1,
    )


def test_log_row_k_holds_the_frame_start_state_and_coherent_air_data(tmp_path: Path) -> None:
    path = tmp_path / "run.h5"
    sim = _sim(CRUISE, path)
    before: list[np.ndarray] = []
    for k in range(2100):
        before.append(np.array(sim.vehicles[0].state, copy=True))
        assert not sim.step()
        if k % 500 == 499:
            assert not sim.flush_log()
    sim.close_log()
    run = log.load(path)
    table = run.rates[1]

    # Every state column equals a step-by-step readout of the state before step k, bitwise.
    states = np.stack(before)
    names = run.final_state["v0"].index.tolist()
    assert "v0.state.q_nb_w" in names
    for offset, name in enumerate(names):
        if name:
            assert np.array_equal(table[name].to_numpy(), states[:, offset]), name

    # alpha and beta recomputed in numpy from the logged v_b, q_nb and wind (air_data.hpp's
    # atan2 forms on v_rel_b = v_b - rotate_n_to_b(q_nb, wind_n), frames.hpp:132).
    q = table[[f"v0.state.q_nb_{c}" for c in "wxyz"]].to_numpy()
    v_b = table[[f"v0.state.v_b_{i}" for i in range(3)]].to_numpy()
    wind_n = table[["wind.north", "wind.east", "wind.down"]].to_numpy()
    assert np.any(wind_n != 0.0)  # Dryden: the rotation is exercised
    conjugate = q * np.array([1.0, -1.0, -1.0, -1.0])
    pure = np.column_stack([np.zeros(len(wind_n)), wind_n])
    wind_b = _quaternion_product(_quaternion_product(conjugate, pure), q)[:, 1:]
    v_rel = v_b - wind_b
    alpha = np.arctan2(v_rel[:, 2], v_rel[:, 0])
    beta = np.arctan2(v_rel[:, 1], np.hypot(v_rel[:, 0], v_rel[:, 2]))
    assert np.max(np.abs(alpha - table["v0.derived.alpha"].to_numpy())) <= 1e-12
    assert np.max(np.abs(beta - table["v0.derived.beta"].to_numpy())) <= 1e-12


# --- load's refusals ----------------------------------------------------------------------------


def _drop_last_row(f: Any) -> None:
    rows = f["rate_1/frame"].shape[0]
    f["rate_1/frame"].resize((rows - 1,))
    f["rate_1/values"].resize((rows - 1, f["rate_1/values"].shape[1]))


def _frame_gap(f: Any) -> None:
    frame = f["rate_1/frame"][()]
    frame[5:] += 1
    f["rate_1/frame"][...] = frame


def _frames_run_plus_one(f: Any) -> None:
    f["summary"].attrs["frames_run"] = np.uint64(int(f["summary"].attrs["frames_run"]) + 1)


def _no_complete(f: Any) -> None:
    del f.attrs["complete"]


def _halt_before_frames_run(f: Any) -> None:
    f["summary"].attrs["halt_check"] = "NON_FINITE_WRENCH"
    f["summary"].attrs["halt_source"] = "tests/plugins/nan_wrench"
    f["summary"].attrs["halt_frame"] = np.uint64(3)


def _writes_decrease(f: Any) -> None:
    writes = f["writes"][()]
    writes["frame"] = writes["frame"][::-1]
    f["writes"][...] = writes


def _write_past_frames_run(f: Any) -> None:
    writes = f["writes"][()]
    writes["frame"][-1] = int(f["summary"].attrs["frames_run"]) + 1
    f["writes"][...] = writes


@pytest.mark.parametrize(
    ("edit", "named"),
    [
        (_drop_last_row, r"/rate_1/frame is not 0, 1, \.\.\. below frames_run 40 \(39 rows"),
        (_frame_gap, r"/rate_1/frame is not 0, 1, \.\.\. below frames_run 40 \(40 rows"),
        (_frames_run_plus_one, r"below frames_run 41 \(40 rows, expected 41\)"),
        (_no_complete, r"no complete = 1"),
        (_halt_before_frames_run, r"the halt is at frame 3, not at frames_run 40"),
        (_writes_decrease, r"/writes frames decrease"),
        (_write_past_frames_run, r"a /writes frame is past frames_run 40"),
    ],
)
def test_load_refuses_a_log_inconsistent_with_its_summary_unless_allowed(
    tmp_path: Path, edit: Callable[[Any], None], named: str
) -> None:
    base = tmp_path / "run.h5"
    sim = _sim(CRUISE, base)
    sim.write_input("controls.throttle", 0.55)
    assert not sim.run(10)
    sim.write_input("controls.elevator", -0.01)
    assert not sim.run(30)
    sim.close_log()
    assert log.load(base).frames_run == 40  # the untampered file is accepted

    copy = _tampered(base, tmp_path / "copy.h5", edit)
    with pytest.raises(log.IncompleteLogError, match=named):
        log.load(copy)
    assert log.load(copy, allow_incomplete=True).rates[1].shape[1] > 1


def _synthetic(path: Path, rate_10_frames: list[int]) -> Path:
    # No registered signal has a divisor other than 1 today, so the multi-rate arithmetic is
    # checked on a file written directly in ADR-0025's layout.
    text = h5py.string_dtype()
    with h5py.File(path, "w") as f:
        f.attrs["log_format_version"] = np.uint64(1)
        f.attrs["config"] = "sim:\n  dt: 0.001\n"
        f.attrs["git_describe"] = "synthetic"
        f.attrs["core_version"] = _core.VERSION
        f.attrs["core_api_version"] = np.uint64(_core.core_api_version())
        f.attrs["dt"] = 0.001
        f.attrs["complete"] = np.int64(1)
        for divisor, frames, name in ((1, list(range(25)), "fast"), (10, rate_10_frames, "slow")):
            group = f.create_group(f"rate_{divisor}")
            group["frame"] = np.array(frames, dtype=np.uint64)
            values = group.create_dataset("values", data=np.ones((len(frames), 1)))
            values.attrs.create("names", [name], dtype=text)
            values.attrs.create("units", ["1"], dtype=text)
        summary = f.create_group("summary")
        summary.attrs["frames_run"] = np.uint64(25)
        summary.attrs["halt_check"] = "NONE"
        f["writes"] = np.zeros(0, dtype=[("frame", "<u8"), ("name", text), ("value", "<f8")])
    return path


def test_load_checks_each_rate_group_against_its_own_divisor(tmp_path: Path) -> None:
    run = log.load(_synthetic(tmp_path / "good.h5", [0, 10, 20]))
    assert sorted(run.rates) == [1, 10]
    assert run.rates[10].index.tolist() == [0, 10, 20]
    assert np.array_equal(run.rates[10]["t"].to_numpy(), np.array([0.0, 10 * 0.001, 20 * 0.001]))
    with pytest.raises(log.IncompleteLogError, match=r"/rate_10/frame is not 0, 10"):
        log.load(_synthetic(tmp_path / "short.h5", [0, 10]))


def test_load_refuses_a_file_that_is_not_a_run_log(tmp_path: Path) -> None:
    junk = tmp_path / "junk.h5"
    junk.write_bytes(b"not hdf5")
    with pytest.raises(log.LogFormatError, match="not a readable HDF5 file"):
        log.load(junk)
    other = tmp_path / "other.h5"
    with h5py.File(other, "w") as f:
        f.attrs["complete"] = np.int64(1)
    with pytest.raises(log.LogFormatError, match="log_format_version"):
        log.load(other)


_CLOSE_FAILS = """
import json, resource, signal, sys
from pathlib import Path
import yaml
from astra import _core
path = Path(sys.argv[1])
d = yaml.safe_load(open(sys.argv[2]))
d["log"] = {"path": str(path)}
sim = _core.Simulation.from_config(_core.Config.from_dict(d), _core.default_plugin_dir())
# A real write failure: the file may not grow past its size plus 4 KiB (PR 4's lockstep_child.py).
signal.signal(signal.SIGXFSZ, signal.SIG_IGN)
soft, hard = resource.getrlimit(resource.RLIMIT_FSIZE)
resource.setrlimit(resource.RLIMIT_FSIZE, (path.stat().st_size + 4096, hard))
halt = sim.run(3000)
try:
    sim.close_log()
    error = None
except _core.LogError as caught:
    error = str(caught)
resource.setrlimit(resource.RLIMIT_FSIZE, (soft, hard))
print(json.dumps({"halt": halt.check.name if halt else None, "error": error}))
"""


def test_load_refuses_a_log_whose_close_log_failed(tmp_path: Path) -> None:
    # Plan R6b, the load half (PR 4 covered the core and Session halves).
    path = tmp_path / "run.h5"
    facts = _child(_CLOSE_FAILS, str(path), str(CRUISE))
    assert facts["halt"] == "LOG"
    assert facts["error"] is not None
    assert "File too large" in facts["error"]
    # This file opens as HDF5 but its root object cannot be read, so the refusal names that
    # rather than the missing marker.
    with pytest.raises(log.IncompleteLogError, match="not closed cleanly"):
        log.load(path)
    with pytest.raises(log.IncompleteLogError, match="not closed cleanly"):
        log.load(path, allow_incomplete=True)


_DROPPED_MID_RUN = """
import gc, json, sys
import yaml
from astra import _core

def drive(path, source, frames):
    d = yaml.safe_load(open(source))
    d["log"] = {"path": path}
    sim = _core.Simulation.from_config(_core.Config.from_dict(d), _core.default_plugin_dir())
    for _ in range(frames):
        assert not sim.step()
    raise RuntimeError("the driver failed mid-run")

try:
    drive(sys.argv[1], sys.argv[2], int(sys.argv[3]))
except RuntimeError as error:
    failure = str(error)
gc.collect()
print(json.dumps({"failure": failure}))
"""


def test_a_log_whose_simulation_was_dropped_mid_run_loads_as_complete(tmp_path: Path) -> None:
    # The disclosed gap (Joe, M5 PR 5): a driver that raises mid-run leaves its Simulation to GC,
    # and the destructor finalizes the log. The file is a consistent record of the frames that ran,
    # with complete = 1, so load() cannot tell; a caller compares frames_run and halt itself.
    path = tmp_path / "run.h5"
    facts = _child(_DROPPED_MID_RUN, str(path), str(CRUISE), "600")
    assert facts["failure"] == "the driver failed mid-run"
    run = log.load(path)
    assert run.complete
    assert run.frames_run == 600  # the config's sim.frames is 1000
    assert run.halt is None
    assert len(run.rates[1]) == 600


# --- R9: replay ---------------------------------------------------------------------------------


def test_replay_of_a_dryden_cruise_is_byte_identical(tmp_path: Path) -> None:
    path = _cruise_log(tmp_path / "run.h5")
    out = tmp_path / "replay.h5"
    assert log.replay(path, out) is True
    assert out.exists()


def test_replay_applies_mid_run_writes_at_their_frames(tmp_path: Path) -> None:
    path = tmp_path / "run.h5"
    sim = _sim(CRUISE, path)
    sim.write_input("controls.throttle", 0.55)
    assert not sim.run(700)
    sim.write_input("controls.elevator", -0.01)
    sim.write_input("controls.aileron", 0.002)  # two writes in one frame
    assert not sim.run(1200)
    sim.write_input("controls.elevator", 0.0)
    assert not sim.run(600)
    sim.write_input("controls.throttle", 0.6)  # after the last step
    sim.close_log()
    run = log.load(path)
    assert run.writes is not None
    assert run.writes["frame"].tolist() == [0, 700, 700, 1900, 2500]
    assert log.replay(path, tmp_path / "replay.h5") is True


def test_replay_reproduces_a_nan_wrench_halt(tmp_path: Path) -> None:
    path = tmp_path / "run.h5"
    sim = _sim(NAN_WRENCH, path)
    assert sim.run().check == _core.HealthCheck.NON_FINITE_WRENCH
    sim.close_log()
    run = log.load(path)
    assert run.halt == log.Halt("NON_FINITE_WRENCH", "tests/plugins/nan_wrench", 500, 0)
    assert run.frames_run == 500
    assert log.replay(path, tmp_path / "replay.h5") is True


def test_replay_reproduces_a_non_finite_input_halt(tmp_path: Path) -> None:
    path = tmp_path / "run.h5"
    sim = _sim(CRUISE, path)
    assert not sim.run(100)
    with pytest.raises(_core.Error):
        sim.write_input("controls.elevator", math.nan)
    assert sim.step().check == _core.HealthCheck.NON_FINITE_INPUT
    sim.close_log()
    run = log.load(path)
    assert run.halt is not None
    assert run.halt.check == "NON_FINITE_INPUT"
    assert run.halt.frame == 100
    assert log.replay(path, tmp_path / "replay.h5") is True


def test_replay_refuses_a_lockstep_log(tmp_path: Path) -> None:
    path = tmp_path / "run.h5"
    sim = _sim(LOOPBACK, path)
    assert not sim.run()
    sim.close_log()
    out = tmp_path / "replay.h5"
    with pytest.raises(log.ReplayError, match="transport block"):
        log.replay(path, out)
    assert not out.exists()


def test_replay_refuses_a_log_halted_log(tmp_path: Path) -> None:
    path = tmp_path / "run.h5"
    sim = _sim(CRUISE, path, buffer_frames=4)
    halts = [sim.step() for _ in range(5)]  # step() never drains: the fifth has no free row
    assert not any(halts[:4])
    assert halts[4].check == _core.HealthCheck.LOG
    sim.close_log()
    assert log.load(path).halt == log.Halt("LOG", "log", 4, 0)
    out = tmp_path / "replay.h5"
    with pytest.raises(log.ReplayError, match="halted LOG at frame 4"):
        log.replay(path, out)
    assert not out.exists()


def test_replay_returns_false_when_a_logged_value_differs(tmp_path: Path) -> None:
    path = _cruise_log(tmp_path / "run.h5", frames=300)

    def nudge(f: Any) -> None:
        f["rate_1/values"][3, 5] += 1.0

    copy = _tampered(path, tmp_path / "copy.h5", nudge)
    assert log.load(copy).frames_run == 300  # still complete and consistent
    assert log.replay(copy, tmp_path / "replay.h5") is False


def test_replay_raises_when_the_recorded_halt_is_not_reproduced(tmp_path: Path) -> None:
    path = tmp_path / "run.h5"
    sim = _sim(NAN_WRENCH, path)
    assert sim.run()
    sim.close_log()

    def other_halt(f: Any) -> None:
        f["summary"].attrs["halt_check"] = "OUT_OF_DOMAIN"

    copy = _tampered(path, tmp_path / "copy.h5", other_halt)
    out = tmp_path / "replay.h5"
    with pytest.raises(log.ReplayError, match="halt not reproduced"):
        log.replay(copy, out)
    assert not out.exists()


def test_replay_raises_on_a_halt_before_the_recorded_frames_run(tmp_path: Path) -> None:
    path = tmp_path / "run.h5"
    sim = _sim(NAN_WRENCH, path)
    assert sim.run()
    sim.close_log()

    def earlier_nan(f: Any) -> None:
        config = str(f.attrs["config"])
        assert "nan_frame: 500" in config
        f.attrs["config"] = config.replace("nan_frame: 500", "nan_frame: 200")

    copy = _tampered(path, tmp_path / "copy.h5", earlier_nan)
    with pytest.raises(log.ReplayError, match=r"at frame 200, before the recorded frames_run 500"):
        log.replay(copy, tmp_path / "replay.h5")


@pytest.mark.parametrize(
    ("change", "named"),
    [
        (lambda text: text.rstrip("\n"), "does not end with a newline"),
        (lambda text: f"{text}log:\n  path: elsewhere.h5\n", "has a top-level log key"),
    ],
)
def test_replay_refuses_a_stored_config_that_cannot_take_a_log_block(
    tmp_path: Path, change: Callable[[str], str], named: str
) -> None:
    path = _cruise_log(tmp_path / "run.h5", frames=50)

    def edit(f: Any) -> None:
        f.attrs["config"] = change(str(f.attrs["config"]))

    copy = _tampered(path, tmp_path / "copy.h5", edit)
    with pytest.raises(log.LogFormatError, match=named):
        log.replay(copy, tmp_path / "replay.h5")
