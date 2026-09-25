# M4 Lockstep: spec and plan

Status: approved by Joe with the decisions recorded in "Open questions" (2026-09-24); all questions closed
Owner: Joe
Risk: 3. Transport/concurrency, `icd/` semantics, health behavior (workflow §Risk tiers).
Intent/approval: Joe's M4 planning request (2026-09-24) and his approval with four changes to the
plan content, folded in below. Spec/plan: **approved by Joe with these decisions, 2026-09-24**. Q5, Q6, Q8, Q10 closed as recommended, Q14 closed as an accepted gap, Q7 decided (chart read 2026-09-24); no question is open.
Base: `origin/main` = `827077b`.
Architecture/ADRs: [architecture](../architecture.md) §5, §10, §11, §15, §16 (M4 row);
[ADR-0006](../decisions/0006-icd-versioned-c11-structs.md),
[ADR-0007](../decisions/0007-shared-memory-lockstep-futex.md),
[ADR-0008](../decisions/0008-monorepo-embedded-separate-cmake.md),
[ADR-0009](../decisions/0009-plugin-architecture.md),
[ADR-0010](../decisions/0010-integer-frame-counter-time.md),
[ADR-0011](../decisions/0011-one-vehicle-as-list-of-one.md),
[ADR-0017](../decisions/0017-control-input-write-seam.md).
System context: the exchange stage sits in architecture §5's per-frame order between the environment step
and each vehicle's `step()`/`latch()`; it consumes vehicle truth state and writes commands through
ADR-0017's `InputRegistry`. Producer of commands: the autopilot process (`embedded/autopilot`). Consumers:
the wrench sources, through the latched inputs.

## Outcome and boundaries

**Outcome.** The notional transport, started from `aircraft/notional_transport/configs/cruise.yaml`'s
trimmed cruise, is flown by a separate C++17 autopilot process that links only `icd/` and `embedded/hal/`,
over a shared-memory futex lockstep transport. M4 acceptance (architecture §16, PLAN.md):
10,000 exchanges with zero missed; altitude within ±30 m and heading within ±2° under light Dryden;
bit-identical logs across two runs; killing the autopilot mid-run halts within one timeout.

**Non-goals.** HDF5 logging and replay (M5); a dynamic `ActuatorModel`; sensor models (ICD v2); `UdpTransport`/
`FpgaTransport`; `RealTime` pacing; §15 envelope monitors; multi-vehicle transport; any change to a
`icd/astra_icd.h` struct layout; goldens, tolerances, or accepted ADR edits (only superseding records, Joe-authorized).

**Invariants.** No transport block in a config means behavior bit-identical to today (M3's frozen regression
untouched). Per-frame path: no allocation, no exceptions, no blocking I/O except the deliberate lockstep wait
(ADR-0007), no `std::string`. Time is the frame counter (ADR-0010). Frames and signs per ADR-0001, SI per ADR-0002.

**Assumptions.** The M3 trim, plugins, and Dryden plugin are correct as shipped (this plan does not re-validate
them). `cruise.yaml`'s frame 0 already carries a Dryden gust of about (-3.08, 0.91, 6.63) m/s (lesson
2026-09-22), so the autopilot starts in turbulence, not still air. Same-build, same-machine determinism only
(lesson 2026-09-21: bits differ across presets). What this feature does not validate: HWIL timing,
`SCHED_FIFO` behavior, cross-machine layout.

## Repository findings (observed 2026-09-24 at `827077b`)

- No `embedded/`, no `plugins/sim/`, no `Transport` interface, and no logger of any kind exist. `Simulation::step`
  (`core/src/kernel/simulation.cpp:562`): environment scheduler, then per vehicle {sample environment,
  `vehicle.step`, `InputRegistry::latch`, `integrate`, atmosphere domain check}, then clock advance.
- `PluginDescriptor::create` returns `unique_ptr<Model>` (`core/include/astra/plugin/descriptor.hpp`), so a
  transport plugin must be a `Model` or the descriptor changes. Either needs a `kCoreApiVersion` bump (7 to 8).
- ADR-0017 "Known gap" leaves the M4 write route open. **Proposed resolution:** `Simulation` (core) owns the
  exchange stage. At init it resolves `InputRegistry::find()` handles from a config-declared command map and
  calls `InputHandle::write()` before `latch()`. No seam extension is needed, the commit is same-frame with no
  lag (§5), and the ICD-field-to-input-name map stays in config (principle 6), never in `core/`.
- `HealthCheck` is a `uint8` enum bound to Python (`python/bindings/_core.cpp:346`). A `kTransport` value
  and transport counters/signals are additive (no `sim.health.transport` action key; failure is terminal).
  `Simulation` `run`/`step` bindings hold the GIL (no
  `gil_scoped_release`), so an in-process thread cannot kill the peer mid-run.
- Architecture is inconsistent on the count: §11 says "10,000 lockstep frames"; §16, ADR-0007, and PLAN.md say
  "10,000 exchanges". At the default divisor 10 that is 10 s versus 100 s of sim time.
