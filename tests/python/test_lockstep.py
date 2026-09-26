"""M4 acceptance, full length (docs/plans/m4-pr6-lockstep-acceptance.md; M4 plan R1 to R4b, R12b,
Q7,
Q15), and the lifecycle of the ``astra.lockstep`` driver.

Every lockstep run here happens in a ``lockstep_child.py`` process (the plan's "Placement of runs"):
the child has a hard timeout and is killed with its whole process group on expiry, R3's runs are
fresh processes, and this process stays free to act as the killer. This module makes pytest a child
subreaper (``PR_SET_CHILD_SUBREAPER``) so that a dead child's orphans (its autopilot, its signaler)
reparent here and are collected, never left for init.

Reports: each full run writes ``summary.json`` and ``trace.csv`` (per exchange) under
``$ASTRA_LOCKSTEP_REPORT_DIR/<run>`` when that variable is set, else under pytest's tmp directory.
"""

from __future__ import annotations

import contextlib
import csv
import ctypes
import json
import math
import os
import select
import signal
import subprocess
import sys
import tempfile
import time
from collections.abc import Iterator
from dataclasses import dataclass
from pathlib import Path
from typing import Any, NoReturn

import numpy as np
import pytest
import yaml

from astra import _core
from astra import lockstep as ls

ROOT = Path(__file__).resolve().parents[2]
CONFIGS = ROOT / "aircraft/notional_transport/configs"
CRUISE = CONFIGS / "cruise.yaml"
LIGHT = CONFIGS / "cruise_light_turbulence.yaml"
MODERATE = CONFIGS / "cruise_moderate_turbulence.yaml"
CHILD = Path(__file__).with_name("lockstep_child.py")
SETPOINTS = ls.Setpoints(altitude_m=10000.0, airspeed_mps=230.0, heading_rad=0.0)

ALTITUDE_BAND_M = 30.0  # M4 acceptance (architecture §16)
HEADING_BAND_DEG = 2.0
BEATS_HOLD_RATIO = 0.5  # R2d, Joe's Q1 (c): light only
TIMEOUT_MS = 500.0  # M4 Q3: the timing tests' timeout_ms and peer_timeout_ms
ALLOWANCE_MS = max(20.0, 0.1 * TIMEOUT_MS)  # M4 Q3: wake/measurement allowance, never a period
R12B_TOLERANCE = 1e-12  # plan D12: the same doubles, or the same formula in another order
R12B_LAG_FLOOR = 1e-9  # plan D12, Joe's E4: a one-frame lag must exceed this somewhere
FULL_RUN_TIMEOUT_S = 120.0
SHORT_RUN_TIMEOUT_S = 30.0
JITTER_MIN_STOPS = 20
SWEEP_SEEDS = (43, 44, 45, 46, 47, 48, 49, 50)  # Q15; 43 is also R3's different-seed run D

PR_SET_CHILD_SUBREAPER = 36  # <linux/prctl.h>


# --- process supervision --------------------------------------------------------------------------


class Reaper:
    """Collects the processes the children report. Joe's E3: ``waitpid`` ONLY on reported pids,
    never
    ``waitpid(-1)``: reaping a ``Popen``-managed child behind ``Popen``'s back makes ``Popen`` see
    ECHILD and report return code 0, which would silently turn a failure into a pass."""

    def __init__(self) -> None:
        self.pending: set[int] = set()

    def track(self, pid: int) -> None:
        self.pending.add(pid)

    def collect(self, pid: int, timeout_s: float) -> int | None:
        """The raw wait status once ``pid`` (an adopted orphan) is reaped here; None if it is not
        ours (its own parent already reaped it). Fails if it outlives ``timeout_s``."""
        deadline = time.monotonic() + timeout_s
        while True:
            try:
                reaped, status = os.waitpid(pid, os.WNOHANG)
            except ChildProcessError:
                self.pending.discard(pid)
                return None
            if reaped == pid:
                self.pending.discard(pid)
                return status
            if time.monotonic() >= deadline:
                with contextlib.suppress(ProcessLookupError):
                    os.kill(pid, signal.SIGKILL)
                os.waitpid(pid, 0)
                self.pending.discard(pid)
                pytest.fail(f"adopted process {pid} outlived {timeout_s} s; killed")
            time.sleep(0.001)


@pytest.fixture(scope="module")
def reaper() -> Iterator[Reaper]:
    libc = ctypes.CDLL(None, use_errno=True)
    if libc.prctl(PR_SET_CHILD_SUBREAPER, 1, 0, 0, 0) != 0:
        pytest.fail(f"prctl(PR_SET_CHILD_SUBREAPER) failed: errno {ctypes.get_errno()}")
    ls.remove_stale_segments()
    the_reaper = Reaper()
    try:
        yield the_reaper
    finally:
        leftovers = sorted(the_reaper.pending)
        for pid in leftovers:
            the_reaper.collect(pid, 5.0)
        libc.prctl(PR_SET_CHILD_SUBREAPER, 0, 0, 0, 0)
        ls.remove_stale_segments()


