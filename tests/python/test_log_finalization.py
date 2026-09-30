"""ADR-0027 (M6 PR 2, docs/plans/m6-pr2-finalization.md) through Python: astra.log's three states,
strict ``.partial`` refusal, cross-checks and certified rows (R17); replay's strict input, its
``out`` rules and its byte identity across every drain pattern (R16b, R18); ``discard_log()`` as
replay's only delete (R18b, J6); the seeded kill tests, each killed file read in its own process
(R16, J4); confirmation 1's byte scan (J12), confirmation 2 through ``load``, and confirmation 3
(``load(log.path)`` while the ``Simulation`` is alive, with HDF5 file locking on, J5). The core's
side is tests/unit/kernel/run_log_finalization_test.cpp; the tools' is test_montecarlo.py,
test_scenario.py and test_lockstep.py."""

import json
import os
import random
import shutil
import signal
import subprocess
import sys
import time
from collections.abc import Callable
from pathlib import Path
from typing import Any

import h5py
import numpy as np
import pytest
import yaml

from astra import _core, _runkit, log

ROOT = Path(__file__).resolve().parents[2]
CRUISE = ROOT / "aircraft/notional_transport/configs/cruise.yaml"
FAST = ROOT / "tests/configs/log_slow_rate.yaml"  # /rate_1 and /rate_10, Dryden, fast frames
NAN_WRENCH = ROOT / "tests/configs/health_nan_wrench.yaml"
SLOW_RATE = {"plugin": "tests/plugins/slow_rate", "divisor": 10}


def _data(source: Path, *models: dict[str, Any]) -> dict[str, Any]:
    data = yaml.safe_load(source.read_text(encoding="utf-8"))
    data["vehicle"]["models"].extend(dict(model) for model in models)
    return data


def _sim(data: dict[str, Any], path: Path, **log_settings: Any) -> "_core.Simulation":
    config = _core.Config.from_dict({**data, "log": {"path": str(path), **log_settings}})
    return _core.Simulation.from_config(config, _core.default_plugin_dir())


def _partial(path: Path) -> Path:
    return path.with_name(f"{path.name}.partial")


def _closed_log(data: dict[str, Any], path: Path, frames: int) -> Path:
    sim = _sim(data, path)
    assert not sim.run(frames)
    sim.close_log()
    return path


def _bits(frame: Any) -> np.ndarray:
    return np.ascontiguousarray(frame.to_numpy(dtype=np.float64)).view(np.uint64)


def _tampered(source: Path, target: Path, edit: Callable[[Any], None]) -> Path:
    shutil.copyfile(source, target)
    with h5py.File(target, "r+") as f:
        edit(f)
    return target


def _run_child(code: str, *args: str) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        [sys.executable, "-c", code, *args],
        capture_output=True,
        text=True,
        timeout=300,
        check=False,
        cwd=ROOT,
    )


# --- R16: the lifecycle, and confirmation 3 -----------------------------------------------------


def test_log_a_closed_run_is_published_and_loads_while_its_simulation_is_alive(
    tmp_path: Path,
) -> None:
    # Confirmation 3 (ADR-0027 item 1, J5). Non-vacuous only with HDF5 file locking on: h5py then
    # takes a lock on open, which a lock left behind by the writer would refuse.
    assert os.environ.get("HDF5_USE_FILE_LOCKING", "").upper() not in {"FALSE", "0"}
    path = tmp_path / "run.h5"
    sim = _sim(_data(FAST), path)
    assert _partial(path).exists()
    assert not path.exists()
    assert not sim.run(2500)
    sim.close_log()
    assert not _partial(path).exists()
    run = log.load(path)  # the Simulation, and its kept descriptor, are still alive
    assert (run.state, run.published, run.complete) == ("complete", True, True)
    assert run.rows_committed == {1: 2500, 10: 250}
    assert sim.has_log  # still alive here


