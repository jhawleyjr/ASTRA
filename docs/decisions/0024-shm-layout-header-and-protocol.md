# ADR-0024: Shared shm layout header and lockstep protocol refinements (supersedes ADR-0007 in part)

Status: proposed
Date: 2026-09-24
Owner: Joe
Related/supersedes: supersedes ADR-0007 **in part**: its Decision sentence "`embedded/hal/` has the mirror-image
`shm` backend", its Consequences line "documented in the header of the shm plugin and mirrored byte-for-byte in
the HAL", the `req_seq = frame` / `ack_seq = frame` rule as literally written (a zero-initialized word collides
with frame 0), and "peer liveness is the pid in the header plus the timeout". **Stays in force**: `shm_open` +
`mmap` with raw futex words and its rejected alternatives (condvars, semaphores, iceoryx2, UDP), the transport
divisor and zero-order hold, blocking the scheduler in either clock mode, `LoopbackTransport`, the halt
consequence, and the M4 acceptance. Elaborates ADR-0008 (its link rule is unchanged) and ADR-0006. The frame
semantics and failure policy are ADR-0022; the clock read is ADR-0023.

## Context

ADR-0007 said the protocol is documented in the plugin and mirrored byte-for-byte in the HAL. Two hand-kept copies
of a wire layout drift, and the plan critique found the protocol underspecified: a zero-initialized sequence word
looks like a request for frame 0, nothing says who initializes the region or when a payload is safe to read (F-2),
a graceful end is indistinguishable from peer death (F-5), and pid probing is defeated by zombies.

## Decision

**One layout header.** A single header in `embedded/hal/` (working name `astra_shm_layout.h`) defines the region
layout and protocol constants. It includes only `<stdint.h>` and `icd/astra_icd.h`, compiles as C11 and C++17, and is
static-asserted. The sim-side shm plugin includes it; the HAL shm backend includes it. It is an include, not a
link dependency, so ADR-0008's rule (embedded links only `icd/` and `embedded/hal/`, never `astra_core`) holds. Any
change to it bumps the region's own layout version (validated in the handshake); it never edits an ICD struct.

**Region contents.** Magic, version, layout size; two readiness words, `sim_state` (`INIT`, `SIM_READY`, `CLOSED`)
and `peer_state` (`NONE`, `PEER_READY`); `req_seq` and `ack_seq`; the two ICD frames; `sim_pid` and `peer_pid`
(diagnostic only). **Every word has exactly one writer**: sim writes `sim_state` and `req_seq`; peer writes
`peer_state` and `ack_seq`. Exact offsets are PR 4's, under this record.

**Sentinel.** Both sequence words are initialized to `UINT32_MAX`; `frame = 0` is therefore distinguishable from a
zeroed word. Comparisons are wrap-safe; the sim halts `kTransport` rather than publish `frame == UINT32_MAX`
(ADR-0022).

**Handshake, header-only.** The sim creates the segment `O_EXCL`, sizes it, initializes the whole header, then
release-stores `sim_state = SIM_READY`. The peer retries `open` until the handshake timeout (so either start order
works), acquire-loads `sim_state`, validates magic, version, and layout size, then release-stores
`peer_state = PEER_READY`. The sim waits for it (handshake timeout) before frame 0. No frame payload is read or
written before both states are visible. No probe frame is used (it would duplicate frame 0). Failure is an init
error naming `transport`.

**Per-exchange ordering.** The writer stores the payload, then release-stores its sequence word, then
`FUTEX_WAKE`s; the reader acquire-loads the sequence word before reading the payload. Futexes are process-shared
(no private flag). The intended wait is the absolute-deadline form (`FUTEX_WAIT_BITSET`, `CLOCK_MONOTONIC`), so a
spurious wakeup or `EINTR` re-waits against the same deadline (ADR-0023).

**Shutdown and ownership.** On normal completion the sim release-stores `sim_state = CLOSED` and wakes the peer;
the HAL maps `CLOSED` to a clean exit (status 0), never to peer death. The creator unlinks on close. Segment names
are `astra_<driver pid>_<nonce>`, chosen by the driver and passed through config; the driver also unlinks its own
segment in a `finally`, terminates then kills and reaps the autopilot with a bounded wait on every exit path, and a
test fixture removes leftovers whose creator pid is dead. There is **no unlink-on-next-start** (nothing safely
identifies old segments) and **no pid probing** (a zombie still answers `kill(pid, 0)`); the timeout is the
liveness detector. A peer that re-stores an identical sequence value is not detectable and is not claimed to be.

## Rejected alternatives

- **Keep mirrored copies plus a layout-equality test.** The equality test catches drift only after the fact and
  leaves two definitions of one wire format.
- **Zero-initialized sequence words.** Collide with a frame-0 request.
- **A probe frame for the handshake.** Duplicates frame 0 and pollutes the recorded stream.
- **One shared state word with two writers.** A read-modify-write race on a word neither side owns; single-writer
  words need no atomics beyond load/store ordering.
- **Unlink-on-next-start; pid liveness.** Neither can be made safe (see above).

## Consequences and validation

The HAL and the shm plugin cannot disagree on layout. ASan cannot see futex memory-order bugs and TSan cannot
follow cross-process futex synchronization (accepted gap, plan Q14); mitigation is a 1e6-exchange Release echo
stress test plus review of acquire/release pairing and single-writer discipline. Tests: two-process echo-peer,
delayed initialization, both start orders, `CLOSED` exit 0, stale segment, no orphans (plan R5, PR 4, PR 6).
Check for this record: A1. No `icd/astra_icd.h` struct changes.

## Approval

Pending. Joe's decision, date, and any requested changes are recorded here when given.