class Host:
    """One ``lockstep_child.py`` process in its own session (process group)."""

    def __init__(self, reaper: Reaper, args: list[str], timeout_s: float) -> None:
        self.reaper = reaper
        # Held for the child's lifetime and closed by close(), so not a `with` block.
        self.stderr = tempfile.TemporaryFile()  # noqa: SIM115
        self.proc = subprocess.Popen(
            [sys.executable, str(CHILD), *args],
            stdout=subprocess.PIPE,
            stderr=self.stderr,
            start_new_session=True,
        )
        self.deadline = time.monotonic() + timeout_s
        self.buffer = b""
        self.pids: dict[str, int] = {}
        self.lines: list[str] = []
        self.eof = False

    def _stderr_text(self) -> str:
        self.stderr.seek(0)
        return self.stderr.read().decode(errors="replace")

    def close(self) -> None:
        if self.proc.stdout is not None:
            self.proc.stdout.close()
        self.stderr.close()

    def _fail(self, message: str) -> NoReturn:
        text = self._stderr_text()
        self.close()
        pytest.fail(f"{message}; stderr:\n{text}")

    def _hard_timeout(self) -> None:
        with contextlib.suppress(ProcessLookupError):
            os.killpg(self.proc.pid, signal.SIGKILL)
        self.proc.wait()
        for pid in list(self.pids.values()):
            self.reaper.collect(pid, 5.0)
        self._fail("lockstep child timed out (hard timeout)")

    def next_line(self) -> str | None:
        assert self.proc.stdout is not None
        fd = self.proc.stdout.fileno()
        while b"\n" not in self.buffer:
            if self.eof:
                return None
            remaining = self.deadline - time.monotonic()
            if remaining <= 0:
                self._hard_timeout()
            ready, _, _ = select.select([fd], [], [], remaining)
            if not ready:
                continue
            chunk = os.read(fd, 65536)
            if not chunk:
                self.eof = True
                if not self.buffer:
                    return None
                self.buffer += b"\n"
            self.buffer += chunk
        raw, self.buffer = self.buffer.split(b"\n", 1)
        line = raw.decode()
        self.lines.append(line)
        parts = line.split()
        if len(parts) == 3 and parts[0] == "PID":
            self.pids[parts[1]] = int(parts[2])
            self.reaper.track(int(parts[2]))
        return line

    def expect(self, word: str) -> str:
        while True:
            line = self.next_line()
            if line is None:
                self.proc.wait()
                self._fail(f"child ended before {word!r}")
            if line.split()[:1] == [word]:
                return line

    def finish(self) -> dict[str, Any]:
        """Reads to EOF, reaps the child (bounded), collects its orphans; the RESULT payload."""
        while self.next_line() is not None:
            pass
        try:
            status = self.proc.wait(max(self.deadline - time.monotonic(), 0.0))
        except subprocess.TimeoutExpired:
            self._hard_timeout()
            raise
        for pid in list(self.pids.values()):
            self.reaper.collect(pid, 5.0)
        if status != 0:
            self._fail(f"lockstep child exited {status}")
        for line in reversed(self.lines):
            if line.startswith("RESULT "):
                self.close()
                return json.loads(line[len("RESULT ") :])
        self._fail("lockstep child printed no RESULT")

    def kill_child_only(self) -> int:
        """SIGKILLs the child process itself (not its group) and reaps it; returns t_kill."""
        t_kill = time.monotonic_ns()
        os.kill(self.proc.pid, signal.SIGKILL)
        self.proc.wait(10)
        while self.next_line() is not None:  # any PID line still buffered is tracked
            pass
        self.close()
        return t_kill


# --- executables and shared values ----------------------------------------------------------------


def _executable(variable: str, relative: str) -> Path:
    """Plan D4: $variable, else the imported module's build tree. Missing is a failure, not a
    skip."""
    value = os.environ.get(variable)
    path = Path(value) if value else Path(_core.default_plugin_dir()).parent / relative
    if not (path.is_file() and os.access(path, os.X_OK)):
        pytest.fail(f"{path} not found or not executable (set {variable} or run `just python`)")
    return path


@pytest.fixture(scope="module")
def binaries() -> dict[str, str]:
    return {
        "--autopilot": str(_executable("ASTRA_AUTOPILOT", "embedded/autopilot/astra_autopilot")),
        "--echo-peer": str(_executable("ASTRA_ECHO_PEER", "tests/lockstep/astra_echo_peer")),
    }


def _child_args(binaries: dict[str, str], scenario: str, config: Path, *extra: str) -> list[str]:
    args = [scenario, "--config", str(config)]
    for flag, path in binaries.items():
        args += [flag, path]
    return [*args, *extra]


@pytest.fixture(scope="module")
def trim() -> ls.AutopilotTrim:
    return ls.trim_for(LIGHT, SETPOINTS)


@pytest.fixture(scope="module")
def report_dir(tmp_path_factory: pytest.TempPathFactory) -> Path:
    value = os.environ.get("ASTRA_LOCKSTEP_REPORT_DIR")
    path = Path(value) if value else tmp_path_factory.mktemp("lockstep")
    path.mkdir(parents=True, exist_ok=True)
    return path


# --- test-side flight arithmetic (independent of core/ packing) -----------------------------------


def euler_321(q: np.ndarray) -> tuple[np.ndarray, np.ndarray, np.ndarray]:
    """(psi, theta, phi) from q_nb rows (w, x, y, z), ADR-0001 3-2-1, via the n->b DCM (S&L 1.3-11,
    1.8-16/18): C11 = w2+x2-y2-z2, C12 = 2(xy+wz), C13 = 2(xz-wy), C23 = 2(yz+wx),
    C33 = w2-x2-y2+z2."""
    w, x, y, z = q[:, 0], q[:, 1], q[:, 2], q[:, 3]
    c11 = w * w + x * x - y * y - z * z
    c12 = 2.0 * (x * y + w * z)
    c13 = 2.0 * (x * z - w * y)
    c23 = 2.0 * (y * z + w * x)
    c33 = w * w - x * x - y * y + z * z
    return np.arctan2(c12, c11), -np.arcsin(np.clip(c13, -1.0, 1.0)), np.arctan2(c23, c33)


