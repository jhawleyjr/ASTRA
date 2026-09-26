"""The sim host for tests/python/test_lockstep.py (M4 PR 6,
docs/plans/m4-pr6-lockstep-acceptance.md,
"Placement of runs"). Test support only.

Every lockstep run in the pytest suite happens in one of these child processes, never in pytest
itself: ``Simulation.run()`` holds the GIL, so a hang here must be killable from outside (the test's
hard timeout kills this process group), R3's runs must be fresh processes, and the pytest process
must stay free to act as a killer.

Protocol on stdout, one line each, flushed: ``PID <role> <pid>`` as soon as a process is launched
(so the subreaping pytest process can reap it if this host dies), ``SEGMENT <name>``, ``READY``, and
a
final ``RESULT <json>``.
"""

from __future__ import annotations

import argparse
import json
import os
import select
import signal
import subprocess
import sys
import time
from pathlib import Path
from typing import Any

import numpy as np

from astra import _core
from astra import lockstep as ls

SETPOINTS = ls.Setpoints(altitude_m=10000.0, airspeed_mps=230.0, heading_rad=0.0)
SIGNALER = Path(__file__).with_name("lockstep_signaler.py")
KILL_TIMEOUT_MS = 500  # M4 Q3: timing tests use a 500 ms timeout


def say(*parts: object) -> None:
    print(*parts, flush=True)


def report_pid(role: str, pid: int) -> None:
    say("PID", role, pid)


def result(payload: dict[str, Any]) -> None:
    say("RESULT", json.dumps(payload))


def halt_dict(halt: ls.Halt | None) -> dict[str, Any] | None:
    return (
        None if halt is None else {"check": halt.check, "source": halt.source, "frame": halt.frame}
    )


def read_notify(fd: int, timeout_s: float) -> bytes:
    ready, _, _ = select.select([fd], [], [], timeout_s)
    return os.read(fd, 1) if ready else b"timeout"


def peer_argv(args: argparse.Namespace, config: dict[str, Any], **notify: int) -> list[str]:
    if args.peer == "autopilot":
        trim = ls.trim_for(args.config, SETPOINTS)
        return ls.autopilot_argv(args.autopilot, config, SETPOINTS, trim, **notify)
    if args.peer == "hold":
        trim = ls.trim_for(args.config, SETPOINTS)
        return ls.hold_peer_argv(
            args.echo_peer, config, (trim.elevator_rad, 0.0, 0.0, trim.throttle)
        )
    return ls.echo_peer_argv(args.echo_peer, config)


def on_launch(peer: ls.PeerProcess) -> None:
    report_pid("peer", peer.pid)


def stop_helper(helper: subprocess.Popen[str]) -> str:
    """SIGTERM, bounded communicate, SIGKILL: the helper is always reaped."""
    if helper.poll() is None:
        helper.send_signal(signal.SIGTERM)
    try:
        out, _ = helper.communicate(timeout=10)
    except subprocess.TimeoutExpired:
        helper.kill()
        out, _ = helper.communicate()
    return out


def scenario_run(args: argparse.Namespace) -> None:
    """One full closed-loop run: recorder bytes to --out, a summary as RESULT."""
    config = ls.load_config(
        args.config, segment=ls.new_segment_name(), frames=args.frames, seed=args.seed
    )
    argv = peer_argv(args, config)
    out = Path(args.out)
    payload: dict[str, Any] = {"argv": argv[1:]}
    if args.jitter_seed is None:
        run = ls.run_closed_loop(config, argv, on_peer_launched=on_launch)
    else:
        jitter_out = ""
        window: list[int] = []
        with ls.lockstep_session(config, argv, on_peer_launched=on_launch) as session:
            helper = subprocess.Popen(
                [
                    sys.executable,
                    str(SIGNALER),
                    "jitter",
                    "--pid",
                    str(session.peer.pid),
                    "--seed",
                    str(args.jitter_seed),
                ],
                stdout=subprocess.PIPE,
                text=True,
            )
            report_pid("signaler", helper.pid)
            try:
                window.append(time.monotonic_ns())
                halt = session.run()
                window.append(time.monotonic_ns())
            finally:
                jitter_out = stop_helper(helper)
            frame = session.frame
            exchanges = session.signal("transport.exchanges")
            missed = session.signal("transport.missed")
            recorder = session.recorder_bytes()
            peer = session.peer
        run = ls.RunResult(
            halt, frame, exchanges, missed, recorder, peer.returncode, peer.summary(), peer.stderr
        )
        intervals = json.loads(jitter_out) if jitter_out.strip() else []
        inside = [iv for iv in intervals if window[0] <= iv[0] and iv[1] <= window[1]]
        durations = [(iv[1] - iv[0]) / 1e6 for iv in inside]
        payload["jitter"] = {
            "observed_stops": len(intervals),
            "stops_inside_run": len(inside),
            "total_stopped_ms": sum(durations),
            "max_stopped_ms": max(durations, default=0.0),
        }
    (out / "recorder.bin").write_bytes(run.recorder)
    summary = run.peer_summary
    payload.update(
        {
            "halt": halt_dict(run.halt),
            "frame": run.frame,
            "exchanges": run.exchanges,
            "missed": run.missed,
            "sha256": run.sha256,
            "peer_returncode": run.peer_returncode,
            "peer_summary": None if summary is None else summary.__dict__,
            "peer_stderr": run.peer_stderr,
        }
    )
    result(payload)


