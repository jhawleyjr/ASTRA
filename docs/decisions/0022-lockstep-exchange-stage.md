# ADR-0022: Lockstep exchange stage owned by `Simulation`

Status: **accepted**, 2026-09-24
Date: 2026-09-24
Owner: Joe
Related/supersedes: elaborates ADR-0007 (transport protocol; its shm-specific parts are amended by ADR-0024),
ADR-0009 (plugins), ADR-0010 (frame counter), ADR-0011 (vehicle list), ADR-0017 (write seam) and ADR-0006 (ICD
frames). Resolves ADR-0017's "Known gap" for the transport by a route ADR-0017 did not list. Does **not**
supersede any of them. Drafted with `docs/plans/m4-lockstep.md` (decisions Q1 to Q17, critique F-1 to F-10).

## Context

M4 needs a separate autopilot process to fly the notional transport over a lockstep transport. ADR-0007 fixed the
wire protocol but left open who calls the transport, when in the frame, how commands reach the vehicle, and what
failure means. ADR-0017 disclosed that a writer which is not a `WrenchSource` has no seam-provided route to a write
handle, and depends on `vehicle.models` load order, which nothing validates. `PluginDescriptor::create` returns a
`Model`, so a transport implemented as a model would also force a descriptor change and a core API bump.

## Decision