- `astra_sim_to_sw_v1` fields with no filling utility or definition today: `cas` (not in `util/air_data.hpp`),
  `ax/ay/az` (specific force vs kinematic, unstated), `lat/lon` (undefined for flat Earth), `status_flags`
  (no bit meanings). `astra_sw_to_sim_v1.throttle[2]` versus the single shared `controls.throttle` input
  (ADR-0017 default for two turbofans).
- No wall-clock symbol scan exists: §15's determinism guard is prose only (grep of `tests/`, `scripts/`, `cmake/`,
  `.github/` finds none). CI is `GATE=local`, so ADR-0008's "CI builds embedded standalone" must be a `just check` recipe.
- `Simulation` has no exchange recorder; `astra.linearize`'s 9-state `A` has no altitude state (PLAN.md:540,
  lesson 2026-09-22); the PR 8 test builds a 5x5 augmentation (`tests/python/test_cruise_phugoid.py`).
- MIL-F-8785C Fig. 7 (p.49) at 32,800 ft, Joe's chart read (Q7): `cruise.yaml`'s `sigma_high: 3.0` m/s is **not** light;
  it sits between the 10^-4 curve and SEVERE. Its `# light` comment is wrong. My earlier low-resolution read
  reached the same direction but is superseded by his.
- PLAN.md lines 600 and 605 are byte-identical "§15 envelope monitors" bullets; **remove one** in the PR 7 closeout.
  PLAN.md:527 (deflection range) is narrowed to "the actuator model (M4+)" and is decided by Q9.

## Contract and failure behavior

**Exchange stage.** At `frame % transport.divisor == 0` (default 10), for the bound vehicle (v1: index 0), after
the frame's environment sample and before `vehicle.step`: pack truth into `astra_sim_to_sw_v1` (zero-initialized
first; padding and `reserved` are zero so recorded bytes are deterministic), publish, block for the reply up to the
timeout, validate (magic, version, echoed `frame`), write the command map into the vehicle's `InputRegistry`
(pending), then the existing `latch()` commits it the same frame. Between exchanges commands hold (ZOH, §10).
Commands computed from frame-k state are therefore applied from frame k with zero compute delay (§5); disclosed.

**Transport failure is unconditionally terminal** (§15, ADR-0007 Consequences; critique F-4): timeout, wrong
or unexpected ack value, echoed-frame mismatch, bad magic/version, frame-counter discontinuity, peer closed or
gone, sequence range exhausted. `HaltReason{kTransport, source = transport plugin path, frame, vehicle_index}`; the
clock does not advance; nothing further steps. There is **no** `warn`/`off` action for this check (a run that
continues past a missed lockstep exchange is no longer meaningful, and would let wall-time expiry change the
trajectory); `sim.health.transport` is not introduced. A non-finite command value keeps using ADR-0017's
`health.input` (naming the input). Handshake failure before frame 0 is an init error naming `transport`.

**Receive state machine** (critique F-1). After publishing request `frame` the sim waits on `ack_seq`. Unchanged
`ack_seq` (including a spurious wakeup or a re-`FUTEX_WAKE` with no store) means *still waiting*, never a
failure. `ack_seq == frame` completes the wait; the reply payload is then validated (magic, version, `reply.frame
== frame`). Any other value is a protocol failure: `ack_seq` moved to something that is neither the previous value
nor `frame` (out-of-order), or `reply.frame != frame` (a stale payload behind a fresh sequence word). A peer
that re-stores the same sequence value is undetectable by sequence words alone and is **not claimed detectable**;
R5 covers only the cases above.

**Handshake, readiness, and shutdown** (critique F-2, F-5), to be pinned in the PR 1 ADR: the sim creates the
segment `O_EXCL`, sizes it, initializes the whole header (both sequence words to `UINT32_MAX`, `sim_pid`), then
publishes `state = SIM_READY` with release. The peer attaches (retrying `open` until the handshake timeout, so
either start order works), validates magic, version, and layout size with acquire, then publishes `PEER_READY`;
the sim waits for it (handshake timeout) before frame 0. No frame payload is read or written before both
readiness states are visible. On normal completion the sim publishes `state = CLOSED` and wakes the peer; the HAL
maps `CLOSED` to a clean exit (status 0), never to peer death. The creator unlinks on close; the driver also
unlinks its own uniquely named segment (name from the driver, `astra_<driver pid>_<nonce>`, passed through config)
in a `finally`, terminates then kills and reaps the autopilot with a bounded wait on every exit path, and a
fixture removes leftovers whose creator pid is dead. There is no "unlink on next start" (nothing safely
identifies old segments).

**Deadline and spurious wakeups** (critique F-3). One immutable absolute deadline (`CLOCK_MONOTONIC`, Q2) is
computed when the wait begins; every re-wait after a spurious wakeup or `EINTR` uses the time remaining to that
same deadline, never a fresh timeout. Peer death is detected no later than that deadline. Two separate
measurements in the tests: wait duration (must not exceed the timeout by more than wake latency) and kill-to-halt.