@dataclass
class Run:
    name: str
    result: dict[str, Any]
    data: bytes
    recording: ls.Recording
    metrics: dict[str, Any]


def flight_metrics(recording: ls.Recording, trim: ls.AutopilotTrim) -> dict[str, Any]:
    """R2's margins over every recorded state, plus what the plan's "Reports" column lists."""
    states = recording.states
    y = states["y"]
    altitude_error = y[:, 2] - SETPOINTS.altitude_m
    psi, _, _ = euler_321(y[:, 6:10])
    heading_error_deg = np.degrees(np.arctan2(np.sin(psi), np.cos(psi)))  # wrapped
    i_alt = int(np.argmax(np.abs(altitude_error)))
    i_psi = int(np.argmax(np.abs(heading_error_deg)))
    replies = recording.replies
    throttle = replies["throttle"][:, 0]
    limits = {"elevator": 0.3, "aileron": 0.35, "rudder": 0.35}
    metrics: dict[str, Any] = {
        "states": len(states),
        "max_altitude_error_m": float(abs(altitude_error[i_alt])),
        "max_altitude_error_frame": int(states["frame"][i_alt]),
        "altitude_margin_m": ALTITUDE_BAND_M - float(abs(altitude_error[i_alt])),
        "max_heading_error_deg": float(abs(heading_error_deg[i_psi])),
        "max_heading_error_frame": int(states["frame"][i_psi]),
        "heading_margin_deg": HEADING_BAND_DEG - float(abs(heading_error_deg[i_psi])),
        "throttle_min": float(throttle.min()),
        "throttle_max": float(throttle.max()),
        "throttle_max_from_trim": float(np.max(np.abs(throttle - trim.throttle))),
        "throttle_max_step": float(np.max(np.abs(np.diff(throttle)))) if len(throttle) > 1 else 0.0,
        "throttle_at_limit": int(np.count_nonzero((throttle <= 0.0) | (throttle >= 1.0))),
        "beta_min_deg": float(np.degrees(recording.requests["beta"].min())),
        "beta_max_deg": float(np.degrees(recording.requests["beta"].max())),
        "tas_min": float(recording.requests["tas"].min()),
        "tas_max": float(recording.requests["tas"].max()),
    }
    for channel, limit in limits.items():
        values = replies[channel]
        metrics[f"{channel}_min"] = float(values.min())
        metrics[f"{channel}_max"] = float(values.max())
        metrics[f"{channel}_at_limit"] = int(np.count_nonzero(np.abs(values) >= limit))
    return metrics


def write_report(directory: Path, run: Run) -> None:
    directory.mkdir(parents=True, exist_ok=True)
    (directory / "summary.json").write_text(
        json.dumps({"result": run.result, "metrics": run.metrics}, indent=2) + "\n"
    )
    requests = run.recording.requests
    replies = run.recording.replies
    states = run.recording.states
    by_frame = {int(f): i for i, f in enumerate(states["frame"])}
    with open(directory / "trace.csv", "w", newline="") as f:
        writer = csv.writer(f)
        writer.writerow(
            [
                "frame",
                "t_s",
                "altitude_error_m",
                "heading_error_deg",
                "throttle",
                "elevator_rad",
                "aileron_rad",
                "rudder_rad",
                "beta_deg",
                "tas_mps",
            ]
        )
        for k in range(len(replies)):
            frame = int(requests["frame"][k])
            y = states["y"][by_frame[frame]]
            psi, _, _ = euler_321(y[np.newaxis, 6:10])
            writer.writerow(
                [
                    frame,
                    f"{frame * 0.001:.3f}",
                    f"{y[2] - SETPOINTS.altitude_m:.6f}",
                    f"{math.degrees(math.atan2(math.sin(psi[0]), math.cos(psi[0]))):.6f}",
                    f"{replies['throttle'][k, 0]:.6f}",
                    f"{replies['elevator'][k]:.6f}",
                    f"{replies['aileron'][k]:.6f}",
                    f"{replies['rudder'][k]:.6f}",
                    f"{math.degrees(requests['beta'][k]):.6f}",
                    f"{requests['tas'][k]:.4f}",
                ]
            )


RUN_SPECS: dict[str, tuple[Path, tuple[str, ...]]] = {
    "A_light_autopilot": (LIGHT, ()),
    "B_light_autopilot_repeat": (LIGHT, ()),
    "C_light_autopilot_jitter": (LIGHT, ("--jitter-seed", "7")),
    "E_light_hold": (LIGHT, ("--peer", "hold")),
    "F_moderate_autopilot": (MODERATE, ()),
    "G_moderate_hold": (MODERATE, ("--peer", "hold")),
    **{f"sweep_seed_{seed}": (LIGHT, ("--seed", str(seed))) for seed in SWEEP_SEEDS},
}