def test_log_a_refused_publication_leaves_a_complete_unpublished_partial(tmp_path: Path) -> None:
    path = tmp_path / "run.h5"
    sim = _sim(_data(FAST), path)
    assert not sim.run(300)
    path.write_text("someone else's file")
    with pytest.raises(_core.LogError, match="EEXIST"):
        sim.close_log()
    partial = _partial(path)
    # Readable while the Simulation is alive (confirmation 3, the refusal's half).
    run = log.load(partial, allow_incomplete=True)
    assert (run.state, run.published) == ("complete", False)
    assert run.frames_run == 300
    with pytest.raises(log.IncompleteLogError, match="not published by close_log"):
        log.load(partial)
    # A published-looking symlink to it is refused too: the check resolves the name.
    link = tmp_path / "looks_published.h5"
    link.symlink_to(partial)
    with pytest.raises(log.IncompleteLogError, match="not published by close_log"):
        log.load(link)
    assert path.read_text() == "someone else's file"


def test_log_discard_log_through_the_binding(tmp_path: Path) -> None:
    path = tmp_path / "run.h5"
    sim = _sim(_data(FAST), path)
    assert not sim.run(100)
    first = sim.discard_log()
    assert first.outcome == _core.LogDiscardOutcome.DELETED
    assert first.deleted
    assert first.path == str(_partial(path))
    assert not _partial(path).exists()
    assert not path.exists()
    second = sim.discard_log()
    assert (second.outcome, second.path) == (first.outcome, first.path)
    with pytest.raises(_core.LogError, match="discarded"):
        sim.close_log()
    assert sim.step().check == _core.HealthCheck.LOG
    unlogged = _core.Simulation.from_config(
        _core.Config.from_dict(_data(FAST)), _core.default_plugin_dir()
    )
    assert unlogged.discard_log().outcome == _core.LogDiscardOutcome.NO_LOG


# --- R17: cross-checks and certified rows -------------------------------------------------------


def _log_with_slow_rate(tmp_path: Path) -> Path:
    return _closed_log(_data(FAST), tmp_path / "run.h5", 2600)


def _wrong_final_frame(f: Any) -> None:
    f["final_state"].attrs["frame"] = np.uint64(2599)


def _renamed_signal(f: Any) -> None:
    names = [
        n.decode() if isinstance(n, bytes) else str(n) for n in f["rate_1/values"].attrs["names"]
    ]
    names[0] = "renamed.signal"
    f["rate_1/values"].attrs.create("names", names, dtype=h5py.string_dtype())


def _duplicated_name(f: Any) -> None:
    fast = [
        n.decode() if isinstance(n, bytes) else str(n) for n in f["rate_1/values"].attrs["names"]
    ]
    f["rate_10/values"].attrs.create("names", [fast[0]], dtype=h5py.string_dtype())


def _wrong_rows_committed(f: Any) -> None:
    f["rate_10"].attrs["rows_committed"] = np.uint64(259)


@pytest.mark.parametrize(
    ("edit", "named"),
    [
        (_wrong_final_frame, r"final_state\.frame 2599 is not frames_run 2600"),
        (_renamed_signal, r"signal names are not /summary's .*renamed\.signal"),
        (_duplicated_name, r"the rate groups name .* more than once"),
        (_wrong_rows_committed, r"/rate_10 rows_committed 259 is not its row count 260"),
    ],
)
def test_log_cross_checks_refuse_a_hand_edited_finalized_log_naming_the_check(
    tmp_path: Path, edit: Callable[[Any], None], named: str
) -> None:
    source = _log_with_slow_rate(tmp_path)
    copy = _tampered(source, tmp_path / "copy.h5", edit)
    with pytest.raises(log.IncompleteLogError, match=named):
        log.load(copy)
    with pytest.raises(log.IncompleteLogError, match=named):
        log.load(copy, allow_incomplete=True)


def test_log_a_version_one_file_is_refused_naming_the_version(tmp_path: Path) -> None:
    def version_one(f: Any) -> None:
        f.attrs["log_format_version"] = np.uint64(1)

    copy = _tampered(_log_with_slow_rate(tmp_path), tmp_path / "v1.h5", version_one)
    with pytest.raises(log.LogFormatError, match=r"log_format_version .*1.* is not 2"):
        log.load(copy)


_KILL_AFTER = """
import os, signal, sys
import yaml
from astra import _core
d = yaml.safe_load(open(sys.argv[2]))
d["log"] = {"path": sys.argv[1]}
sim = _core.Simulation.from_config(_core.Config.from_dict(d), _core.default_plugin_dir())
assert not sim.run(int(sys.argv[3]))
assert not sim.flush_log()
os.kill(os.getpid(), signal.SIGKILL)
"""


