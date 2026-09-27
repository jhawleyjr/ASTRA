"""astra.lockstep: drive a lockstep run against a flight-software peer process (M4 PR 6,
docs/plans/m4-pr6-lockstep-acceptance.md; ADR-0022, ADR-0024).

The sim runs in this process; the peer (``embedded/autopilot``'s ``astra_autopilot``, or the echo
peer
the tests build) is a child process that attaches to the shm segment the sim creates. This module
owns
what the sim itself cannot: the segment name (``astra_<driver pid>_<nonce>``, M4 Q5), the peer's
launch, a bounded terminate/kill/reap of the peer on every exit path, the unlink of the segment name
in
a ``finally``, and the sweep of names left by a dead driver.

It knows no build layout: every executable path is the caller's (plan D4). Linux only (POSIX shm
under
``/dev/shm``, ``pidfd_open``).

Lifetime rule. The ``Simulation`` closes its transport only in its destructor (``CLOSED`` on normal
completion, nothing after a failure, ADR-0024). ``Session`` therefore never hands the ``Simulation``
out; every method copies what it returns, except ``Session.registry``, which is a live view that
keeps
the ``Simulation`` alive while it is held. ``lockstep_session`` detects any reference still held at
exit (a ``sys.getrefcount`` above the baseline taken right after construction), still stops and
reaps
the peer, and raises ``LockstepError``.
"""

from __future__ import annotations

import contextlib
import dataclasses
import hashlib
import os
import re
import secrets
import select
import signal
import subprocess
import sys
import tempfile
import time
from collections.abc import Callable, Generator, Sequence
from pathlib import Path
from typing import IO, Any, cast

import numpy as np
import yaml

from astra import _core
from astra.trim import trim_straight_and_level

SHM_PLUGIN = "plugins/sim/transport/shm"
SHM_DIRECTORY = Path("/dev/shm")

# The shm plugin's own rule (plugins/sim/transport/shm/shm.cpp, valid_segment): the pid group is the
# driver's pid.
_SEGMENT = re.compile(r"astra_([0-9]{1,10})_[A-Za-z0-9]{1,32}")
_SUMMARY = re.compile(r"(autopilot|echo_peer): handled ([0-9]+)(?: held ([0-9]+))? status ([0-9]+)")

ICD_MAGIC = 0x41535452  # "ASTR", icd/astra_icd.h ASTRA_ICD_MAGIC
ICD_VERSION = 1  # ASTRA_ICD_VERSION
RECORDER_MAGIC = 0x41535852  # "ASXR", core/include/astra/kernel/exchange_recorder.hpp
RECORDER_VERSION = 1

# The serialized recorder (exchange_recorder.hpp): a 48-byte header, then the three streams as
# fixed-size native-endian records. The ICD dtypes are transcribed from icd/astra_icd.h; the tests
# check every offset against a hand-counted table and against an echo-mode session.
RECORDER_HEADER_DTYPE = np.dtype(
    [
        ("magic", "=u4"),
        ("version", "=u4"),
        ("request_size", "=u4"),
        ("reply_size", "=u4"),
        ("state_size", "=u4"),
        ("reserved", "=u4"),
        ("request_count", "=u8"),
        ("reply_count", "=u8"),
        ("state_count", "=u8"),
    ]
)
_REQUEST_DOUBLES = [
    "t",
    "phi",
    "theta",
    "psi",
    "p",
    "q",
    "r",
    "u",
    "v",
    "w",
    "alpha",
    "beta",
    "tas",
    "cas",
    "mach",
    "altitude",
    "vertical_speed",
    "lat",
    "lon",
    "ax",
    "ay",
    "az",
]
REQUEST_DTYPE = np.dtype(
    [("magic", "=u4"), ("version", "=u4"), ("frame", "=u8")]
    + [(name, "=f8") for name in _REQUEST_DOUBLES]
    + [("status_flags", "=u4"), ("reserved", "=u4")]
)
REPLY_DTYPE = np.dtype(
    [
        ("magic", "=u4"),
        ("version", "=u4"),
        ("frame", "=u8"),
        ("elevator", "=f8"),
        ("aileron", "=f8"),
        ("rudder", "=f8"),
        ("throttle", "=f8", (2,)),
        ("mode_flags", "=u4"),
        ("fault_flags", "=u4"),
    ]
)
STATE_DTYPE = np.dtype([("frame", "=u8"), ("y", "=f8", (13,))])


