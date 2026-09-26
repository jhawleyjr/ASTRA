"""Acts on a lockstep peer while the sim host holds the GIL inside ``Simulation.run()`` (M4 PR 6,
docs/plans/m4-pr6-lockstep-acceptance.md D6, D8). Launched by ``lockstep_child.py``; test support
only.

    kill-on-notify --fd R --pid P   block on the notify pipe's read end R; on its byte, take
                                    monotonic_ns BEFORE kill(), SIGKILL P, print the timestamp.
                                    EOF before the byte (the peer died first) prints "EOF", exit 3.
    jitter --pid P --seed S         until SIGTERM: sleep U(0.5, 5) ms, SIGSTOP P, wait until
                                    /proc/P/stat reads T (observed stop), sleep U(0.1, 10) ms
                                    (requested, not a bound), SIGCONT, wait until it no longer
                                    reads T (observed resume). A final SIGCONT on exit; prints the
                                    observed [stop_ns, resume_ns] intervals as JSON.
"""

from __future__ import annotations

import argparse
import contextlib
import json
import os
import random
import signal
import sys
import time

_POLL_S = 0.0001
_STATE_WAIT_S = 0.5


def _state(pid: int) -> str | None:
    try:
        with open(f"/proc/{pid}/stat", encoding="ascii", errors="replace") as f:
            text = f.read()
    except FileNotFoundError:
        return None
    return text[text.rindex(")") + 2]  # the field after "(comm)"


def _wait_state(pid: int, stopped: bool) -> int | None:
    """monotonic_ns once the stopped-ness of pid equals `stopped`; None if it is gone or too
    slow."""
    deadline = time.monotonic() + _STATE_WAIT_S
    while time.monotonic() < deadline:
        state = _state(pid)
        if state is None or state in "ZX":
            return None
        if (state in "Tt") == stopped:
            return time.monotonic_ns()
        time.sleep(_POLL_S)
    return None


def kill_on_notify(fd: int, pid: int) -> int:
    byte = os.read(fd, 1)
    if not byte:
        print("EOF", flush=True)
        return 3
    t_kill = time.monotonic_ns()
    os.kill(pid, signal.SIGKILL)
    print(t_kill, flush=True)
    return 0


def jitter(pid: int, seed: int) -> int:
    stop_requested = False

    def on_term(_signum: int, _frame: object) -> None:
        nonlocal stop_requested
        stop_requested = True

    signal.signal(signal.SIGTERM, on_term)
    rng = random.Random(seed)
    intervals: list[list[int]] = []
    try:
        while not stop_requested:
            time.sleep(rng.uniform(0.0005, 0.005))
            if stop_requested:
                break
            try:
                os.kill(pid, signal.SIGSTOP)
            except ProcessLookupError:
                break
            stopped = _wait_state(pid, stopped=True)
            time.sleep(rng.uniform(0.0001, 0.010))
            try:
                os.kill(pid, signal.SIGCONT)
            except ProcessLookupError:
                break
            resumed = _wait_state(pid, stopped=False)
            if stopped is not None and resumed is not None:
                intervals.append([stopped, resumed])
    finally:
        with contextlib.suppress(ProcessLookupError):
            os.kill(pid, signal.SIGCONT)
    print(json.dumps(intervals), flush=True)
    return 0


def main() -> int:
    parser = argparse.ArgumentParser()
    sub = parser.add_subparsers(dest="mode", required=True)
    kill = sub.add_parser("kill-on-notify")
    kill.add_argument("--fd", type=int, required=True)
    kill.add_argument("--pid", type=int, required=True)
    jit = sub.add_parser("jitter")
    jit.add_argument("--pid", type=int, required=True)
    jit.add_argument("--seed", type=int, required=True)
    args = parser.parse_args()
    if args.mode == "kill-on-notify":
        return kill_on_notify(args.fd, args.pid)
    return jitter(args.pid, args.seed)


if __name__ == "__main__":
    sys.exit(main())