def _killed_partial(tmp_path: Path, frames: int, name: str = "killed.h5") -> Path:
    """A real incomplete file: a child that ran ``frames`` frames, drained, and killed itself."""
    path = tmp_path / name
    child = _run_child(_KILL_AFTER, str(path), str(FAST), str(frames))
    assert child.returncode == -signal.SIGKILL, child.stderr
    assert not path.exists()
    return _partial(path)


def test_log_an_incomplete_file_gives_only_its_certified_rows(tmp_path: Path) -> None:
    partial = _killed_partial(tmp_path, 5000)
    run = log.load(partial, allow_incomplete=True)
    assert (run.state, run.published) == ("incomplete", False)
    assert run.rows_committed[1] >= 3072  # four blocks written; the last one's count may lag
    assert run.rows_committed[1] % 1024 == 0
    reference = log.load(_closed_log(_data(FAST), tmp_path / "reference.h5", 5000))
    for divisor, table in run.rates.items():
        assert len(table) == run.rows_committed[divisor]
        assert np.array_equal(_bits(table), _bits(reference.rates[divisor].iloc[: len(table)]))
    assert run.frames_run is None
    assert run.writes is None
    assert run.final_state == {}


def _drop_rows_committed(f: Any) -> None:
    del f["rate_1"].attrs["rows_committed"]


def _rows_committed_beyond(f: Any) -> None:
    f["rate_1"].attrs["rows_committed"] = np.uint64(f["rate_1/frame"].shape[0] + 1)


def _unallocated_certified_chunk(f: Any) -> None:
    rows = f["rate_1/frame"].shape[0]
    f["rate_1/frame"].resize((rows + 1024,))
    f["rate_1/values"].resize((rows + 1024, f["rate_1/values"].shape[1]))
    f["rate_1"].attrs["rows_committed"] = np.uint64(rows + 1024)


def _altered_certified_frame(f: Any) -> None:
    f["rate_1/frame"][7] = np.uint64(8)


@pytest.mark.parametrize(
    ("edit", "named"),
    [
        (_drop_rows_committed, r"/rate_1: rows_committed is missing"),
        (_rows_committed_beyond, r"/rate_1: rows_committed \d+ exceeds the extent"),
        (_unallocated_certified_chunk, r"/rate_1/frame: the chunk at row \d+, .* is not allocated"),
        (_altered_certified_frame, r"/rate_1: the certified frame column is not 0, 1, 2"),
    ],
)
def test_log_an_incomplete_file_that_fails_a_certified_row_check_is_refused_naming_the_group(
    tmp_path: Path, edit: Callable[[Any], None], named: str
) -> None:
    partial = _killed_partial(tmp_path, 3000)
    copy = _tampered(partial, tmp_path / "copy.h5.partial", edit)
    with pytest.raises(log.IncompleteLogError, match=named):
        log.load(copy, allow_incomplete=True)


def test_log_a_file_killed_right_after_its_create_is_a_typed_refusal(tmp_path: Path) -> None:
    # J4: the core's kill test measured what a kill between the exclusive create and the provenance
    # flush leaves: HDF5's 48-byte superblock alone (run_log_finalization_test.cpp,
    # AKillDuringInit...). The first 48 bytes of a real log are that state; an empty file is the
    # other possibility. Either is a typed error naming the file, never an untyped one.
    source = _closed_log(_data(FAST), tmp_path / "source.h5", 10).read_bytes()
    for name, content in (("superblock.h5.partial", source[:48]), ("empty.h5.partial", b"")):
        partial = tmp_path / name
        partial.write_bytes(content)
        with pytest.raises((log.LogFormatError, log.IncompleteLogError), match=str(partial)):
            log.load(partial, allow_incomplete=True)


# --- Confirmation 2 through load --------------------------------------------------------------