class LockstepError(RuntimeError):
    """A lockstep session could not be set up or cleaned up as required."""


@dataclasses.dataclass(frozen=True)
class Setpoints:
    """What the autopilot holds: altitude (m), true airspeed (m/s), heading (rad)."""

    altitude_m: float
    airspeed_mps: float
    heading_rad: float


@dataclasses.dataclass(frozen=True)
class AutopilotTrim:
    """The autopilot's bias trims (M4 Q12): elevator (rad), throttle (0..1), theta (rad)."""

    elevator_rad: float
    throttle: float
    theta_rad: float


@dataclasses.dataclass(frozen=True)
class Halt:
    """An owned copy of a ``HaltReason`` that fired."""

    check: str
    source: str
    frame: int
    vehicle_index: int


@dataclasses.dataclass(frozen=True)
class PeerSummary:
    """The peer's own exit line: ``<name>: handled <n> [held <m>] status <s>``."""

    name: str
    handled: int
    held: int | None
    status: int


def trim_for(config_path: str | os.PathLike[str], setpoints: Setpoints) -> AutopilotTrim:
    """The still-air straight-and-level trim at the setpoints' altitude and airspeed.

    ``astra.trim`` drops ``physical.wind`` itself (lesson 2026-09-22) and pins psi = 0, so the
    heading
    setpoint must be 0. gamma = 0 makes theta = alpha, the autopilot's ``--theta-trim``. The trim
    builds
    a ``Simulation`` of its own, so it is given a temporary copy of the config without the
    ``transport`` block (a trim must not create a segment or wait for a peer); every other key is
    unchanged (``yaml.safe_dump`` writes floats with ``repr``, so they round-trip exactly).
    """
    if setpoints.heading_rad != 0.0:
        raise ValueError("trim_for: astra.trim pins psi = 0, so the heading setpoint must be 0")
    with open(config_path, encoding="utf-8") as f:
        config = _mapping(yaml.safe_load(f), str(config_path))
    config.pop("transport", None)
    with tempfile.TemporaryDirectory(prefix="astra_trim_") as directory:
        still_air = Path(directory) / Path(config_path).name
        still_air.write_text(yaml.safe_dump(config, sort_keys=False), encoding="utf-8")
        result = trim_straight_and_level(still_air, setpoints.airspeed_mps, setpoints.altitude_m)
    return AutopilotTrim(result.elevator_rad, result.throttle, result.alpha_rad)


def new_segment_name() -> str:
    """``astra_<this process's pid>_<16 hex>``: unique per call (M4 Q5, ADR-0024)."""
    return f"astra_{os.getpid()}_{secrets.token_hex(8)}"


def _pid_alive(pid: int) -> bool:
    if pid <= 0:
        return True  # not a process we can probe: leave the name alone
    try:
        os.kill(pid, 0)
    except ProcessLookupError:
        return False
    except PermissionError:
        return True
    return True


def remove_stale_segments(directory: Path = SHM_DIRECTORY) -> list[str]:
    """Unlinks ``astra_<pid>_*`` names in ``directory`` whose driver pid is not a live process.

    Best-effort and conservative (plan, critique C-4): a reused pid keeps a stale name, a zombie
    counts
    as alive, and the probe and the unlink are not atomic. It claims neither ownership nor complete
    reclamation. A leftover name only arises when a sim process died between ``shm_open`` and the
    end
    of its handshake: the shm plugin unlinks the name once both sides are mapped, and on a failed
    handshake.
    """
    removed: list[str] = []
    try:
        names = sorted(os.listdir(directory))
    except FileNotFoundError:
        return removed
    for name in names:
        match = _SEGMENT.fullmatch(name)
        if match is None or _pid_alive(int(match.group(1))):
            continue
        with contextlib.suppress(FileNotFoundError):
            os.unlink(directory / name)
            removed.append(name)
    return removed