def post_halt_facts(session: ls.Session, halt: ls.Halt | None) -> dict[str, Any]:
    """R4: clock frame, state finiteness, recorder shape, and a second step() against the halt."""
    state = session.rigid_body_state()
    frame = session.frame
    recording = ls.parse_recording(session.recorder_bytes())
    halt2 = session.step()
    return {
        "halt": halt_dict(halt),
        "frame": frame,
        "state_finite": bool(np.all(np.isfinite(state))),
        "replies": len(recording.replies),
        "requests": len(recording.requests),
        "last_state_frame": int(recording.states["frame"][-1]),
        "halt2": halt_dict(halt2),
        "frame2": session.frame,
        "state_unchanged": session.rigid_body_state().tobytes() == state.tobytes(),
    }


def scenario_kill(args: argparse.Namespace) -> None:
    """R4: --mode sigkill (kill-to-halt), abort (reply-to-halt bound), wait (wait duration)."""
    config = ls.load_config(
        args.config,
        segment=ls.new_segment_name(),
        timeout_ms=KILL_TIMEOUT_MS,
        peer_timeout_ms=KILL_TIMEOUT_MS,
    )
    trim = ls.trim_for(args.config, SETPOINTS)
    read_end, write_end = os.pipe()
    open_fds = {read_end, write_end}

    def close_fd(fd: int) -> None:
        if fd in open_fds:
            os.close(fd)
            open_fds.discard(fd)

    notify = {"stop_after": 500} if args.mode in ("sigkill", "wait") else {"abort_after": 500}
    argv = ls.autopilot_argv(args.autopilot, config, SETPOINTS, trim, notify_fd=write_end, **notify)

    def launched(peer: ls.PeerProcess) -> None:
        close_fd(write_end)  # the autopilot holds the only write end (plan, descriptor table)
        report_pid("peer", peer.pid)

    payload: dict[str, Any] = {"mode": args.mode}
    try:
        with ls.lockstep_session(
            config, argv, pass_fds=(write_end,), on_peer_launched=launched
        ) as session:
            if args.mode == "sigkill":
                killer = subprocess.Popen(
                    [
                        sys.executable,
                        str(SIGNALER),
                        "kill-on-notify",
                        "--fd",
                        str(read_end),
                        "--pid",
                        str(session.peer.pid),
                    ],
                    pass_fds=(read_end,),
                    stdout=subprocess.PIPE,
                    text=True,
                )
                close_fd(read_end)  # the signaler holds the only read end
                report_pid("signaler", killer.pid)
                try:
                    halt = session.run()
                    t_return = time.monotonic_ns()
                finally:
                    killer_out = stop_helper(killer)
                payload["killer_out"] = killer_out.strip()
                payload["killer_returncode"] = killer.returncode
                payload["t_return"] = t_return
                payload.update(post_halt_facts(session, halt))
            elif args.mode == "abort":
                first = session.run(4990)  # 499 exchanges; the 500th is at frame 4990
                t0 = time.monotonic_ns()  # precedes the final reply by construction (critique C-5)
                halt = session.run()
                t_return = time.monotonic_ns()
                payload["first_halt"] = halt_dict(first)
                payload["notify"] = read_notify(read_end, 5.0).hex()
                payload["t0"] = t0
                payload["t_return"] = t_return
                payload.update(post_halt_facts(session, halt))
            else:  # wait
                first = session.run(5000)  # exactly 500 exchanges, all answered
                payload["first_halt"] = halt_dict(first)
                payload["notify"] = read_notify(read_end, 5.0).hex()
                session.peer.signal(signal.SIGKILL)
                payload["killed_returncode"] = session.peer.wait(5.0)  # killed AND reaped
                t0 = time.monotonic_ns()
                halt = session.step()  # frame 5000: an exchange frame with no peer at all
                t1 = time.monotonic_ns()
                payload["duration_ms"] = (t1 - t0) / 1e6
                payload.update(post_halt_facts(session, halt))
            peer = session.peer
        payload["peer_returncode"] = peer.returncode
    finally:
        for fd in list(open_fds):
            close_fd(fd)
    result(payload)