def test_log_a_halted_run_stepped_again_loads_strictly_with_contiguous_frames(
    tmp_path: Path,
) -> None:
    path = tmp_path / "repeated.h5"
    data = yaml.safe_load(NAN_WRENCH.read_text(encoding="utf-8"))
    data["vehicle"]["models"].append(dict(SLOW_RATE))
    sim = _sim(data, path)
    halt = sim.run()
    assert halt.check == _core.HealthCheck.NON_FINITE_WRENCH
    for _ in range(5):
        assert sim.step().frame == halt.frame
    sim.close_log()
    run = log.load(path)
    assert run.frames_run == halt.frame
    assert np.array_equal(run.rates[1].index.to_numpy(), np.arange(halt.frame, dtype=np.uint64))
    assert np.array_equal(
        run.rates[10].index.to_numpy(), np.arange(0, halt.frame, 10, dtype=np.uint64)
    )


# --- R18: replay across every drain pattern -----------------------------------------------------


def _drive(sim: "_core.Simulation", frames: int, pattern: str) -> None:
    if pattern in {"run", "small_ring"}:
        sim.run(frames)
    else:
        for k in range(frames):
            if sim.step():
                break
            if pattern == "step_flush" or k % 5 == 3:
                assert not sim.flush_log()
    sim.close_log()


_PATTERNS = {
    "run": {},
    "step_flush": {},
    "small_ring": {"buffer_frames": 3},
    "irregular": {"buffer_frames": 7},
}


@pytest.mark.parametrize(
    ("label", "models", "frames"),
    [
        ("cruise", (), 12000),
        ("cruise_slow_rate", (SLOW_RATE,), 25000),
        ("halting", (SLOW_RATE, {"plugin": "tests/plugins/nan_wrench", "nan_frame": 12345}), 25000),
    ],
)
def test_log_every_drain_pattern_gives_identical_bytes_and_each_replays_identically(
    tmp_path: Path, label: str, models: tuple[dict[str, Any], ...], frames: int
) -> None:
    data = _data(CRUISE, *models)
    files: dict[str, Path] = {}
    for pattern, settings in _PATTERNS.items():
        path = tmp_path / f"{label}_{pattern}.h5"
        _drive(_sim(data, path, **settings), frames, pattern)
        files[pattern] = path
    reference = files["run"].read_bytes()
    for pattern, path in files.items():
        assert path.read_bytes() == reference, f"{pattern} differs from run"
        assert log.replay(path, tmp_path / f"{label}_{pattern}_replay.h5") is True, pattern
    run = log.load(files["run"])
    if label == "halting":
        assert run.halt is not None
        assert run.halt.frame == 12345


# --- R16b and R18b: replay's out and its one delete path --------------------------------------


def test_log_replay_refuses_an_existing_out_a_stale_partial_and_a_partial_name(
    tmp_path: Path,
) -> None:
    source = _closed_log(_data(CRUISE), tmp_path / "run.h5", 300)
    existing = tmp_path / "existing.h5"
    existing.write_text("never overwritten")
    stale = tmp_path / "stale.h5.partial"
    stale.write_text("an earlier run's partial")
    for out, says in (
        (existing, "already exists; a run log never overwrites"),
        (tmp_path / "stale.h5", "stale partial log belongs to the user"),
        (tmp_path / "named.h5.partial", "ends in .partial"),
    ):
        with pytest.raises(_core.ConfigError, match=says):
            log.replay(source, out)
    assert existing.read_text() == "never overwritten"
    assert stale.read_text() == "an earlier run's partial"
    assert not (tmp_path / "stale.h5").exists()
    assert not (tmp_path / "existing.h5.partial").exists()
    assert not (tmp_path / "named.h5.partial").exists()


def _nudged(tmp_path: Path) -> Path:
    source = _closed_log(_data(CRUISE), tmp_path / "run.h5", 300)

    def nudge(f: Any) -> None:
        f["rate_1/values"][3, 5] += 1.0

    return _tampered(source, tmp_path / "copy.h5", nudge)


def test_log_replay_discards_its_published_out_when_the_bytes_differ(tmp_path: Path) -> None:
    out = tmp_path / "replay.h5"
    assert log.replay(_nudged(tmp_path), out) is False
    assert not out.exists()
    assert not _partial(out).exists()