def checked_segment(segment: object) -> str:
    """``segment`` if it matches the shm plugin's ``astra_<pid>_<nonce>`` rule, else ``ValueError``.

    The rule admits no ``/`` and no ``..``, so ``SHM_DIRECTORY / segment`` never leaves ``/dev/shm``
    (review R-001: an absolute name would replace the directory, and the cleanup would unlink it).
    """
    if not isinstance(segment, str) or _SEGMENT.fullmatch(segment) is None:
        raise ValueError(f"segment {segment!r} does not match astra_<pid>_<nonce>")
    return segment


def _unlink_segment(segment: str) -> None:
    with contextlib.suppress(FileNotFoundError):
        os.unlink(SHM_DIRECTORY / checked_segment(segment))


def _mapping(value: object, where: str) -> dict[str, Any]:
    if not isinstance(value, dict):
        raise LockstepError(f"{where} must be a map")
    return cast(dict[str, Any], value)


def transport_block(config: dict[str, Any]) -> dict[str, Any]:
    """The config's ``transport`` map, which must name the shm plugin."""
    transport = _mapping(config.get("transport"), "transport")
    if transport.get("plugin") != SHM_PLUGIN:
        raise LockstepError(f"transport.plugin must be {SHM_PLUGIN}")
    return transport


def load_config(
    config_path: str | os.PathLike[str],
    *,
    segment: str,
    frames: int | None = None,
    seed: int | None = None,
    timeout_ms: int | None = None,
    handshake_timeout_ms: int | None = None,
    peer_timeout_ms: int | None = None,
) -> dict[str, Any]:
    """Reads a lockstep config as a dict and sets ``transport.segment`` to ``segment``.

    The file must carry an shm ``transport`` block with no ``segment`` of its own (the name is the
    driver's) and all three timeouts (the peer is given the same values, so the driver never assumes
    a
    core default). The overrides are for tests; none of them changes a trajectory except ``seed``.
    """
    checked_segment(segment)
    with open(config_path, encoding="utf-8") as f:
        config = _mapping(yaml.safe_load(f), str(config_path))
    transport = transport_block(config)
    if "segment" in transport:
        raise LockstepError(f"{config_path}: transport.segment is the driver's; remove it")
    transport["segment"] = segment
    for key, value in (
        ("timeout_ms", timeout_ms),
        ("handshake_timeout_ms", handshake_timeout_ms),
        ("peer_timeout_ms", peer_timeout_ms),
    ):
        if value is not None:
            transport[key] = value
        if not isinstance(transport.get(key), int):
            raise LockstepError(f"{config_path}: transport.{key} must be given as an integer")
    if frames is not None:
        _mapping(config.get("sim"), "sim")["frames"] = frames
    if seed is not None:
        physical = _mapping(config.get("physical"), "physical")
        wind = _mapping(physical.get("wind"), "physical.wind")
        if "seed" not in wind:
            raise LockstepError(f"{config_path}: physical.wind has no seed to override")
        wind["seed"] = seed
    return config


def _peer_prefix(executable: str | os.PathLike[str], config: dict[str, Any]) -> list[str]:
    transport = transport_block(config)
    return [
        os.fspath(executable),
        "--segment",
        str(transport["segment"]),
        "--handshake-timeout-ms",
        str(transport["handshake_timeout_ms"]),
        "--peer-timeout-ms",
        str(transport["peer_timeout_ms"]),
    ]


def _notify_args(
    notify_fd: int | None, stop_after: int | None, abort_after: int | None
) -> list[str]:
    if notify_fd is None:
        if stop_after is not None or abort_after is not None:
            raise ValueError("stop_after/abort_after need notify_fd")
        return []
    if (stop_after is None) == (abort_after is None):
        raise ValueError("notify_fd goes with exactly one of stop_after / abort_after")
    if stop_after is not None:
        return ["--notify-fd", str(notify_fd), "--stop-after", str(stop_after)]
    return ["--notify-fd", str(notify_fd), "--abort-after", str(abort_after)]