class Runs:
    """Each full run once per module, on first use (so `-k` selects only what a test needs)."""

    def __init__(
        self, reaper: Reaper, binaries: dict[str, str], trim: ls.AutopilotTrim, report_dir: Path
    ) -> None:
        self.reaper = reaper
        self.binaries = binaries
        self.trim = trim
        self.report_dir = report_dir
        self.cache: dict[str, Run] = {}

    def __getitem__(self, name: str) -> Run:
        if name not in self.cache:
            config, extra = RUN_SPECS[name]
            out = self.report_dir / name
            out.mkdir(parents=True, exist_ok=True)
            host = Host(
                self.reaper,
                _child_args(self.binaries, "run", config, "--out", str(out), *extra),
                FULL_RUN_TIMEOUT_S,
            )
            result = host.finish()
            data = (out / "recorder.bin").read_bytes()
            recording = ls.parse_recording(data)
            run = Run(name, result, data, recording, flight_metrics(recording, self.trim))
            write_report(out, run)
            self.cache[name] = run
        return self.cache[name]


@pytest.fixture(scope="module")
def runs(
    reaper: Reaper, binaries: dict[str, str], trim: ls.AutopilotTrim, report_dir: Path
) -> Runs:
    return Runs(reaper, binaries, trim, report_dir)


def assert_lockstep_healthy(run: Run, peer: str) -> None:
    """R1's counters and stream shape (every full run is 10,000 exchanges, 100,001 states)."""
    result = run.result
    assert result["halt"] is None, result["halt"]
    assert result["frame"] == 100000
    assert result["exchanges"] == 10000.0
    assert result["missed"] == 0.0
    requests, replies, states = run.recording.requests, run.recording.replies, run.recording.states
    assert (len(requests), len(replies), len(states)) == (10000, 10000, 100001)
    assert np.all(requests["magic"] == ls.ICD_MAGIC)
    assert np.all(requests["version"] == ls.ICD_VERSION)
    assert np.all(replies["magic"] == ls.ICD_MAGIC)
    assert np.all(replies["version"] == ls.ICD_VERSION)
    assert np.array_equal(requests["frame"], np.arange(0, 100000, 10, dtype=np.uint64))
    assert np.array_equal(replies["frame"], requests["frame"])  # every reply echoes its request
    assert np.array_equal(states["frame"], np.arange(0, 100001, dtype=np.uint64))
    assert result["peer_returncode"] == 0, result["peer_stderr"]
    summary = result["peer_summary"]
    assert summary is not None
    assert summary["name"] == peer
    assert summary["handled"] == 10000  # the peer's own, independent count


# --- configs --------------------------------------------------------------------------------------


def _yaml(path: Path) -> dict[str, Any]:
    return yaml.safe_load(path.read_text())


@pytest.mark.parametrize(("path", "sigma", "w20"), [(LIGHT, 0.15, 15.0), (MODERATE, 1.5, 30.0)])
def test_turbulence_configs_differ_from_cruise_only_where_intended(
    path: Path, sigma: float, w20: float
) -> None:
    cruise = _yaml(CRUISE)
    config = _yaml(path)
    assert config["physical"]["wind"] == {
        "plugin": "plugins/physical/wind/dryden",
        "seed": 42,
        "w20_knots": w20,
        "sigma_high": sigma,
    }
    assert config["sim"]["frames"] == 100000
    assert "segment" not in config["transport"]
    for data in (cruise, config):
        data["physical"].pop("wind")
        data["sim"].pop("frames")
    config.pop("transport")
    assert config == cruise


@pytest.mark.parametrize("path", [LIGHT, MODERATE])
def test_trim_matches_config_initial_state(path: Path) -> None:
    """The ctest runs take the trim from the config; this links it to astra.trim (plan, Config
    row)."""
    trim = ls.trim_for(path, SETPOINTS)
    config = _yaml(path)
    controls = config["vehicle"]["initial_controls"]["controls"]
    assert abs(trim.elevator_rad - controls["elevator"]) <= 1e-12
    assert abs(trim.throttle - controls["throttle"]) <= 1e-12
    assert abs(trim.theta_rad - config["initial"]["state"]["euler_nb"][1]) <= 1e-12
    assert config["initial"]["state"]["euler_nb"][0] == 0.0  # psi 0: the heading setpoint


def test_config_alone_refuses_to_run() -> None:
    """transport.segment is the driver's: the file alone is a ConfigError naming it."""
    with pytest.raises(_core.ConfigError, match=r"transport\.segment"):
        _core.Simulation.from_config(_core.Config.from_file(str(LIGHT)), _core.default_plugin_dir())


# --- driver units ---------------------------------------------------------------------------------


def test_segment_name_format_and_uniqueness() -> None:
    names = {ls.new_segment_name() for _ in range(100)}
    assert len(names) == 100
    for name in names:
        prefix, pid, nonce = name.split("_")
        assert (prefix, int(pid)) == ("astra", os.getpid())
        assert len(nonce) == 16
        assert nonce.isalnum()


def test_remove_stale_segments_is_conservative(tmp_path: Path) -> None:
    dead = subprocess.Popen(["true"])
    dead.wait()
    stale = tmp_path / f"astra_{dead.pid}_deadbeef"
    live = tmp_path / f"astra_{os.getpid()}_cafe"
    foreign = tmp_path / "astra_notapid_x"
    for path in (stale, live, foreign):
        path.write_bytes(b"")
    assert ls.remove_stale_segments(tmp_path) == [stale.name]
    assert live.exists()
    assert foreign.exists()
    assert not stale.exists()