**Sequence range** (critique F-6). The exchange stage checks `frame < UINT32_MAX` at every publication and halts
`kTransport` otherwise, independent of `sim.frames`, so repeated `run()`/`step()` calls cannot reach the
sentinel; a boundary test steps a clock preset near the limit.

**Config** (proposed schema; unknown keys are errors): `transport: { plugin: plugins/sim/transport/<loopback|shm>,
divisor, timeout_ms, handshake_timeout_ms, commands: { elevator: controls.elevator, aileron: ..., rudder: ...,
throttle: [controls.throttle, controls.throttle] }, limits: {...} }` (limits per Q9). Exact spelling is a PR 3 detail
under the ADR.

**Determinism.** Lockstep makes results independent of scheduling. Wall time appears only in the timeout, whose
expiry is a terminal halt (no continuation, so it can never change a trajectory). The autopilot reads no clock;
`hal_now` derives from the frame. **Both producers are fully initialized** (critique F-9): the sim zeroes its
request struct before packing; the HAL/autopilot zeroes its reply struct before filling it, so padding, `mode_flags`,
and `fault_flags` are deterministic. M4 autopilot semantics: `mode_flags` carries its mode word (a constant),
`fault_flags` is 0; the sim records but does not act on either in M4 (Q16). Recorder serialization: a fixed header
(magic, version, stream counts, record sizes), then each stream as fixed-size native-endian records, so the byte
comparison covers counts and layout, not only payloads.

## Acceptance