def scenario_pause(args: argparse.Namespace) -> None:
    """R4b sim pause: an idle sim longer than peer_timeout_ms; the next exchange halts."""
    config = ls.load_config(
        args.config,
        segment=ls.new_segment_name(),
        timeout_ms=KILL_TIMEOUT_MS,
        peer_timeout_ms=KILL_TIMEOUT_MS,
    )
    argv = peer_argv(args, config)
    with ls.lockstep_session(config, argv, on_peer_launched=on_launch) as session:
        first = session.run(1000)
        time.sleep(0.8)
        peer_status = session.peer.wait(5.0)
        t0 = time.monotonic_ns()
        halt = session.run()
        t1 = time.monotonic_ns()
        payload = {
            "first_halt": halt_dict(first),
            "peer_returncode": peer_status,
            "halt": halt_dict(halt),
            "halt_ms": (t1 - t0) / 1e6,
            "frame": session.frame,
        }
    result(payload)


def barrier_run(run: Any, signal_value: Any) -> None:
    """R4b / driver-death barrier (critique C-6): 100 confirmed exchanges, READY, then a long
    run."""
    first = run(1000)
    exchanges = signal_value("transport.exchanges")
    if first is not None or exchanges != 100.0:
        raise SystemExit(f"barrier not reached: halt {first}, exchanges {exchanges}")
    say("READY")
    run(200000)
    say("COMPLETED")  # only if the parent was too slow: the test then fails (exit 0, not 2)


def scenario_sim_only(args: argparse.Namespace) -> None:
    """R4b sim death: the sim alone on the parent's segment; the parent launched the autopilot."""
    config = ls.load_config(
        args.config,
        segment=args.segment,
        frames=201000,
        timeout_ms=KILL_TIMEOUT_MS,
        peer_timeout_ms=KILL_TIMEOUT_MS,
    )
    sim = _core.Simulation.from_config(
        _core.Config.from_dict(config, origin="sim-only"), _core.default_plugin_dir()
    )
    registry = sim.registry
    barrier_run(lambda n: sim.run(n) or None, registry.find)


def scenario_driver_death(args: argparse.Namespace) -> None:
    """Driver death after the handshake: the whole driver in this process, behind the barrier."""
    config = ls.load_config(
        args.config,
        segment=ls.new_segment_name(),
        frames=201000,
        timeout_ms=KILL_TIMEOUT_MS,
        peer_timeout_ms=KILL_TIMEOUT_MS,
    )
    say("SEGMENT", ls.transport_block(config)["segment"])
    argv = peer_argv(args, config)
    with ls.lockstep_session(config, argv, on_peer_launched=on_launch) as session:
        barrier_run(session.run, session.signal)


def scenario_pre_handshake(args: argparse.Namespace) -> None:
    """Driver death before the handshake: create the segment with no peer and wait to be killed."""
    config = ls.load_config(args.config, segment=ls.new_segment_name(), handshake_timeout_ms=30000)
    say("SEGMENT", ls.transport_block(config)["segment"])
    _core.Simulation.from_config(
        _core.Config.from_dict(config, origin="pre-handshake"), _core.default_plugin_dir()
    )
    say("COMPLETED")


def proc_state(pid: int) -> str:
    try:
        with open(f"/proc/{pid}/stat", encoding="ascii", errors="replace") as f:
            text = f.read()
    except FileNotFoundError:
        return "gone"
    return text[text.rindex(")") + 2]