def test_load_config_rejects_a_segment_in_the_file(tmp_path: Path) -> None:
    data = _yaml(LIGHT)
    data["transport"]["segment"] = "astra_1_x"
    path = tmp_path / "with_segment.yaml"
    path.write_text(yaml.safe_dump(data))
    with pytest.raises(ls.LockstepError, match="segment"):
        ls.load_config(path, segment=ls.new_segment_name())


def test_exchange_recorder_bytes_is_a_copy_and_none_without_transport() -> None:
    sim = _core.Simulation.from_config(
        _core.Config.from_file(str(CRUISE)), _core.default_plugin_dir()
    )
    before = sys.getrefcount(sim)
    assert sim.exchange_recorder_bytes() is None
    assert sys.getrefcount(sim) == before


def _offsets(dtype: np.dtype[Any]) -> dict[str, int]:
    fields = dtype.fields
    assert fields is not None
    return {name: int(info[1]) for name, info in fields.items()}


def test_recording_decoder_layout(
    reaper: Reaper, binaries: dict[str, str], report_dir: Path
) -> None:
    """Critique C-8: every decoder offset against a hand-counted table, then an echo-mode session
    whose reply is a known function of the request (tests/lockstep/echo_peer.cpp)."""
    request_offsets = {
        "magic": 0,
        "version": 4,
        "frame": 8,
        "t": 16,
        "phi": 24,
        "theta": 32,
        "psi": 40,
        "p": 48,
        "q": 56,
        "r": 64,
        "u": 72,
        "v": 80,
        "w": 88,
        "alpha": 96,
        "beta": 104,
        "tas": 112,
        "cas": 120,
        "mach": 128,
        "altitude": 136,
        "vertical_speed": 144,
        "lat": 152,
        "lon": 160,
        "ax": 168,
        "ay": 176,
        "az": 184,
        "status_flags": 192,
        "reserved": 196,
    }
    reply_offsets = {
        "magic": 0,
        "version": 4,
        "frame": 8,
        "elevator": 16,
        "aileron": 24,
        "rudder": 32,
        "throttle": 40,
        "mode_flags": 56,
        "fault_flags": 60,
    }
    header_offsets = {
        "magic": 0,
        "version": 4,
        "request_size": 8,
        "reply_size": 12,
        "state_size": 16,
        "reserved": 20,
        "request_count": 24,
        "reply_count": 32,
        "state_count": 40,
    }
    for dtype, table, size in (
        (ls.REQUEST_DTYPE, request_offsets, 200),
        (ls.REPLY_DTYPE, reply_offsets, 64),
        (ls.RECORDER_HEADER_DTYPE, header_offsets, 48),
    ):
        assert dtype.itemsize == size
        assert _offsets(dtype) == table
    assert ls.STATE_DTYPE.itemsize == 112
    assert _offsets(ls.STATE_DTYPE) == {"frame": 0, "y": 8}

    out = report_dir / "decoder_echo"
    out.mkdir(parents=True, exist_ok=True)
    host = Host(
        reaper,
        _child_args(
            binaries, "run", MODERATE, "--peer", "echo", "--frames", "100", "--out", str(out)
        ),
        SHORT_RUN_TIMEOUT_S,
    )
    result = host.finish()
    recording = ls.parse_recording((out / "recorder.bin").read_bytes())
    # Echo replies throttle = {p, q}: equal (both 0) at frame 0, unequal once the rates move, so the
    # sim halts on the throttle rule at frame 10, after recording that reply (ADR-0022).
    assert result["halt"] is not None
    assert result["halt"]["check"] == "TRANSPORT"
    assert result["halt"]["frame"] == 10
    assert len(recording.requests) == len(recording.replies) == 2
    for request, reply in zip(recording.requests, recording.replies, strict=True):
        assert reply["frame"] == request["frame"]
        assert reply["elevator"] == request["phi"]
        assert reply["aileron"] == request["theta"]
        assert reply["rudder"] == request["psi"]
        assert reply["throttle"][0] == request["p"]
        assert reply["throttle"][1] == request["q"]
        assert reply["fault_flags"] == (int(request["frame"]) & 0xFFFFFFFF) ^ int(
            request["status_flags"]
        )
        digest = 2166136261
        for byte in request.tobytes():
            digest = ((digest ^ byte) * 16777619) & 0xFFFFFFFF
        assert reply["mode_flags"] == digest
    frame_10 = recording.requests[1]
    assert len({float(frame_10[n]) for n in ("phi", "theta", "psi", "p", "q")}) == 5  # distinct


# --- R1, R2, R2c, R2d, R12b -----------------------------------------------------------------------


@pytest.mark.parametrize("name", ["A_light_autopilot", "F_moderate_autopilot"])
def test_r1_lockstep_health(runs: Runs, name: str) -> None:
    run = runs[name]
    assert_lockstep_healthy(run, "autopilot")
    assert run.result["peer_summary"]["held"] == 0  # Joe's Q3: every exchange flown, none held


@pytest.mark.parametrize("name", ["A_light_autopilot", "F_moderate_autopilot"])
def test_r2_altitude_and_heading_bands(runs: Runs, name: str) -> None:
    """Gating at both levels (Q7), every recorded state including the final one."""
    metrics = runs[name].metrics
    assert metrics["states"] == 100001
    assert metrics["max_altitude_error_m"] <= ALTITUDE_BAND_M, metrics
    assert metrics["max_heading_error_deg"] <= HEADING_BAND_DEG, metrics