| ID | Observable requirement (M4 clause) | Executable check and independent expected result |
|---|---|---|
| R1 | 10,000 exchanges, zero missed: sim counters `exchanges == 10000`, `missed == 0`; every reply echoes its request frame; all magic/version valid. The autopilot's own exit summary independently reports 10,000 handled. | Python driver test reads both counters and the recorder. Two independent counters, one per process. |
| R2a | **At each of the two gating turbulence levels (Q7: light, moderate), each a separate run and test:** altitude within ±30 m of the initial 10,000 m at every major frame, including the final state. **Checked against the sim truth state recorded by the sim (rigid-body block, `p_n`/`q_nb`), not the ICD fields PR 2 packs**; altitude is `p_n[2]` for `wgs84_rotating`. The recorder's state stream is per major frame (about 10 MB for 100k frames), a superset of the per-exchange samples you asked for, because per-exchange sampling leaves the nine frames between exchanges and the tail unchecked (critique F-8). | Driver test over the recorder's state stream, test-side arithmetic independent of `core/` packing. |
| R2b | At both levels: heading within ±2° of the commanded 0°, angle-wrapped, at every major frame incl. the final state; ψ derived test-side from recorded `q_nb` (ADR-0001, 3-2-1). | Same. |
| R2c | Discrimination: the same run with an impaired autopilot (zero gains / constant trim output) is reported. If open loop also passes both bands, the bands are recorded non-discriminating (lesson 2026-09-19). | Driver test; informational unless Joe makes it a gate. Both turbulence levels gate R2a/R2b (Q7); the impaired-autopilot run is on the light config. |
| R3 | Bit-identical across two runs: byte-equal recorder (sim-to-sw stream, sw-to-sim stream, state stream) and final state bytes; SHA-256 recorded. Still equal with injected autopilot scheduling jitter; a different Dryden seed makes them differ (the comparison is live). | Fresh-process runs, same build/machine. |
| R4 | Peer death: autopilot SIGKILLed (and separately `abort()` via a test flag) after exchange N gives `HaltReason{kTransport, ...}` with kill-to-halt no greater than the configured timeout plus a wake/measurement allowance (Q3), measured from the kill (or from the autopilot's last reply for the `abort()` flag), and separately the sim's wait duration no greater than the timeout plus the same allowance; no hang (hard test timeout); clock frame equals halt frame; state finite; commands from before death not applied after halt. A repeated-wakeup test (peer issues `FUTEX_WAKE` without storing) proves the deadline does not restart. | Driver test plus shm unit test. |
| R4b | Sim death: autopilot exits nonzero within its HAL timeout; no orphan. | Driver test. |
| R5 | Per the receive state machine: an out-of-order `ack_seq`, a wrong echoed `reply.frame`, bad magic/version, a late ack, a peer `CLOSED`, and sequence-range exhaustion (including via repeated `run()`/`step()`) each halt naming the transport; an unchanged `ack_seq` or a spurious wakeup keeps waiting. A re-stored identical sequence value is **not claimed detectable** (critique F-1). NaN command halts through `health.input` naming the input. Startup: delayed initialization and either start order (autopilot first or sim first) reach handshake; normal completion exits the autopilot with status 0; startup failure, driver death, and a stale segment each leave no orphan process or segment (critique F-2, F-5). | Unit tests on loopback and shm; driver tests for lifecycle. |
| R6 | Zero heap allocations over 1000 frames including exchanges. | Extend `tests/physics/no_alloc_test.cpp`. |
| R7 | ZOH: latched command values are constant across the 9 frames between exchanges; a new command is committed on the frame it arrives. | Unit test on latched values. |
| R8 | No `transport` block: bit-identical to today (existing goldens, including the M3 frozen regression, pass unmodified). Unknown keys or unknown input names: `ConfigError` naming the path. | Existing suites plus config tests. |
| R9 | `embedded/` configures and builds standalone with only a C++17 compiler; links only `icd/` and `embedded/hal/`; the autopilot binary imports no `astra_core` symbol. | New `just` recipe in `check`; `nm -D` test. |
| R10 | HAL: `hal_wait_frame`, `hal_send`, `hal_now` (frame-derived, no clock), wait with timeout that reports expiry. | HAL unit tests. |
| R11 | `icd/astra_icd.h` layout untouched (comment-only edits at most); static asserts hold under clang-18 and `gcc -std=c11`. | Existing `tests/icd`. |
| R12a | Each packed `sim_to_sw` field has a stated definition and a hand-computed oracle test (`cas`, accelerations, lat/lon, flat-Earth defined value plus status flag, never garbage). | PR 2 unit tests. |
| R12b | **ICD-vs-state agreement, a separate check from R2:** the recorded ICD `phi/theta/psi/altitude/lat/lon` (and `u/v/w`, `p/q/r`) agree, at each exchange frame, with the same quantities derived independently from the recorded state stream within a stated tolerance (proposal 1e-12 absolute, since it is the same arithmetic; PR 2 justifies it). A packing bug therefore cannot pass its own acceptance, and R2 cannot be satisfied by a wrong pack. | Driver test, separate test function and failure message. |

Performance: exchange path allocation-free (R6). Runtime budget is in "Validation and risks".

## Proposed PR decomposition (dependency order, one writer per PR)

| PR | Scope | Tier | Bound by | Acceptance commands |
|---|---|---|---|---|
| 1 | **Decision records and architecture edits.** (a) ADR-0022 (proposed number): exchange stage owned by `Simulation`, config-declared command map, sequence-word sentinel, readiness-state handshake, normal shutdown and segment ownership/cleanup, receive state machine, single-deadline timeout, unconditional terminal failure (no warn/off), exchange recorder (three streams, defined serialization) as M4's "log". (b) **ADR-0024, the ADR-0007 amendment**, as a superseding record (accepted ADRs are immutable; Joe's decision, 2026-09-24: its own number, not folded into ADR-0022): ADR-0007's Consequence line 39, "mirrored byte-for-byte in the HAL", is replaced by one shared layout header in `embedded/hal/` consumed by both the shm plugin and the HAL (Q4); also records the sentinel/handshake protocol refinements. (c) ADR-0023 (proposed): narrow monotonic-clock exception to ADR-0010 for the transport timeout (Q2). (d) Architecture §5/§10/§11/§15/§16 edits, incl. correcting §11's "frames" (Q1) and naming both gating turbulence levels, light and moderate, in §16's M4 row (Q7). ADR files need Joe's authorization. | 3 (docs; policy) | 0006, 0007, 0008, 0009, 0010, 0011, 0017 | A1 |
| 2 | ICD truth packing. **Not a pure function of vehicle state alone** (critique F-7): inputs are the frame-start rigid-body state, the frame's `EnvironmentSample` (air data: alpha, beta, tas, mach, `cas`, all available before the exchange), the Earth model (altitude, lat/lon), and, for `ax/ay/az`, the **previous frame's published `wrench_total` and mass** (allocation-free, already registered as `v<i>.wrench_total.*`); current-frame wrenches do not exist until `integrate()`, and `Vehicle::derivative()` allocates and throws, so it is not used. Consequence: specific force lags one frame and is invalid at frame 0 (status flag; Q11). Oracle unit tests; header comment clarifications only | 3 (`icd/` semantics, numerical util) | 0001, 0002, 0006 | A2 |
| 3 | Core seam: `Transport` interface, `Simulation` exchange stage and command map, receive state machine, sequence-range check at publication, exchange recorder (sim-to-sw, sw-to-sim, per-major-frame rigid-body state), `HealthCheck::kTransport`, counters/signals (no action key), `plugins/sim/transport/loopback`, `kCoreApiVersion` 8, Python enum | 3 (kernel, transport, health) | 0006, 0007, 0009, 0011, 0016, 0017, 0022, 0023 | A3 |
| 4 | `plugins/sim/transport/shm` and `embedded/hal` shm backend on the shared layout header; header state word (readiness, CLOSED), futex protocol with acquire/release ordering and one-deadline waits; two-process echo-peer tests; measures real exchange cost (see runtime) | 3 (concurrency) | 0006, 0007, 0024, 0008, 0009 | A4 |
| 5 | `embedded/autopilot` (four PID loops, C++17), standalone-build `just` recipe wired into `check`, no-`astra_core`-symbol test, scripted-frame unit tests, **gain design per Q13** | 2 | 0006, 0008 | A5 |
| 6 | `cruise_light_turbulence.yaml` and `cruise_moderate_turbulence.yaml` (Q7 values, correct labels, and the one-line `cruise.yaml` note; `cruise.yaml` itself untouched); Python driver `astra.lockstep` (subprocess lifecycle, bounded reap, segment cleanup) and the M4 acceptance tests R1 to R4b, R12b; GIL release on `run` if the SIGKILL-from-thread test needs it (else a killer subprocess) | 3 | 0007, 0008, 0010 | A6 |
| 7 | M4 closeout: PLAN.md check-off, remove the duplicate envelope-monitor bullet, follow-ups, STATE | 0 to 1 | none | A1 |