def autopilot_argv(
    executable: str | os.PathLike[str],
    config: dict[str, Any],
    setpoints: Setpoints,
    trim: AutopilotTrim,
    *,
    notify_fd: int | None = None,
    stop_after: int | None = None,
    abort_after: int | None = None,
) -> list[str]:
    """``astra_autopilot``'s command line. Segment and timeouts come from ``config``; floats are
    passed
    with ``repr`` (shortest round-trip, parsed exactly by ``strtod``)."""
    return [
        *_peer_prefix(executable, config),
        "--altitude-m",
        repr(setpoints.altitude_m),
        "--airspeed-mps",
        repr(setpoints.airspeed_mps),
        "--heading-rad",
        repr(setpoints.heading_rad),
        "--elevator-trim",
        repr(trim.elevator_rad),
        "--throttle-trim",
        repr(trim.throttle),
        "--theta-trim",
        repr(trim.theta_rad),
        *_notify_args(notify_fd, stop_after, abort_after),
    ]


def hold_peer_argv(
    executable: str | os.PathLike[str],
    config: dict[str, Any],
    commands: tuple[float, float, float, float],
) -> list[str]:
    """The echo peer holding constant (elevator, aileron, rudder, throttle): R2c's constant trim."""
    return [
        *_peer_prefix(executable, config),
        "--mode",
        "hold",
        "--commands",
        ",".join(repr(value) for value in commands),
    ]


def echo_peer_argv(executable: str | os.PathLike[str], config: dict[str, Any]) -> list[str]:
    """The echo peer in ``echo`` mode (its reply is a known function of the request)."""
    return [*_peer_prefix(executable, config), "--mode", "echo"]


def _check_executable(path: str) -> None:
    if not os.path.isfile(path) or not os.access(path, os.X_OK):
        raise FileNotFoundError(f"peer executable not found or not executable: {path}")


class PeerProcess:
    """One peer process: output to temporary files (no pipe-buffer deadlock), a pidfd opened at
    launch
    (an exact exit wakeup, and the pid cannot be reused while it is held), and a bounded stop."""

    def __init__(self, argv: Sequence[str], *, pass_fds: Sequence[int] = ()) -> None:
        _check_executable(argv[0])
        # Held open for the peer's lifetime and closed by _finish(), so not a `with` block.
        self._stdout: IO[bytes] | None = tempfile.TemporaryFile()  # noqa: SIM115
        self._stderr: IO[bytes] | None = tempfile.TemporaryFile()  # noqa: SIM115
        self._pidfd = -1
        self.stdout = ""
        self.stderr = ""
        self.exited_ns: int | None = None
        try:
            self._process = subprocess.Popen(
                list(argv),
                stdin=subprocess.DEVNULL,
                stdout=self._stdout,
                stderr=self._stderr,
                close_fds=True,
                pass_fds=tuple(pass_fds),
            )
        except BaseException:
            self._close_files()
            raise
        try:
            self._pidfd = os.pidfd_open(self._process.pid)
        except BaseException:
            self._process.kill()
            self._process.wait()
            self._finish()
            raise

    @property
    def pid(self) -> int:
        return self._process.pid

    @property
    def returncode(self) -> int | None:
        return self._process.returncode

    def wait(self, timeout_s: float) -> int | None:
        """The exit status once the peer has exited (and is reaped), or None after ``timeout_s``."""
        if self._process.returncode is not None:
            return self._process.returncode
        ready, _, _ = select.select([self._pidfd], [], [], max(timeout_s, 0.0))
        if not ready:
            return None
        self.exited_ns = time.monotonic_ns()
        status = self._process.wait()  # exited: this reaps at once
        self._finish()
        return status

    def running(self) -> bool:
        """True while the peer has not exited: a zero-timeout pidfd poll, never Popen's cached
        status (review R-003), and nothing is reaped."""
        if self._process.returncode is not None:
            return False
        ready, _, _ = select.select([self._pidfd], [], [], 0.0)
        return not ready

    def signal(self, signum: int) -> None:
        """Sends ``signum`` unless the peer is already reaped."""
        if self._process.returncode is None:
            with contextlib.suppress(ProcessLookupError):
                os.kill(self._process.pid, signum)

    def stop(self, grace_s: float = 1.0, kill_wait_s: float = 5.0) -> int:
        """SIGCONT (a jittered peer may be stopped), SIGTERM, ``grace_s``, SIGKILL, ``kill_wait_s``:
        always reaps or raises ``LockstepError``. A no-op once the peer has exited."""
        try:
            status = self.wait(0.0)
            if status is None:
                self.signal(signal.SIGCONT)
                self.signal(signal.SIGTERM)
                status = self.wait(grace_s)
        finally:
            # However the graceful phase ended (a timeout, or an exception such as KeyboardInterrupt
            # inside its wait; review R-002), a peer still running is killed and collected.
            if self._process.returncode is None:
                status = self._kill_and_reap(kill_wait_s)
        assert status is not None
        return status

    def _kill_and_reap(self, kill_wait_s: float) -> int:
        """SIGKILL, then collect; an interrupt during the collection is deferred until the peer is
        reaped (a SIGKILLed process exits at once), then re-raised."""
        self.signal(signal.SIGKILL)
        interrupted: BaseException | None = None
        deadline = time.monotonic() + kill_wait_s
        status: int | None = None
        while status is None:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise LockstepError(f"peer pid {self.pid} survived SIGKILL for {kill_wait_s} s")
            try:
                status = self.wait(remaining)
            except KeyboardInterrupt as error:
                interrupted = error
        if interrupted is not None:
            raise interrupted
        return status

    def summary(self) -> PeerSummary | None:
        """The parsed exit line, once the peer has exited."""
        for line in reversed(self.stdout.splitlines()):
            match = _SUMMARY.fullmatch(line.strip())
            if match is not None:
                held = match.group(3)
                return PeerSummary(
                    match.group(1),
                    int(match.group(2)),
                    None if held is None else int(held),
                    int(match.group(4)),
                )
        return None

    def _finish(self) -> None:
        for handle, attr in ((self._stdout, "stdout"), (self._stderr, "stderr")):
            if handle is not None:
                handle.seek(0)
                setattr(self, attr, handle.read().decode(errors="replace"))
        self._close_files()
        if self._pidfd >= 0:
            os.close(self._pidfd)
            self._pidfd = -1

    def _close_files(self) -> None:
        for handle in (self._stdout, self._stderr):
            if handle is not None:
                handle.close()
        self._stdout = None
        self._stderr = None