@pytest.mark.parametrize("name", ["E_light_hold", "G_moderate_hold"])
def test_r2c_hold_is_reported(runs: Runs, name: str) -> None:
    """Informational (R2c; Joe's Q1 (b)): lockstep health only; the bands are reported."""
    run = runs[name]
    assert_lockstep_healthy(run, "echo_peer")
    metrics = run.metrics
    metrics["altitude_band_discriminates"] = metrics["max_altitude_error_m"] > ALTITUDE_BAND_M
    metrics["heading_band_discriminates"] = metrics["max_heading_error_deg"] > HEADING_BAND_DEG
    write_report(runs.report_dir / name, run)


def test_r2d_autopilot_beats_hold_light(runs: Runs) -> None:
    """Joe's Q1 (c), light only, seed 42: at most half the hold peer's worst errors."""
    autopilot = runs["A_light_autopilot"].metrics
    hold = runs["E_light_hold"].metrics
    altitude_ratio = autopilot["max_altitude_error_m"] / hold["max_altitude_error_m"]
    heading_ratio = autopilot["max_heading_error_deg"] / hold["max_heading_error_deg"]
    (runs.report_dir / "r2d_ratios.json").write_text(
        json.dumps({"altitude": altitude_ratio, "heading": heading_ratio}) + "\n"
    )
    assert altitude_ratio <= BEATS_HOLD_RATIO
    assert heading_ratio <= BEATS_HOLD_RATIO


R12B_FIELDS = ("phi", "theta", "psi", "altitude", "lat", "lon", "u", "v", "w", "p", "q", "r")
R12B_LIVE_FIELDS = ("phi", "theta", "psi", "u", "v", "w", "p", "q", "r")


def _state_quantities(y: np.ndarray) -> dict[str, np.ndarray]:
    psi, theta, phi = euler_321(y[:, 6:10])
    return {
        "phi": phi,
        "theta": theta,
        "psi": psi,
        "altitude": y[:, 2],
        "lat": y[:, 0],
        "lon": y[:, 1],
        "u": y[:, 3],
        "v": y[:, 4],
        "w": y[:, 5],
        "p": y[:, 10],
        "q": y[:, 11],
        "r": y[:, 12],
    }


@pytest.mark.parametrize("name", ["A_light_autopilot", "F_moderate_autopilot"])
def test_r12b_icd_matches_state(runs: Runs, name: str) -> None:
    """A separate check from R2: the packed ICD fields against the recorded state of the same
    frame."""
    recording = runs[name].recording
    frames = recording.requests["frame"].astype(np.int64)
    y = recording.states["y"]
    same = _state_quantities(y[frames])
    lagged = _state_quantities(y[frames + 1])
    diffs = {f: float(np.max(np.abs(recording.requests[f] - same[f]))) for f in R12B_FIELDS}
    lags = {f: float(np.max(np.abs(recording.requests[f] - lagged[f]))) for f in R12B_LIVE_FIELDS}
    (runs.report_dir / name / "r12b.json").write_text(
        json.dumps({"max_diff": diffs, "max_one_frame_lag": lags}, indent=2) + "\n"
    )
    bad = {f: d for f, d in diffs.items() if not d <= R12B_TOLERANCE}
    assert not bad, f"R12b: ICD fields disagree with the recorded state beyond 1e-12: {bad}"
    dead = {f: d for f, d in lags.items() if not d > R12B_LAG_FLOOR}
    assert not dead, f"R12b liveness: a one-frame lag would pass for {dead}"


# --- R3 and the Q15 sweep -------------------------------------------------------------------------


def test_r3_bit_identical_with_jitter(runs: Runs) -> None:
    a, b, c = (
        runs["A_light_autopilot"],
        runs["B_light_autopilot_repeat"],
        runs["C_light_autopilot_jitter"],
    )
    for run in (a, b, c):
        assert_lockstep_healthy(run, "autopilot")
    jitter = c.result["jitter"]
    (runs.report_dir / "r3.json").write_text(
        json.dumps(
            {"sha256": {r.name: r.result["sha256"] for r in (a, b, c)}, "jitter": jitter}, indent=2
        )
        + "\n"
    )
    assert jitter["stops_inside_run"] >= JITTER_MIN_STOPS, jitter
    assert a.data == b.data
    assert a.data == c.data
    assert a.result["sha256"] == b.result["sha256"] == c.result["sha256"]


def test_r3_seed_changes_bytes(runs: Runs) -> None:
    """The comparison is live: another Dryden seed changes the recording, the state stream
    included."""
    a, d = runs["A_light_autopilot"], runs["sweep_seed_43"]
    assert_lockstep_healthy(d, "autopilot")
    assert a.data != d.data
    assert not np.array_equal(a.recording.states["y"], d.recording.states["y"])


def test_seed_sweep_light_is_reported(runs: Runs) -> None:
    """Q15: non-gating. Lockstep health is asserted; the margins are reported only."""
    table = {}
    for seed in SWEEP_SEEDS:
        run = runs[f"sweep_seed_{seed}"]
        assert_lockstep_healthy(run, "autopilot")
        table[seed] = {k: run.metrics[k] for k in ("max_altitude_error_m", "max_heading_error_deg")}
    (runs.report_dir / "sweep.json").write_text(json.dumps(table, indent=2) + "\n")


# --- R4 and R4b -----------------------------------------------------------------------------------