Acceptance commands (critique F-10; all from the worktree root, `--no-tests=error` so a selector that matches
nothing fails; a test added by a PR is listed there by name, and the gate picks it up because `ctest` and
`pytest` run every registered test):

- **A1** `python3 scripts/docs_check.py && git diff --check`; Codex review of the diff.
- **A2** `cmake --build --preset asan --parallel && ctest --preset asan -R IcdPack --no-tests=error`, then `./scripts/check-full.sh`.
- **A3** `ctest --preset asan -R '(Transport|Loopback|NoAlloc|SimulationTransport)' --no-tests=error` (all four presets in the gate), `./scripts/check-full.sh`, and the existing M3 regression tests passing unmodified.
- **A4** `ctest --preset asan -R '(ShmTransport|HalShm|EchoPeer)' --no-tests=error`; the Release-only 1e6-exchange echo stress test as `ctest --preset release -R EchoPeerStress --no-tests=error` (label `stress`, in the gate for release only); `./scripts/check-full.sh`.
- **A5** `just embedded-standalone` (new recipe, added to `check`); `ctest --preset asan -R '(Autopilot|EmbeddedBoundary)' --no-tests=error`; `.venv/bin/pytest tests/python/test_autopilot_design.py`; `./scripts/check-full.sh`.
- **A6** `.venv/bin/pytest tests/python/test_lockstep.py`; `ctest --preset asan -R ClosedLoopAcceptance --no-tests=error`; `./scripts/check-full.sh` with the log retained under `docs/reviews/evidence/`.

Tier 3 PRs 3 and 4 get their own plan critique (exchange-stage ownership; futex ordering). This document's critique covers PR 1.

## Open questions for Joe

1. **Count.** "10,000 exchanges" (100,000 frames, 100 s at divisor 10) or 10,000 frames (§11)? Recommend exchanges; §11 is corrected in PR 1.
   **Decision (Joe, 2026-09-24):** exchanges (100,000 frames); §11 corrected in PR 1.
2. **Wall clock.** A futex wait with spurious-wakeup handling needs one `CLOCK_MONOTONIC` read for the remaining time. ADR-0010/AGENTS.md restrict wall-clock access to the `RealTime` pacer. Recommend a narrow, ADR-recorded exception (transport timeout only; never feeds sim state; expiry is a halt, never a value). Alternative: relative-timeout loop with a bounded retry count and no clock read (looser bound).
   **Decision (Joe, 2026-09-24):** narrow ADR-recorded monotonic-clock exception, transport timeout only.
3. **Timeout values.** Recommend `transport.timeout_ms` default 1000 (sanitizer-safe), tests use 500; `handshake_timeout_ms` separate, default 5000. "Within one timeout" is read strictly: kill-to-halt no greater than the timeout, plus only a wake/measurement allowance (proposal: the greater of 20 ms and 10% of the timeout), never a whole extra period. Tests use timeout 500 ms so the allowance is small relative to it. Any looser bound is a weakened acceptance and yours to approve.
   **Decision (Joe, 2026-09-24):** as recommended.
4. **HAL boundary and ADR-0007 amendment.** ADR-0007 line 39 says the protocol is "mirrored byte-for-byte in the HAL". Recommend one shared layout header in `embedded/hal/` consumed by the shm plugin, which is a **superseding amendment to ADR-0007** and is in PR 1's scope (item b), not just a preference. Alternative: keep mirrored copies plus a layout-equality test, no amendment. HAL additions either way: wait-with-timeout return, init/close; `hal_now` frame-derived.
   **Decision (Joe, 2026-09-24):** single shared header, as an ADR-0007 superseding amendment in PR 1.
5. **Process lifecycle.** Recommend the sim creates the segment `O_EXCL` (name `astra_<driver pid>_<nonce>`, supplied by the driver through config) and unlinks on close; there is no unlink-on-next-start (nothing safely identifies old segments); the Python driver also unlinks its own segment in a `finally`, launches and reaps the autopilot (zombies defeat `kill(pid,0)`, so the timeout, not pid probing, is the detector); the autopilot exits when the sim dies (HAL timeout); test fixture removes leftover segments whose creator pid is dead.
   **Decision (Joe, 2026-09-24):** closed as recommended.