def _halt(reason: _core.HaltReason) -> Halt | None:
    if not reason:
        return None
    return Halt(reason.check.name, reason.source, reason.frame, reason.vehicle_index)


class Session:
    """A running lockstep session. Wraps the ``Simulation`` without exposing it (module
    docstring)."""

    def __init__(self, sim: _core.Simulation, peer: PeerProcess, segment: str) -> None:
        self._sim: _core.Simulation | None = sim
        self.peer = peer
        self.segment = segment

    def _live(self) -> _core.Simulation:
        if self._sim is None:
            raise LockstepError("the session has ended")
        return self._sim

    def run(self, frames: int | None = None) -> Halt | None:
        """``Simulation.run``, then a drain of the run log (ADR-0025: the ``Simulation`` is hidden,
        so the session drains at its own calls). Returns the run's halt, else the drain's."""
        sim = self._live()
        halt = sim.run() if frames is None else sim.run(frames)
        drained = sim.flush_log()
        return _halt(halt if halt else drained)

    def step(self) -> Halt | None:
        """``Simulation.step``, then a drain of the run log, as ``run`` does."""
        sim = self._live()
        halt = sim.step()
        drained = sim.flush_log()
        return _halt(halt if halt else drained)

    def flush_log(self) -> Halt | None:
        """Drains the run log; the sticky ``LOG`` halt once there is one. Nothing without a
        ``log`` block."""
        return _halt(self._live().flush_log())

    def close_log(self) -> None:
        """Finalizes the run log (``Simulation.close_log``): raises ``astra._core.LogError`` naming
        the write that failed, never setting ``complete``; a second call does nothing after a
        success and raises the same error after a failure. Without it, the ``Simulation``'s
        destructor completes the log best effort when the session ends."""
        self._live().close_log()

    @property
    def frame(self) -> int:
        return self._live().clock.frame

    def signal(self, name: str) -> float | None:
        return self._live().registry.find(name)

    def rigid_body_state(self) -> np.ndarray:
        """A copy of vehicle 0's 13 rigid-body doubles."""
        return np.array(self._live().vehicles[0].state[: _core.STATE_SIZE], dtype=np.float64)

    def recorder_bytes(self) -> bytes:
        data = self._live().exchange_recorder_bytes()
        if data is None:
            raise LockstepError("the Simulation has no transport")
        return data

    @property
    def registry(self) -> _core.SignalRegistry:
        """A LIVE view: it keeps the Simulation (and its transport) alive while it is held."""
        return self._live().registry