**Ownership.** `Simulation` (core) owns the exchange stage. A transport plugin under `plugins/sim/transport/`
implements a `Transport` interface (`kCoreApiVersion` 7 to 8; its exact shape is PR 3's, under this record). At
init, after every model's `bind_inputs()` has run, `Simulation` resolves `InputRegistry::find()` handles from a
config-declared command map, so there is no load-order dependency. `ActuatorModel` remains an open question
(architecture §19), not decided here.

**Placement and timing.** At `frame % transport.divisor == 0` (default 10), for the bound vehicle (v1: index 0),
after the frame's environment sample and before that vehicle's `step()`: pack truth into a zero-initialized
`astra_sim_to_sw_v1` (padding and `reserved` are zero, so recorded bytes are deterministic), publish, block for the
reply up to the timeout, validate it (magic, version, `reply.frame == frame`), write the command map through the
handles (pending), and let the same frame's `latch()` commit it. Commands computed from frame-k state are applied
from frame k, with zero compute delay (disclosed). Between exchanges the latched commands hold (zero-order hold).

**Config** (unknown keys are errors, `ConfigError` naming the path; exact spelling is a PR 3 detail):
`transport: { plugin, divisor, timeout_ms, handshake_timeout_ms, peer_timeout_ms, commands, limits }`. The ICD-field-to-input-name
map lives in config data, never in `core/` (principle 6). `astra_sw_to_sim_v1.throttle[2]` maps to the single shared
`controls.throttle` input (ADR-0017): the map accepts both entries and **requires equality**; a mismatch halts.
Defaults: `timeout_ms` 1000, `handshake_timeout_ms` 5000. `peer_timeout_ms` is the autopilot-side wait (ADR-0024): the
driver passes the same value to the HAL, and init rejects a value below `timeout_ms` with a `ConfigError` naming it
(its default is PR 3's to set, not below `timeout_ms`). Lockstep runs are driven by back-to-back `run()`/`step()`
calls: a pause longer than `peer_timeout_ms` ends the peer nonzero and the next exchange halts `kTransport`. `limits` are per-command bounds; a command outside its
limit **halts**, it is never clamped (clamping would hide an autopilot fault). No `transport` block means behavior
bit-identical to a build without this record (M3's frozen regression stays untouched).

**Receive state machine.** After publishing request `frame` the sim waits on `ack_seq`. An unchanged `ack_seq`
(including a spurious wakeup or a `FUTEX_WAKE` with no store) means *still waiting*, never failure.
`ack_seq == frame` completes the wait, and the payload is then validated. Any other value is a protocol failure:
`ack_seq` moved to something that is neither the previous value nor `frame`, or `reply.frame != frame`. A peer that
re-stores the identical sequence value is undetectable from sequence words alone and is **not claimed
detectable**. The sim checks `frame < UINT32_MAX - 1` at every publication (not only against `sim.frames` at
init, which repeated `run()`/`step()` calls bypass) and halts otherwise: `UINT32_MAX` is the initial sentinel and
`UINT32_MAX - 1` is reserved as the shutdown value of `req_seq` (ADR-0024), so neither can be a frame.

**Command validation order.** On every received reply the exchange stage checks, in this order and before any
`InputHandle::write()`: (1) magic, version, and echoed frame; (2) **every command value is finite**; (3) `throttle[2]`
equality; (4) each command against its configured limit. The first failure halts. A non-finite command halts
`kTransport` naming the ICD field, unconditionally. The check is needed regardless of routing, because a NaN also
defeats a `v < lo || v > hi` limit test (both comparisons are false). It is **not** delegated to ADR-0017's
`health.input`: that check honors `sim.health.input` (`warn` or `off` keeps the last good value and continues), so
routing through it could not guarantee a halt. `sim.health.input` is left unconstrained, and ADR-0017's check
remains as a backstop for every other writer.

**Failure is unconditionally terminal.** Timeout, an unexpected or out-of-order ack, echoed-frame mismatch, bad
magic or version, frame-counter discontinuity, peer closed or gone, sequence range exhausted, a non-finite command,
a limit violation, and a `throttle[2]` mismatch each halt with `HaltReason{kTransport, source = transport plugin
path, frame, vehicle_index}` (the last three name the ICD field). The clock does not advance and
nothing further steps. There is **no `warn`/`off` action** and no `sim.health.transport` key: a run that continues
past a missed lockstep exchange is not meaningful, and letting wall-time expiry change the trajectory would break
determinism. `HealthCheck::kTransport` is a new additive enum value (bound to Python). A handshake failure before
frame 0 is an init error naming `transport`.

**Timeout reading.** "Within one timeout" is strict: peer death is detected by one immutable absolute deadline
per wait (ADR-0023), kill-to-halt is no greater than `timeout_ms` plus a wake/measurement allowance (the greater
of 20 ms and 10 % of the timeout), and never an extra period. Any looser bound is a weakened acceptance.

**Determinism.** The sim zeroes its request struct before packing; the peer zeroes its reply struct before
filling it. In M4 the autopilot sends a constant `mode_flags` and `fault_flags = 0`; the sim records both and acts
on neither.

**Exchange recorder (M4's log).** A preallocated in-memory recorder with three streams, dumped raw after the run:
each `sim_to_sw` frame, each `sw_to_sim` frame (all 64 reply bytes), and the rigid-body state at **every major
frame** (a superset of per-exchange sampling, which would leave the nine frames between exchanges and the tail
unchecked). A fixed header (magic, version, stream counts, record sizes) precedes fixed-size native-endian records,
so byte equality covers counts and layout, not just payloads. Same-build, same-machine determinism only. M5's HDF5
logger supersedes the recorder.

## Rejected alternatives

- **The transport as a `Model` writing through the seam from `step()`.** Depends on `vehicle.models` load order
  (ADR-0017's disclosed gap) and forces `PluginDescriptor::create` and the seam to change.
- **`ActuatorModel` as a `WrenchSource` carrying the write route.** Decides an open architecture question to serve
  one milestone; M4 has no dynamic actuator (commanded equals achieved).
- **`warn`/`off` for transport health.** Contradicts ADR-0007's halt consequence and would let timing change results.
- **Clamping out-of-limit commands.** Hides autopilot faults.
- **Routing a non-finite command through `health.input` only.** `sim.health.input` may be `warn` or `off`, which
  keeps the last good value and continues, so a NaN reply would not halt (Codex review finding, PR 1).
- **Constraining `sim.health.input` to `halt` when a transport is present.** Works, but restricts a global setting
  to serve one stage and leaves the limit check exposed to NaN; a local finite check is simpler.
- **Sampling state only at exchange frames.** Leaves 90 % of frames and the tail unchecked against the acceptance bands.
- **Detecting a re-stored identical sequence value.** Not possible from sequence words alone; claiming it would be false.

## Consequences and validation

Additive behind an absent `transport` block, so reverting any M4 PR restores today's behavior. The exchange path
is allocation-free (extends `tests/physics/no_alloc_test.cpp`). Acceptance adds a NaN and an infinity command under each of `sim.health.input` = `halt`, `warn`, `off`, each
halting `kTransport` naming the ICD field. Validation is in `docs/plans/m4-lockstep.md`
R1 to R12b and acceptance commands A2 to A6; this record's own check is A1 (`docs_check`, `git diff --check`, one
Codex review). Not covered: HWIL timing, `SCHED_FIFO` behavior, cross-machine layout.

## Approval

Accepted by Joe, 2026-09-24. Decision record: `docs/reviews/m4-pr1-adrs-human.md` (Decision record, state approved), which approved ADR-0022/0023/0024 with one change, correction round 2 (the HAL wait uses a driver-supplied `peer_timeout_ms`, plan Q19), after independent Codex review with two fix rounds (plan Q18, Q19); Joe's own decisions in round 1 fixed the non-finite-command routing (F2) and the shutdown value (F3). Retained Codex output: `docs/reviews/evidence/`.