6. **Sequence words.** ADR-0007's `req_seq = frame` collides with a zero-initialized word at frame 0. Recommend initializing both words to `UINT32_MAX` (sentinel), checking `frame < UINT32_MAX` at every publication (halting `kTransport` otherwise), not only `sim.frames` at init, because repeated `run()`/`step()` bypass an init check (critique F-6); wrap-safe compares. §15's "one round-trip before frame 0": recommend a header-only handshake, not a probe frame (a probe would duplicate frame 0).
   **Decision (Joe, 2026-09-24):** closed as recommended.
7. **"Light Dryden" as config.** `cruise.yaml`'s `sigma_high: 3.0` m/s, `w20_knots: 15` is labeled light. Needs your chart read (human-owned).
   **Decision (Joe, 2026-09-24):** chart read from MIL-F-8785C Fig. 7 (p.49) at 32,800 ft (10,000 m). **The reading is Joe's, not mine.** Both levels gate M4 (this supersedes the earlier "moderate non-gating"):

   Full chart read, Joe, 2026-09-24, Fig. 7 at 32,800 ft, each value about ±0.5 ft/s:

   | Curve / level | Fig. 7 value | m/s | Used as |
   |---|---|---|---|
   | 10^-1 exceedance | 0 (zero above about 9 kft) | 0 | not usable |
   | LIGHT line | 0 (zero above about 30 kft) | 0 | not usable at this altitude |
   | 10^-2 | about 0.5 ft/s | 0.15 | **Light**: `sigma_high` 0.15, `w20_knots` 15 |
   | MODERATE line | about 5 ft/s | 1.5 | **Moderate**: `sigma_high` 1.5, `w20_knots` 30 |
   | 10^-3 | about 5.3 ft/s | 1.6 | reference only |
   | 10^-4 | about 8 ft/s | 2.5 | reference only |
   | SEVERE line | about 17 ft/s | 5.1 | reference only |
   | (current `cruise.yaml`) | about 9.8 ft/s | 3.0 | between 10^-4 and SEVERE; not light |

   Both levels gate M4 (this supersedes the earlier "moderate non-gating"). The `m/s` column is Joe's; my check of the two used values: 0.5 ft/s = 0.152 m/s, 5 ft/s = 1.524 m/s, and 3.0 m/s = 9.84 ft/s.
   Consequences: (1) §16's M4 row is reworded in PR 1 to name both levels. (2) PR 6 adds `cruise_light_turbulence.yaml` and `cruise_moderate_turbulence.yaml` (seed 42) with the correct labels, each carrying a one-line note that `cruise.yaml`'s `sigma_high` 3.0 m/s lies between 10^-4 and SEVERE at 10 km despite its `# light` comment. **`cruise.yaml` is left untouched** (no relabel; the M3 frozen regression depends on its Dryden wind, and the blob recorded in `tests/golden/notional_transport/cruise_regression_manifest.md` stays current). (3) The non-gating 8-seed sweep (Q15) runs on the light config.
8. **Which log satisfies "bit-identical" before M5's HDF5.** Recommend a preallocated in-memory exchange recorder with three streams (sim-to-sw frames, sw-to-sim frames, rigid-body state per major frame, per R2a), plus final state bytes, dumped raw after the run. M5's HDF5 supersedes it. The state stream is what R2 checks; the ICD stream is what R12b cross-checks.
   **Decision (Joe, 2026-09-24):** closed as recommended.
9. **Actuators and deflection range (PLAN.md:527).** Recommend no dynamic `ActuatorModel` in M4 (ideal, commanded = achieved) plus config-declared per-command limits that **halt** on violation (no clamping, which would hide autopilot faults). Alternative: defer entirely. Decide before PR 3.
   **Decision (Joe, 2026-09-24):** halt on violation, no clamp.
10. **`throttle[2]`.** The ICD has two; ADR-0017 shares one `controls.throttle`. Recommend the command map accepts both and requires equality, and a mismatch halts (no warn), rather than renaming inputs across turbofan/trim/linearize in M4.
   **Decision (Joe, 2026-09-24):** closed as recommended.
11. **ICD fields.** Recommend `ax/ay/az` = specific force (what an IMU reports), taken from the previous frame's `wrench_total`/mass (one-frame lag, invalid at frame 0 with a status flag) because current-frame wrenches do not exist at exchange time and the derivative probe allocates; the alternative is a new allocation-free evaluation seam in `Vehicle`, a larger PR 2/3 scope; `cas` from compressible-flow relation with ISA sea-level reference; flat-Earth `lat/lon` = 0 plus a `status_flags` bit; header comments only.
   **Decision (Joe, 2026-09-24):** as recommended.
12. **Autopilot trim biases.** Recommend explicit CLI args from `astra.trim` output, integrators initialized at the bias, so the frame-0 reply equals trim and there is no startup transient.
   **Decision (Joe, 2026-09-24):** as recommended.