def _assert_post_halt(result: dict[str, Any], halt_frame: int, replies: int) -> None:
    halt = result["halt"]
    assert halt is not None
    assert halt["check"] == "TRANSPORT"
    assert halt["frame"] == halt_frame
    assert result["frame"] == halt_frame  # clock == halt frame
    assert result["state_finite"]
    assert result["replies"] == replies
    assert result["requests"] == replies + 1
    assert result["last_state_frame"] == halt_frame
    assert result["halt2"] == halt
    assert result["frame2"] == halt_frame  # sticky, nothing applied
    assert result["state_unchanged"]


def test_r4_sigkill_kill_to_halt(reaper: Reaper, binaries: dict[str, str]) -> None:
    host = Host(
        reaper, _child_args(binaries, "kill", LIGHT, "--mode", "sigkill"), SHORT_RUN_TIMEOUT_S
    )
    result = host.finish()
    assert result["killer_returncode"] == 0, result["killer_out"]
    t_kill = int(result["killer_out"])
    t_return = int(result["t_return"])
    assert t_kill < t_return  # Joe, E1: the kill is proven to precede the halt
    kill_to_halt_ms = (t_return - t_kill) / 1e6
    assert kill_to_halt_ms <= TIMEOUT_MS + ALLOWANCE_MS, kill_to_halt_ms
    _assert_post_halt(result, 5000, 500)
    assert result["peer_returncode"] == -signal.SIGKILL


def test_r4_abort_reply_to_halt(reaper: Reaper, binaries: dict[str, str]) -> None:
    host = Host(
        reaper, _child_args(binaries, "kill", LIGHT, "--mode", "abort"), SHORT_RUN_TIMEOUT_S
    )
    result = host.finish()
    assert result["first_halt"] is None
    assert result["notify"] == "01"  # the autopilot sent its 500th reply, then aborted
    bound_ms = (int(result["t_return"]) - int(result["t0"])) / 1e6
    assert bound_ms <= TIMEOUT_MS + ALLOWANCE_MS, bound_ms
    _assert_post_halt(result, 5000, 500)
    assert result["peer_returncode"] == -signal.SIGABRT


def test_r4_wait_duration(reaper: Reaper, binaries: dict[str, str]) -> None:
    host = Host(reaper, _child_args(binaries, "kill", LIGHT, "--mode", "wait"), SHORT_RUN_TIMEOUT_S)
    result = host.finish()
    assert result["first_halt"] is None
    assert result["notify"] == "01"
    assert result["killed_returncode"] == -signal.SIGKILL
    duration = result["duration_ms"]
    assert TIMEOUT_MS <= duration <= TIMEOUT_MS + ALLOWANCE_MS, duration  # not early, not restarted
    _assert_post_halt(result, 5000, 500)


def test_r4b_sim_pause(reaper: Reaper, binaries: dict[str, str]) -> None:
    host = Host(reaper, _child_args(binaries, "pause", LIGHT), SHORT_RUN_TIMEOUT_S)
    result = host.finish()
    assert result["first_halt"] is None
    assert result["peer_returncode"] == 2  # the HAL's timeout: an idle sim is not a clean end
    assert result["halt"] is not None
    assert result["halt"]["check"] == "TRANSPORT"
    assert result["halt"]["frame"] == 1000
    assert result["frame"] == 1000
    assert result["halt_ms"] <= TIMEOUT_MS + ALLOWANCE_MS, result["halt_ms"]


def _pidfd_exit_ns(pid: int, timeout_s: float) -> int:
    """monotonic_ns at which pid exits (exact: a pidfd wakes on exit), failing after timeout_s."""
    fd = os.pidfd_open(pid)
    try:
        ready, _, _ = select.select([fd], [], [], timeout_s)
        exited = time.monotonic_ns()
    finally:
        os.close(fd)
    if not ready:
        pytest.fail(f"pid {pid} still running after {timeout_s} s")
    return exited


def test_r4b_sim_death(reaper: Reaper, binaries: dict[str, str], trim: ls.AutopilotTrim) -> None:
    segment = ls.new_segment_name()
    config = ls.load_config(
        LIGHT, segment=segment, timeout_ms=int(TIMEOUT_MS), peer_timeout_ms=int(TIMEOUT_MS)
    )
    autopilot = ls.PeerProcess(ls.autopilot_argv(binaries["--autopilot"], config, SETPOINTS, trim))
    try:
        host = Host(
            reaper,
            _child_args(binaries, "sim-only", LIGHT, "--segment", segment),
            SHORT_RUN_TIMEOUT_S,
        )
        host.expect("READY")
        assert autopilot.returncode is None
        assert host.proc.poll() is None  # both alive at the kill
        t_kill = host.kill_child_only()
        status = autopilot.wait(TIMEOUT_MS / 1000 + 5.0)
        assert status is not None
        assert autopilot.exited_ns is not None
        kill_to_exit_ms = (autopilot.exited_ns - t_kill) / 1e6
        assert status == 2, autopilot.stderr  # timeout, not 0: no CLOSED was ever written
        assert kill_to_exit_ms <= TIMEOUT_MS + ALLOWANCE_MS, kill_to_exit_ms
        assert not (ls.SHM_DIRECTORY / segment).exists()  # unlinked at the handshake
    finally:
        autopilot.stop()
        with contextlib.suppress(FileNotFoundError):
            os.unlink(ls.SHM_DIRECTORY / segment)


# --- driver lifecycle -----------------------------------------------------------------------------