def scenario_lifecycle(args: argparse.Namespace) -> None:
    """The driver's cleanup on each exit path (plan, "Driver" row): facts reported here, asserted
    by pytest."""
    case = args.case
    segment = ls.new_segment_name()
    handshake = 300 if case in ("bad-args", "term-ignoring-peer") else None
    config = ls.load_config(args.config, segment=segment, handshake_timeout_ms=handshake)
    trim = ls.trim_for(args.config, SETPOINTS)
    argv = ls.autopilot_argv(args.autopilot, config, SETPOINTS, trim)
    if case == "missing-exe":
        argv[0] = "/nonexistent/astra_autopilot"
    elif case == "bad-args":
        argv.append("--bogus")
    elif case == "term-ignoring-peer":
        argv = [
            sys.executable,
            "-c",
            "import signal, time; signal.signal(signal.SIGTERM, signal.SIG_IGN); time.sleep(60)",
        ]
    elif case == "popen-failure":
        # Passes the executable check (a file with the execute bit) but exec() fails (ENOEXEC).
        bogus = Path(args.out) / "not_a_binary"
        bogus.write_bytes(b"\x00\x01 not an executable format\n")
        bogus.chmod(0o755)
        argv[0] = str(bogus)
    peers: list[ls.PeerProcess] = []
    facts: dict[str, Any] = {"case": case}

    def launched(peer: ls.PeerProcess) -> None:
        peers.append(peer)
        report_pid("peer", peer.pid)

    kept: Any = None
    try:
        with ls.lockstep_session(config, argv, on_peer_launched=launched) as session:
            if case == "normal":
                facts["halt"] = halt_dict(session.run(1000))
                # Held past the session's end: a copy, so it must not trip the refcount check
                # (Joe's Q2); the retained-registry case shows that check is live.
                kept = session.recorder_bytes()
                facts["recorder_bytes"] = len(kept)
            else:
                session.run(100)
            if case == "body-exception":
                raise RuntimeError("body failed on purpose")
            if case == "stopped-peer":
                os.kill(session.peer.pid, signal.SIGSTOP)
                deadline = time.monotonic() + 2.0
                while (
                    proc_state(session.peer.pid) not in ("T", "t") and time.monotonic() < deadline
                ):
                    time.sleep(0.001)
                facts["stopped_state"] = proc_state(session.peer.pid)
                raise RuntimeError("body failed with the peer stopped")
            if case == "keyboard-interrupt":
                raise KeyboardInterrupt
            if case == "retained-registry":
                kept = session.registry
    except BaseException as error:  # the facts, not the exception, are the output
        facts["exception"] = type(error).__name__
        facts["message"] = str(error)
        facts["notes"] = list(getattr(error, "__notes__", []))
    del kept
    facts["segment_exists"] = (ls.SHM_DIRECTORY / segment).exists()
    facts["peer_launched"] = bool(peers)
    if peers:
        peer = peers[0]
        facts["peer_returncode"] = peer.returncode
        summary = peer.summary()
        facts["peer_summary"] = None if summary is None else summary.__dict__
        facts["peer_alive"] = proc_state(peer.pid) not in ("gone", "Z", "X")
    result(facts)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("scenario")
    parser.add_argument("--config", required=True)
    parser.add_argument("--autopilot", required=True)
    parser.add_argument("--echo-peer", required=True)
    parser.add_argument("--peer", choices=("autopilot", "hold", "echo"), default="autopilot")
    parser.add_argument("--out", default=".")
    parser.add_argument("--seed", type=int)
    parser.add_argument("--frames", type=int)
    parser.add_argument("--jitter-seed", type=int)
    parser.add_argument("--mode", choices=("sigkill", "abort", "wait"))
    parser.add_argument("--segment")
    parser.add_argument("--case")
    args = parser.parse_args()
    scenarios = {
        "run": scenario_run,
        "kill": scenario_kill,
        "pause": scenario_pause,
        "sim-only": scenario_sim_only,
        "driver-death": scenario_driver_death,
        "pre-handshake": scenario_pre_handshake,
        "lifecycle": scenario_lifecycle,
    }
    scenarios[args.scenario](args)
    return 0


if __name__ == "__main__":
    sys.exit(main())