def test_log_replay_raises_when_its_mismatch_discard_deletes_nothing(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    # J6: out was replaced before the discard, so discard_log() finds another file and deletes
    # nothing; replay must not return False as if out were gone.
    out = tmp_path / "replay.h5"

    def replaced(published: Path, _path: Path) -> bool:
        published.rename(tmp_path / "moved.h5")
        published.write_text("a replacement")
        return False

    monkeypatch.setattr(log, "_identical", replaced)
    with pytest.raises(log.ReplayError, match=r"did not remove it \(MISMATCH"):
        log.replay(_nudged(tmp_path), out)
    assert out.read_text() == "a replacement"
    assert (tmp_path / "moved.h5").exists()


def test_log_replay_discards_its_published_out_when_the_comparison_fails(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    # Codex review of M6 PR 2, F2: the byte comparison runs after the publication; its failure is
    # handled like any other, through discard_log().
    source = _closed_log(_data(CRUISE), tmp_path / "run.h5", 300)
    out = tmp_path / "replay.h5"

    def unreadable(published: Path, _path: Path) -> bool:
        assert published.exists()  # published before the comparison
        raise OSError("the source could not be read")

    monkeypatch.setattr(log, "_identical", unreadable)
    with pytest.raises(OSError, match="the source could not be read") as caught:
        log.replay(source, out)
    assert not out.exists()
    assert not _partial(out).exists()
    assert not getattr(caught.value, "__notes__", [])


def test_log_replay_leaves_a_collided_out_and_discards_only_its_own_partial(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    source = _closed_log(_data(CRUISE), tmp_path / "run.h5", 300)
    out = tmp_path / "replay.h5"
    real = log._rerun  # pyright: ignore[reportPrivateUsage]

    def collide(path: Path, run_log: log.RunLog, sim: Any) -> None:
        real(path, run_log, sim)
        out.write_text("foreign")

    monkeypatch.setattr(log, "_rerun", collide)
    with pytest.raises(_core.LogError, match="EEXIST"):
        log.replay(source, out)
    assert out.read_text() == "foreign"
    assert not _partial(out).exists()


def test_log_replay_discards_its_open_log_on_a_handled_failure(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    source = _closed_log(_data(CRUISE), tmp_path / "run.h5", 300)
    out = tmp_path / "replay.h5"

    def broken(_path: Path, _run_log: log.RunLog, sim: Any) -> None:
        assert not sim.run(50)
        raise RuntimeError("replay driver bug")

    monkeypatch.setattr(log, "_rerun", broken)
    with pytest.raises(RuntimeError, match="replay driver bug"):
        log.replay(source, out)
    assert not out.exists()
    assert not _partial(out).exists()


def test_log_discard_on_failure_notes_a_mismatch_and_deletes_nothing(tmp_path: Path) -> None:
    path = tmp_path / "run.h5"
    sim = _sim(_data(FAST), path)
    assert not sim.run(100)
    _partial(path).rename(tmp_path / "moved.h5.partial")
    _partial(path).write_text("a replacement")
    error = RuntimeError("handled")
    _runkit.discard_on_failure(sim, error)
    assert _partial(path).read_text() == "a replacement"
    assert (tmp_path / "moved.h5.partial").exists()
    assert any("MISMATCH" in note for note in error.__notes__)
    missing = RuntimeError("handled again")
    _partial(path).unlink()
    other = _sim(_data(FAST), tmp_path / "other.h5")
    assert not other.run(10)
    _partial(tmp_path / "other.h5").unlink()
    _runkit.discard_on_failure(other, missing)
    assert any("MISSING" in note for note in missing.__notes__)


# --- Confirmation 1: the byte scan (J12) --------------------------------------------------------


def _signatures(data: bytes, signature: bytes) -> list[int]:
    found, at = [], data.find(signature)
    while at >= 0:
        found.append(at)
        at = data.find(signature, at + 1)
    return found


def test_log_chunk_index_byte_scan(tmp_path: Path) -> None:
    """Confirmation 1's evidence independent of the writer's library (Joe's J12, as corrected at
    its first run: dense attribute storage on /summary, 10 attributes over the compact limit of 8,
    gives a fractal heap and a type-8 v2 B-tree name index, so "no BTHD" was wrong). Every BTHD has
    version 0 and type 8 or 9 (attribute indexes; 10 or 11 would be a chunk index); there is no
    EAHD or FAHD; and the TREE nodes of node type 1 (raw-data chunks) are at least as many as the
    chunked datasets h5py finds."""
    path = _closed_log(_data(CRUISE, SLOW_RATE), tmp_path / "run.h5", 12000)
    data = path.read_bytes()
    chunked: list[str] = []
    with h5py.File(path, "r") as f:
        f.visititems(
            lambda name, obj: (
                chunked.append(name)
                if isinstance(obj, h5py.Dataset) and obj.chunks is not None
                else None
            )
        )
    bthd = [(at, data[at + 4], data[at + 5]) for at in _signatures(data, b"BTHD")]
    tree = [(at, data[at + 4]) for at in _signatures(data, b"TREE")]
    print(f"J12 byte scan of {len(data)} bytes: chunked datasets {sorted(chunked)}")
    print(f"J12 BTHD (offset, version, type): {bthd}")
    print(f"J12 TREE (offset, node type): {tree}")
    for at, version, kind in bthd:
        assert version == 0, f"BTHD at {at}: version {version}"
        assert kind in {8, 9}, f"BTHD at {at}: type {kind} (10 or 11 is a chunk index)"
    assert _signatures(data, b"EAHD") == []
    assert _signatures(data, b"FAHD") == []
    assert len(chunked) >= 4
    assert sum(1 for _, node in tree if node == 1) >= len(chunked)


# --- R16: seeded kills, each file read in its own process ---------------------------------------

_KILL_CHILD = """
import sys, time
import yaml
from astra import _core
d = yaml.safe_load(open(sys.argv[2]))
d["log"] = {"path": sys.argv[1]}
if int(sys.argv[4]) > 0:
    d["log"]["buffer_frames"] = int(sys.argv[4])
sim = _core.Simulation.from_config(_core.Config.from_dict(d), _core.default_plugin_dir())
print("built", flush=True)
started = time.perf_counter()
halt = sim.run(int(sys.argv[3]))
run_s = time.perf_counter() - started
print("closing", run_s, flush=True)
started = time.perf_counter()
sim.close_log()
print("closed", time.perf_counter() - started, flush=True)
"""

_READ_CHILD = """
import json, sys
import numpy as np
from astra import log
path, reference = sys.argv[1], sys.argv[2]
try:
    run = log.load(path, allow_incomplete=True)
except (log.LogFormatError, log.IncompleteLogError) as error:
    print(json.dumps({"outcome": "refused", "names_file": path in str(error),
                      "error": type(error).__name__ + ": " + str(error)}))
    sys.exit(0)
ref = log.load(reference)
wrong = []
for divisor, table in run.rates.items():
    expected = ref.rates[divisor].iloc[: len(table)]
    same = (list(table.columns) == list(expected.columns)
            and np.array_equal(table.index.to_numpy(), expected.index.to_numpy())
            and np.array_equal(np.ascontiguousarray(table.to_numpy()).view(np.uint64),
                               np.ascontiguousarray(expected.to_numpy()).view(np.uint64)))
    if not same:
        wrong.append(divisor)
print(json.dumps({"outcome": "wrong" if wrong else "ok", "wrong": wrong, "state": run.state,
                  "certified": {str(d): n for d, n in run.rows_committed.items()}}))
"""

_KILL_FRAMES = 60000
# A ring whose high-water mark (half its rows) the run never reaches: run() never drains, so every
# block (and its flush and rows_committed) is written inside close_log(), and a kill after
# "closing" lands in the close.
_CLOSE_RING = 2 * _KILL_FRAMES + 2


def _read_killed(path: Path, reference: Path) -> dict[str, Any]:
    """J4: the reader runs in its own process, so a crash inside a stated-limit window is a
    named failure and the file is kept (it stays in tmp_path) for inspection."""
    child = _run_child(_READ_CHILD, str(path), str(reference))
    assert child.returncode == 0, (
        f"reading the killed file {path} crashed or raised an untyped error "
        f"(exit {child.returncode}; the file is kept): {child.stderr}"
    )
    return json.loads(child.stdout.strip().splitlines()[-1])


def _kill_at(path: Path, delay: float, after: str, ring: int) -> dict[str, Any]:
    """Starts the kill child (``ring``: its buffer_frames, 0 for the default), waits for its
    ``after`` line (``built`` or ``closing``), sleeps ``delay`` s and SIGKILLs it. Returns what it
    printed before the kill."""
    process = subprocess.Popen(
        [sys.executable, "-c", _KILL_CHILD, str(path), str(FAST), str(_KILL_FRAMES), str(ring)],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        cwd=ROOT,
    )
    assert process.stdout is not None
    lines: list[str] = []
    while True:
        line = process.stdout.readline()
        if not line:
            break
        lines.append(line.split()[0])
        if lines[-1] == after:
            time.sleep(delay)
            process.send_signal(signal.SIGKILL)
            break
    rest, errors = process.communicate(timeout=300)
    lines.extend(line.split()[0] for line in rest.splitlines() if line)
    return {"lines": lines, "returncode": process.returncode, "stderr": errors}


def test_log_seeded_kills_leave_a_correct_prefix_a_typed_refusal_or_the_published_log(
    tmp_path: Path,
) -> None:
    # Calibration runs: the reference file (the killed runs' prefixes are compared to it, and a
    # kill after the publication must leave exactly its bytes; R18 makes it the same for both ring
    # sizes) and the run and close times of each child.
    timings: dict[int, dict[str, float]] = {}
    for ring in (0, _CLOSE_RING):
        reference = tmp_path / f"reference_{ring}.h5"
        calibration = _run_child(
            _KILL_CHILD, str(reference), str(FAST), str(_KILL_FRAMES), str(ring)
        )
        assert calibration.returncode == 0, calibration.stderr
        lines = calibration.stdout.splitlines()[1:]
        timings[ring] = {line.split()[0]: float(line.split()[1]) for line in lines}
    reference = tmp_path / "reference_0.h5"
    assert reference.read_bytes() == (tmp_path / f"reference_{_CLOSE_RING}.h5").read_bytes()
    run_s, close_s = timings[0]["closing"], timings[0]["closed"]
    close_all_s = timings[_CLOSE_RING]["closed"]
    print(
        f"R16 seeded kills: run {run_s * 1e3:.1f} ms, close {close_s * 1e3:.2f} ms, close with "
        f"every block {close_all_s * 1e3:.1f} ms"
    )
    draws = random.Random(20260929)
    plan = [("built", draws.uniform(0.0, run_s + close_s), 0) for _ in range(12)]
    plan += [("closing", draws.uniform(0.0, close_all_s), _CLOSE_RING) for _ in range(4)]
    outcomes: list[str] = []
    certified_rate1 = 0
    in_close = 0
    for index, (after, delay, ring) in enumerate(plan):
        path = tmp_path / f"kill_{index:02d}.h5"
        killed = _kill_at(path, delay, after, ring)
        lines = killed["lines"]
        if "closing" in lines and "closed" not in lines:
            in_close += 1
        label = f"kill {index} ({after} + {delay * 1e3:.2f} ms, saw {lines})"
        if path.exists():  # after the publication point: exactly the clean close's bytes
            assert not _partial(path).exists(), label
            assert path.read_bytes() == reference.read_bytes(), label
            outcomes.append("published")
            continue
        if not _partial(path).exists():
            outcomes.append("nothing")  # killed before the create (not reachable after "built")
            continue
        read = _read_killed(_partial(path), reference)
        assert read["outcome"] != "wrong", f"{label}: wrong data in {read['wrong']}"
        if read["outcome"] == "refused":
            assert read["names_file"], f"{label}: {read['error']}"
            outcomes.append("refused")
        else:
            certified_rate1 = max(certified_rate1, read["certified"].get("1", 0))
            outcomes.append(f"ok:{read['state']}")
    print(f"R16 seeded kill outcomes: {sorted(outcomes)}; landed in close: {in_close}")
    assert certified_rate1 > 0, "no kill left certified /rate_1 rows: the draws are degenerate"
    assert in_close >= 1, "no kill landed in the close window: the draws are degenerate"


@pytest.mark.parametrize(
    ("frames", "rate", "at_least"), [(500, 1, 0), (2148, 1, 1024), (20485, 10, 1024)]
)
def test_log_a_self_kill_at_a_frame_boundary_leaves_the_certified_prefix(
    tmp_path: Path, frames: int, rate: int, at_least: int
) -> None:
    partial = _killed_partial(tmp_path, frames)
    reference = _closed_log(_data(FAST), tmp_path / "reference.h5", frames)
    read = _read_killed(partial, reference)
    assert read["outcome"] == "ok", read
    assert read["state"] == "incomplete"
    assert read["certified"][str(rate)] >= at_least