13. **PR 5 gain design (new).** `astra.linearize`'s 9-state `A` has no altitude state (PLAN.md:540; lesson 2026-09-22: the phugoid period is 15% off without it), so altitude-hold gains cannot be designed on `A` as shipped. Options: (a) build the 5x5 augmentation (PR 8's approach: PR 7's `A` plus an altitude column and the h_dot row, S&L Example 3.8-5) inside PR 5's own design work; (b) add an opt-in altitude state to `astra.linearize`, a D11 revision, **your call**. **Recommend (a).** Reasons: it keeps an M3 deliverable's API unchanged and stays inside PR 5's Tier 2 scope; the design model is not the acceptance oracle (the nonlinear closed-loop run in R2 is); PR 8 already proved the construction to 0.5% in period. Implementation shape: factor the 5x5 builder into one small helper module under `tests/python/` used by a new design-evidence test (PR 8's test untouched, so no edit to an oracle-adjacent file). If a second consumer appears (M5 Monte Carlo, scenario checks), promote it to a D11 revision then. Heading and roll loops are lateral and need no altitude state.
   **Decision (Joe, 2026-09-24):** (a), the augmentation inside PR 5's design work; `astra.linearize` unchanged.
14. **Validation gap to accept.** ASan cannot see futex memory-order bugs, and TSan cannot follow cross-process futex synchronization. Mitigation: a 1e6-exchange echo stress test (Release), review focused on acquire/release pairing and single-writer-per-word discipline.
   **Decision (Joe, 2026-09-24):** closed as an accepted gap (1e6-exchange stress test plus acquire/release review).
15. **Seeds and gating for R2.** Recommend the gate is the config seed (42) with margin reported, plus a non-gating 8-seed sweep as evidence (Release, about 10 s). One seed alone proves little; you may prefer to gate on the sweep.
   **Decision (Joe, 2026-09-24):** as recommended (config seed gates; sweep non-gating).
16. **`mode_flags` / `fault_flags` semantics (from critique F-9).** Recommend M4 records both and acts on neither (autopilot sends a constant mode word and `fault_flags = 0`), with the recorder capturing all 64 reply bytes. Alternative: a nonzero `fault_flags` is an autopilot-declared fault that halts the sim; that is a new health behavior and would need its own requirement.
   **Decision (Joe, 2026-09-24):** as recommended.
17. **Plan changes beyond your four (from the critique).** (a) transport failure is terminal, with no `warn`/`off` action (contradicts nothing you wrote; ADR-0007 already says halt); (b) R2 checks every major frame rather than every exchange, a superset of your per-exchange instruction; (c) the "within one timeout" slack is now strict (Q3); (d) `ax/ay/az` lag one frame (Q11). Say if you want any of these reversed.
   **Decision (Joe, 2026-09-24):** (a) to (d) accepted.

## Validation and risks

**Gates.** `just check` is the definition of done for PRs 2 to 6; `check-fast.sh` for iteration. No new dependency (futex through `syscall`). No golden, tolerance, or accepted-ADR edits; new ADRs are Joe-authorized.

**Runtime estimate for the M4 acceptance tests** (extrapolated from PR 8's retained evidence,
`docs/reviews/evidence/46b9aca-full-gate.log`, one 300,000-frame open-loop run per test, measured under
`ctest --parallel 32`, so per-frame cost may be slightly inflated by contention):

| Preset | PR 8: 300k frames | per frame | 100k frames (sim only) |
|---|---|---|---|
| release | 0.38 s | 1.3 us | 0.13 s |
| debug | 38.7 s | 0.129 ms | 12.9 s |
| static-plugins | 82.8 s | 0.276 ms | 27.6 s |
| asan | 176.7 s | 0.589 ms | 58.9 s |

Adding the lockstep: 10,000 cross-process round trips. Assumption, **unmeasured** (PR 4 measures it):
20 to 100 us each in Release, 2 to 3x that under debug/ASan, so about 0.2 to 1 s (Release) and 1 to 3 s (ASan)
per 100k-frame run. The autopilot side is negligible. The recorder is a memory write. Estimated per run:
release about 1 to 2 s, debug about 15 s, static-plugins about 30 s, asan about 62 to 65 s.

The full acceptance set is 6 runs of 100k frames: R1/R2 at light (run A), R3's second run (B), a jitter run (C), a
different-seed run (D), the impaired-autopilot run for R2c (E), and R1/R2 at moderate (F, gating per Q7). The kill tests are short
(about the 500 ms test timeout plus startup, about 1 s each, 4 to 6 of them incl. lifecycle cases). The non-gating 8-seed sweep
adds 8 Release runs (about 10 s). Serial totals for all six: release about 7 to 12 s, debug about 90 s,
static-plugins about 180 s, asan about 6.3 min (the placement below runs only run A full-length on asan).

**Proposed placement** (Joe's call):
- `pytest` runs against `build/python`, which is Release, so the **full 100k-frame R1 to R4b, R12b gate runs
  there**: about 10 to 20 s added to the gate, including the seed sweep if gated.
- ctest, all four presets: the echo-peer 10,000-exchange test, protocol-error and kill tests (R4 with the
  `abort()` flag), R5 to R7, and a **shortened closed-loop with the real autopilot at 1,000 exchanges
  (10,000 frames)** plus a shortened R3 pair: about 0.5 s (release), 1.5 s (debug), 3 s (static-plugins), 6 s
  each (asan). About +15 s on asan, negligible elsewhere.
- One **full-length closed-loop run on asan only**, as its own ctest test labelled `acceptance`: about 65 s.
  Because ctest runs it in parallel with PR 8's 348 s serial regression test, it adds about no wall time to
  the asan phase, and it exercises the shm plugin and HAL under the sanitizer with real load. Not on debug or
  static-plugins (their distinct value, unoptimized code and the no-`dlopen` transport build, is covered by the
  shortened run).
- Split the acceptance into separate tests, not one long test (PR 8's single serial two-run test took 348 s on
  asan), so `--parallel` overlaps them.
- Known environment limit: LeakSanitizer under ptrace (lesson 2026-09-17) can block the asan phase in agent
  sessions; run the unchanged gate in a normal terminal before claiming it.

**Risks.** Startup transient from the frame-0 Dryden gust; gain margin against a phugoid with zeta about 0.027
(Q13 design work); stale `/dev/shm` segments after failed runs (fixture cleanup); `run` holding the GIL; a
band that open loop already satisfies (R2c reports it); exchange cost unmeasured until PR 4. **Rollback:** each
PR is additive behind an absent `transport` block (R8), so reverting a PR restores today's behavior.

## Plan critique and decisions

One fresh, read-only Codex critique (`codex-companion.mjs task --fresh`, companion 1.0.6, no `--write`; exit 0;
raw output is in the session scratchpad and not retained in the repository). Ten findings, none rejected. I
checked each against the repository before accepting.

| ID | Sev | Verdict | Evidence and resolution |
|---|---|---|---|
| F-1 duplicate-ack detection unsupported by persistent sequence words | Major | Accepted | ADR-0007 line 15's words persist, so a re-stored value is indistinguishable from an unchanged one. Receive state machine added; R5 narrowed and states what is *not* claimed detectable. |
| F-2 no initialization/publication contract | Major | Accepted | Header `state` word, sim-owned init, `SIM_READY`/`PEER_READY`, retry-until-handshake-timeout, tests for delayed init and both start orders (PR 1 ADR, PR 4, R5). |
| F-3 R4 could pass a violating timeout | Major | Accepted | My own 250 ms slack on a 200 ms timeout allowed 2.25 periods. Single immutable deadline, kill-to-halt and wait duration measured separately, repeated-wakeup test, allowance is now 20 ms / 10%, timeout in tests 500 ms. Any looser bound is flagged to you (Q3). |
| F-4 warn/off contradicts mandatory halt | Major | Accepted | ADR-0007 Consequences require halt; continuing would also let wall-time expiry change a trajectory. `sim.health.transport` dropped; failure is unconditionally terminal. |
| F-5 lifecycle: shutdown, cleanup, stale segments | Major | Accepted | `CLOSED` state, creator unlink plus driver `finally`, bounded terminate/kill/reap, dead-creator fixture cleanup; "unlink on next start" removed (nothing safely identifies old segments). Lifecycle tests added to R5. |
| F-6 sentinel bound bypassable via `run()`/`step()` | Major | Accepted | `Simulation::run(N)` (`simulation.cpp:644`) accepts arbitrary counts. Range check moved to every publication; boundary test added. |
| F-7 PR 2 "pure function of state" is false | Major | Accepted | Specific force needs wrench and mass, produced in `integrate()` after the exchange; `Vehicle::derivative()` allocates and throws. PR 2 inputs restated (env sample, Earth model, previous frame's `wrench_total`); one-frame lag and frame-0 invalid flag; alternative seam listed in Q11. |
| F-8 R2 samples leave intervening frames unchecked | Moderate | Accepted | Recorder state stream is per major frame (about 10 MB), including the final state. This exceeds your per-exchange instruction; flagged in Q17. |
| F-9 determinism specified for one producer | Moderate | Accepted | Reply struct zeroed by the HAL side; `mode_flags`/`fault_flags` semantics (Q16); recorder serialization with counts and record sizes defined. |
| F-10 acceptance commands vague or missing | Moderate | Accepted | Exact commands A1 to A6 with presets and `--no-tests=error`; stress and design-evidence tests named. `ctest -R autopilot` alone could match nothing. |

Not covered by this critique: the ADR text itself (PR 1 gets its own Codex review) and the exchange-cost
measurement (PR 4).

Plan approval: Joe, 2026-09-24, with the decisions in "Open questions".

## Done when

R1 to R12b proved by the commands above; `just check` green on the final candidate of PRs 2 to 6 with
retained evidence; independent Codex review (adversarial for PRs 3 and 4); Joe's human review packets; M4
checked off in PR 7. Implementation completion is distinct from human approval, merge, and release.
