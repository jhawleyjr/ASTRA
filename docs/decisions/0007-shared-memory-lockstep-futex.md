# ADR-0007 — Shared-memory lockstep transport with raw futex sequence words

Status: **accepted**, 2026-09-14. Elaborates ADR-0000 row "Embedded SW hosting (digital)"; `docs/architecture.md` §5 modes, §10 Transport.

## Context

In v1 the flight software runs as a separate process on the same host and must be stepped in lockstep by
the sim, faster than real time, with deterministic frame-by-frame exchange. The same primitive should still
be correct the day the sim thread runs under `SCHED_FIFO` on PREEMPT_RT.

## Decision

`SharedMemoryTransport` (`plugins/sim/transport/shm`): `shm_open` + `mmap` of a region holding a header
(ICD magic/version, peer pid), the two ICD frames, and two 32-bit sequence words. Per exchange: the sim
writes `sim_to_sw`, publishes `req_seq = frame`, `FUTEX_WAKE`; the flight software waits on `req_seq`,
computes, writes `sw_to_sim`, publishes `ack_seq = frame`, `FUTEX_WAKE`; the sim waits on `ack_seq` with
a configured timeout. Peer liveness is the pid in the header plus the timeout. The transport has its own
divisor (default 10 → 100 Hz); between exchanges the sim holds the last commands (zero-order hold). It
blocks the scheduler until the peer answers, in either clock mode. `LoopbackTransport` calls an in-process
functor for tests. `embedded/hal/` has the mirror-image `shm` backend.

## Rejected alternatives

- **pthread condition variables in shared memory.** glibc condvars are not priority-inheritance aware,
  which matters under `SCHED_FIFO`; raw futex words are the same primitive without that liability and work
  identically for the non-RT v1.
- **POSIX named semaphores.** Two semaphores would express the handshake, but they carry no sequence
  number, so a missed or duplicated exchange is undetectable; the sequence words are the health signal.
- **iceoryx2.** Still 0.x with breaking releases; one process pair does not need a pub/sub broker.
- **UDP loopback.** Would let the digital and later network transports share code, but a socket adds
  syscalls, copies, and no lockstep guarantee; it remains the natural shape for `UdpTransport` later.

## Consequences

- A missed or late exchange, a dead peer, or a frame-counter discontinuity is a transport-health `halt`
  (§15); the run is no longer meaningful.
- M4 acceptance: 10,000 lockstep exchanges with zero missed; killing the autopilot mid-run halts within
  one timeout, not a hang; bit-identical logs across two runs.
- The futex protocol is documented in the header of the shm plugin and mirrored byte-for-byte in the HAL.