def _record_cleanup_error(errors: list[BaseException], step: Callable[[], object]) -> None:
    try:
        step()
    except BaseException as error:  # every cleanup step must run; the errors are re-raised
        errors.append(error)


@contextlib.contextmanager
def lockstep_session(
    config: dict[str, Any],
    peer_argv: Sequence[str],
    *,
    plugin_dir: str | os.PathLike[str] | None = None,
    pass_fds: Sequence[int] = (),
    on_peer_launched: Callable[[PeerProcess], None] | None = None,
    stop_grace_s: float = 1.0,
    origin: str = "<astra.lockstep>",
) -> Generator[Session]:
    """Launches the peer, builds the ``Simulation`` (segment creation and handshake), yields a
    ``Session``, and on every exit path: closes the ``Simulation``, reaps the peer (a clean exit is
    awaited up to ``peer_timeout_ms`` + 1 s after a body that returned; anything else, or a late
    peer, is stopped), and unlinks the segment name. Each cleanup step runs whatever the previous
    one
    did; the body's exception is re-raised with any cleanup error attached as a note.

    ``pass_fds`` are inherited by the peer; the caller closes its own copies right after launch
    (``on_peer_launched`` runs then, before the handshake).
    """
    transport = transport_block(config)
    segment = checked_segment(transport.get("segment"))  # before anything is launched (R-001)
    clean_exit_s = int(transport["peer_timeout_ms"]) / 1000.0 + 1.0
    peer = PeerProcess(peer_argv, pass_fds=pass_fds)
    session: Session | None = None
    baseline = 0
    body_error: BaseException | None = None
    try:
        if on_peer_launched is not None:
            on_peer_launched(peer)
        sim = _core.Simulation.from_config(
            _core.Config.from_dict(config, origin=origin),
            os.fspath(plugin_dir) if plugin_dir is not None else _core.default_plugin_dir(),
        )
        session = Session(sim, peer, segment)
        # Joe, E2: a baseline, never a hard-coded count (3.14 borrows stack references).
        baseline = sys.getrefcount(sim)
        del sim
        baseline -= 1
        yield session
    except BaseException as error:
        body_error = error
        raise
    finally:
        errors: list[BaseException] = []
        leaked = False

        def close_simulation() -> None:
            nonlocal leaked
            if session is None or session._sim is None:  # pyright: ignore[reportPrivateUsage]
                return
            held = session._sim  # pyright: ignore[reportPrivateUsage]
            leaked = sys.getrefcount(held) - 1 > baseline
            session._sim = None  # pyright: ignore[reportPrivateUsage]
            del held  # the destructor closes the transport unless something else holds it

        def reap_peer() -> None:
            try:
                if body_error is None and not leaked:
                    peer.wait(clean_exit_s)
            finally:
                peer.stop(stop_grace_s)  # runs even if the clean wait raised (R-002)

        if body_error is None:
            _record_cleanup_error(errors, close_simulation)
            _record_cleanup_error(errors, reap_peer)
        else:
            # A failed session is stopped BEFORE the Simulation closes: the Simulation itself did
            # not fail, so its destructor would send CLOSED and the peer would report a clean end
            # (exit 0) for a session the driver aborted.
            _record_cleanup_error(errors, reap_peer)
            _record_cleanup_error(errors, close_simulation)
        _record_cleanup_error(errors, lambda: _unlink_segment(segment))
        if leaked:
            errors.append(
                LockstepError(
                    "the Simulation was still referenced when the session ended (a kept "
                    "Session.registry view?), so its transport could not close; the peer was "
                    "stopped"
                )
            )
        # An interrupt (KeyboardInterrupt, SystemExit) during cleanup is re-raised once every step
        # has run, chained to the body's exception, never reduced to a note (R-002). Any other
        # cleanup error is a note on the body's exception, or raised itself after a clean body.
        interrupts = [e for e in errors if not isinstance(e, Exception)]
        others = [e for e in errors if isinstance(e, Exception)]
        if interrupts:
            first: BaseException = interrupts[0]
            for error in [*interrupts[1:], *others]:
                first.add_note(f"astra.lockstep cleanup also failed: {error!r}")
            raise first
        if others:
            if body_error is not None:
                for error in others:
                    body_error.add_note(f"astra.lockstep cleanup also failed: {error!r}")
            else:
                raised = others[0]
                for error in others[1:]:
                    raised.add_note(f"astra.lockstep cleanup also failed: {error!r}")
                raise raised


