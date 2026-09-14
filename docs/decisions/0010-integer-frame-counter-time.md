# ADR-0010 — The integer frame counter is time's source of truth; `t` is derived, never accumulated

Status: **accepted**, 2026-09-14. Elaborates ADR-0000 row "Time"; `docs/architecture.md` §5 Clock and modes.

## Context

The sim owns time (principle 1): no model, transport, or embedded process reads the wall clock. Logs must
have exact timestamps, replays must be bit-identical, and the lockstep frame counter in the ICD must agree
with the log without drift after millions of frames.

## Decision

`VirtualClock` holds a `std::uint64_t frame` counter as the single source of truth; only the scheduler
advances it, once per major frame. `t = frame × dt` is computed on demand, never accumulated. `StepContext`
carries `{t, dt, frame}` to every model; the ICD frame carries `frame` and `t`; log timestamps are derived
from `frame`. `RealTime` mode paces the same counter with `timerfd`; the counter, not the timer, is time.

## Rejected alternatives

- **Accumulated double seconds (`t += dt`).** Rounding error grows with frame count, timestamps are
  inexact, and two runs can diverge in the last bit. This is the classic bug.
- **Integer nanoseconds.** Exact, but redundant with the frame counter given a fixed `dt`, and every
  rate check would divide instead of comparing `frame % divisor`.

## Consequences

- Every rate in the system is an integer divisor of the major frame, checked at init; scheduling is
  `frame % divisor == 0`.
- No `std::chrono::system_clock` or `steady_clock` call is permitted in `core/`, `plugins/`, or
  `aircraft/` outside the `RealTime` pacer; the determinism guard (§15) scans plugins for wall-clock symbols.
- Frame-counter continuity across transport exchanges is a health check.