def _lifecycle(reaper: Reaper, binaries: dict[str, str], case: str, out: Path) -> dict[str, Any]:
    host = Host(
        reaper,
        _child_args(binaries, "lifecycle", LIGHT, "--case", case, "--out", str(out)),
        SHORT_RUN_TIMEOUT_S,
    )
    facts = host.finish()
    assert not facts["segment_exists"], facts
    if facts["peer_launched"]:
        assert facts["peer_returncode"] is not None, facts
        assert not facts["peer_alive"], facts  # reaped
    return facts


def test_session_normal_completion(
    reaper: Reaper, binaries: dict[str, str], tmp_path: Path
) -> None:
    facts = _lifecycle(reaper, binaries, "normal", tmp_path)
    assert "exception" not in facts  # the held recorder bytes did not keep the Simulation alive
    assert facts["recorder_bytes"] == 48 + 100 * (200 + 64) + 1001 * 112
    assert facts["halt"] is None
    assert facts["peer_returncode"] == 0
    assert facts["peer_summary"]["handled"] == 100


def test_session_missing_executable(
    reaper: Reaper, binaries: dict[str, str], tmp_path: Path
) -> None:
    facts = _lifecycle(reaper, binaries, "missing-exe", tmp_path)
    assert facts["exception"] == "FileNotFoundError"
    assert not facts["peer_launched"]


def test_session_popen_failure(reaper: Reaper, binaries: dict[str, str], tmp_path: Path) -> None:
    facts = _lifecycle(reaper, binaries, "popen-failure", tmp_path)
    assert facts["exception"] == "OSError", facts
    assert not facts["peer_launched"], facts


def test_session_peer_exits_at_once(
    reaper: Reaper, binaries: dict[str, str], tmp_path: Path
) -> None:
    facts = _lifecycle(reaper, binaries, "bad-args", tmp_path)
    assert facts["exception"] == "ConfigError"
    assert "handshake" in facts["message"]
    assert facts["peer_returncode"] == 1  # the autopilot's own bad-arguments status


def test_session_escalates_to_sigkill(
    reaper: Reaper, binaries: dict[str, str], tmp_path: Path
) -> None:
    facts = _lifecycle(reaper, binaries, "term-ignoring-peer", tmp_path)
    assert facts["exception"] == "ConfigError"
    assert facts["peer_returncode"] == -signal.SIGKILL


def test_session_body_exception(reaper: Reaper, binaries: dict[str, str], tmp_path: Path) -> None:
    facts = _lifecycle(reaper, binaries, "body-exception", tmp_path)
    assert facts["exception"] == "RuntimeError"
    assert facts["notes"] == []
    assert facts["peer_returncode"] == -signal.SIGTERM


def test_session_stopped_peer(reaper: Reaper, binaries: dict[str, str], tmp_path: Path) -> None:
    facts = _lifecycle(reaper, binaries, "stopped-peer", tmp_path)
    assert facts["stopped_state"] in ("T", "t")
    assert facts["exception"] == "RuntimeError"
    assert facts["peer_returncode"] == -signal.SIGTERM


def test_session_keyboard_interrupt(
    reaper: Reaper, binaries: dict[str, str], tmp_path: Path
) -> None:
    facts = _lifecycle(reaper, binaries, "keyboard-interrupt", tmp_path)
    assert facts["exception"] == "KeyboardInterrupt"
    assert facts["peer_returncode"] == -signal.SIGTERM


def test_session_retained_registry(
    reaper: Reaper, binaries: dict[str, str], tmp_path: Path
) -> None:
    facts = _lifecycle(reaper, binaries, "retained-registry", tmp_path)
    assert facts["exception"] == "LockstepError"
    assert "still referenced" in facts["message"]
    assert facts["peer_returncode"] == -signal.SIGTERM  # stopped: the sim could not close


def test_driver_death_after_handshake(reaper: Reaper, binaries: dict[str, str]) -> None:
    host = Host(reaper, _child_args(binaries, "driver-death", LIGHT), SHORT_RUN_TIMEOUT_S)
    segment = host.expect("SEGMENT").split()[1]
    host.expect("READY")
    autopilot = host.pids["peer"]
    t_kill = host.kill_child_only()  # the child only: its autopilot must end by its own timeout
    exited = _pidfd_exit_ns(autopilot, TIMEOUT_MS / 1000 + 5.0)  # adopted by this subreaper
    status = reaper.collect(autopilot, 5.0)
    assert status is not None, "the orphaned autopilot was not reparented to the subreaper"
    assert os.waitstatus_to_exitcode(status) == 2
    assert (exited - t_kill) / 1e6 <= TIMEOUT_MS + ALLOWANCE_MS
    assert not (ls.SHM_DIRECTORY / segment).exists()


def test_driver_death_before_handshake(reaper: Reaper, binaries: dict[str, str]) -> None:
    host = Host(reaper, _child_args(binaries, "pre-handshake", LIGHT), SHORT_RUN_TIMEOUT_S)
    segment = host.expect("SEGMENT").split()[1]
    path = ls.SHM_DIRECTORY / segment
    deadline = time.monotonic() + 10.0
    while not path.exists():
        assert time.monotonic() < deadline, "the sim never created its segment"
        time.sleep(0.005)
    host.kill_child_only()
    assert path.exists()  # a real leftover: the only way one arises
    assert segment in ls.remove_stale_segments()
    assert not path.exists()