@dataclasses.dataclass(frozen=True)
class RunResult:
    """One closed-loop run, every value copied out of the (closed) ``Simulation``."""

    halt: Halt | None
    frame: int
    exchanges: float | None
    missed: float | None
    recorder: bytes
    peer_returncode: int | None
    peer_summary: PeerSummary | None
    peer_stderr: str

    @property
    def sha256(self) -> str:
        return hashlib.sha256(self.recorder).hexdigest()


def run_closed_loop(
    config: dict[str, Any],
    peer_argv: Sequence[str],
    *,
    plugin_dir: str | os.PathLike[str] | None = None,
    on_peer_launched: Callable[[PeerProcess], None] | None = None,
    on_ready: Callable[[Session], None] | None = None,
) -> RunResult:
    """One lockstep session: handshake, ``run()`` for ``sim.frames``, copy the counters and the
    recorder, close, and reap the peer. ``on_peer_launched`` runs right after the peer's launch;
    ``on_ready`` after the handshake, before ``run()``."""
    with lockstep_session(
        config, peer_argv, plugin_dir=plugin_dir, on_peer_launched=on_peer_launched
    ) as session:
        if on_ready is not None:
            on_ready(session)
        halt = session.run()
        frame = session.frame
        exchanges = session.signal("transport.exchanges")
        missed = session.signal("transport.missed")
        recorder = session.recorder_bytes()
        peer = session.peer
    return RunResult(
        halt, frame, exchanges, missed, recorder, peer.returncode, peer.summary(), peer.stderr
    )


@dataclasses.dataclass(frozen=True)
class Recording:
    """The three recorder streams as numpy structured arrays (dtypes above)."""

    requests: np.ndarray
    replies: np.ndarray
    states: np.ndarray


def parse_recording(data: bytes) -> Recording:
    """Validates the recorder header and splits the streams; ``ValueError`` on any mismatch."""
    header_size = RECORDER_HEADER_DTYPE.itemsize
    if len(data) < header_size:
        raise ValueError("recorder bytes shorter than the header")
    header = np.frombuffer(data, dtype=RECORDER_HEADER_DTYPE, count=1)[0]
    expected = {
        "magic": RECORDER_MAGIC,
        "version": RECORDER_VERSION,
        "request_size": REQUEST_DTYPE.itemsize,
        "reply_size": REPLY_DTYPE.itemsize,
        "state_size": STATE_DTYPE.itemsize,
        "reserved": 0,
    }
    for field, value in expected.items():
        if int(header[field]) != value:
            raise ValueError(f"recorder header {field} = {int(header[field])}, expected {value}")
    counts = [int(header[k]) for k in ("request_count", "reply_count", "state_count")]
    dtypes = (REQUEST_DTYPE, REPLY_DTYPE, STATE_DTYPE)
    total = header_size + sum(n * d.itemsize for n, d in zip(counts, dtypes, strict=True))
    if total != len(data):
        raise ValueError(f"recorder is {len(data)} bytes, its header implies {total}")
    streams: list[np.ndarray] = []
    offset = header_size
    for count, dtype in zip(counts, dtypes, strict=True):
        streams.append(np.frombuffer(data, dtype=dtype, count=count, offset=offset))
        offset += count * dtype.itemsize
    return Recording(streams[0], streams[1], streams[2])
