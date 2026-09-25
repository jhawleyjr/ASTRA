# ADR-0023: Monotonic-clock exception to ADR-0010 for the lockstep transport timeout

Status: proposed
Date: 2026-09-24
Owner: Joe
Related/supersedes: supersedes ADR-0010 **in part**: only the Consequences bullet that permits no `steady_clock`
(or equivalent) call outside the `RealTime` pacer, and the matching Context sentence "no model, transport, or
embedded process reads the wall clock", and only for the purpose below. Everything else in ADR-0010 (the integer
frame counter as time's source of truth, `t` derived, integer-divisor rates, frame-counter continuity) stays in
force. Elaborates ADR-0007 and ADR-0022.

## Context

A lockstep wait that detects a dead peer needs a timeout, and a wait that survives spurious wakeups and `EINTR`
must re-wait against the *same* deadline, never a fresh timeout (plan critique F-3: a fresh timeout per wakeup
lets a peer stretch detection to several periods). That needs one monotonic time reading per wait. ADR-0010 and
`AGENTS.md` restrict clock access to the `RealTime` pacer. The alternative, a relative-timeout retry loop with a
bounded count and no clock read, gives a looser and unstated bound.

## Decision

A lockstep transport may read `CLOCK_MONOTONIC` (via `clock_gettime`, or the absolute-deadline form of the futex
wait) **only** to compute the absolute deadline of one blocking wait, and the time remaining to it on a re-wait.
Exactly two sites are allowed:

1. the sim-side shm transport's wait (handshake and acknowledgment waits), and
2. the `embedded/hal` shm backend's wait, against the peer's own driver-supplied `peer_timeout_ms` (>= the sim's
   `timeout_ms`, ADR-0024); it is what lets the autopilot exit nonzero when the sim dies or pauses longer than
   that (plan R4b).

Each site is confined to one function, so a future symbol scan can allowlist it by name. Invariants:

- The reading never enters simulation state, a signal, an ICD field, the exchange recorder, or a log. It supplies no
  `t`, no `frame`, and no delay to any model.
- Expiry has exactly two outcomes: a terminal `kTransport` halt (sim) or a nonzero exit (HAL). It is never a value
  that continues the run, so wall time cannot change a trajectory.
- `hal_now()` stays frame-derived, the autopilot reads no clock, and `LoopbackTransport` reads none.
- The `RealTime` pacer's own clock use is unchanged.

`AGENTS.md` and architecture §2/§5/§15 carry the matching one-clause updates. No wall-clock symbol scan exists
today (architecture §15's guard is prose); when one is added it must allowlist these two sites.

## Rejected alternatives

- **Relative-timeout retry loop, bounded retries, no clock read.** No exception needed, but the bound on detection
  is retries times the period, looser than "within one timeout" (plan Q2, Q3).
- **No timeout.** A dead peer hangs the sim, which the M4 acceptance forbids.
- **`CLOCK_REALTIME`.** Steps with NTP and manual clock changes; a deadline must be monotonic.
- **A `timerfd` or a helper thread.** Adds a second thread or an fd to a single-threaded scheduler for one read.

## Consequences and validation

Two named clock sites in the whole tree, both reviewable by grep. Tests measure wait duration and kill-to-halt
separately, and include a repeated-`FUTEX_WAKE`-without-store case proving the deadline does not restart (plan R4).
Check for this record: A1.

## Approval

Pending. Joe's decision, date, and any requested changes are recorded here when given.
