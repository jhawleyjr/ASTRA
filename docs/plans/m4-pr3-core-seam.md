# Implementation plan: M4 PR 3, core seam (exchange stage, `Transport`, loopback)

Spec/brief: [M4 Lockstep plan](m4-lockstep.md), PR 3 row, sections "Contract and failure behavior",
"Acceptance" (R5 to R8) and A3; [ADR-0022](../decisions/0022-lockstep-exchange-stage.md),
[ADR-0023](../decisions/0023-transport-timeout-clock-exception.md),
[ADR-0024](../decisions/0024-shm-layout-header-and-protocol.md) (all accepted 2026-09-24).
Risk: Tier 3 (kernel per-frame order, a new plugin-facing interface, health/halt behavior).
Status: approved
Plan approval: **approved by Joe, 2026-09-25, with one addition** (all-halts-sticky, below; decisions
Q-A to Q-D recorded under "Questions for Joe"). The M4 plan itself was approved 2026-09-24; this
document refines PR 3 under it and ADR-0022.
Base: `origin/main` = `794158d` (#52's merge), branch `worktree-m4-pr3-core-seam`.

## Outcome and non-goals

**Outcome.** A config with a `transport` block drives a lockstep exchange for vehicle 0 at
`frame % divisor == 0`, after the environment sample and before `vehicle.step()`, through a new
`Transport` interface implemented by `plugins/sim/transport/loopback`; commands pass ADR-0022's
validation order and are staged through `InputRegistry` handles so the same frame's `latch()`
commits them; every failure is a terminal `HealthCheck::kTransport` halt; an in-memory exchange
recorder captures the three streams. A config without `transport` behaves bit-identically (R8).

**Non-goals (PRs 4 to 6).** No shm, no `embedded/`, no HAL, no autopilot, no futex, no clock
read anywhere (the loopback reads none, ADR-0023), no Python driver or recorder binding, no
architecture/ADR edits, no golden or tolerance edits. `ActuatorModel` stays open.

**Invariants.** Per-frame path allocation-free and exception-free (exchange, validation, writes,
recording). ADR-0010 time. No `std::string` built per frame. M3's frozen regression and every
existing test pass unmodified.

## Repository findings (observed at `794158d`)

- **`kCoreApiVersion` is already 8** (`core/include/astra/core_api.hpp.in`, bumped by M3 PR 4a,
  ADR-0021). ADR-0022 and the M4 plan say "7 to 8"; both were written against a stale number.
  **Proposal:** bump 8 to 9 for the new `Transport` interface (Q-A).
- `Simulation::step()` (`core/src/kernel/simulation.cpp:562`): environment scheduler, then per
  vehicle {environment sample into a local `EnvironmentSample sample`, `set_environment`,
  `vehicle.step`, `latch_vehicle_inputs`, `integrate`, atmosphere domain check}, then
  `clock_.advance()`. The exchange slots between `set_environment(sample)` and `vehicle.step(ctx)`.
- `PluginDescriptor::create` returns `unique_ptr<Model>`; every typed slot (`EarthModel`,
  `WindModel`, ...) is a `Model` subclass reached by `dynamic_cast` after `loader.load()`. A
  `Transport : public Model` follows that pattern with no descriptor change (ADR-0022's rejected
  alternative is a transport that *writes through the seam from `step()`*, not a `Model`-derived
  load vehicle; this transport is never scheduled and never calls `step()`).
- `astra::icd_pack::pack_sim_to_sw(out, ctx, vs, env, earth, previous)`
  (`core/include/astra/util/icd_pack.hpp`) needs an `EarthModel&`, so a transport requires a
  `physical` section. `PreviousFrame{wrench_total, mass}` has no source accessor today:
  `Vehicle::published_wrench_` (the k1 wrench, `vehicle.cpp:102`) and
  `RigidBody6DofDynamics::mass_props_` (latched in `begin_frame()`) are private. At the exchange of
  frame k (before frame k's `integrate()`), both still hold frame k-1's values, which is exactly
  what `icd/astra_icd.h` pins for `ax/ay/az`.
- `InputRegistry::find()` returns an `InputHandle` whose `write()` is `noexcept`; `from_config`
  calls `vehicle.init()` (all `bind_inputs()`), then `apply_initial_controls()` (freeze).
- `HealthMonitor::init` registers `health.*` signals unconditionally; adding one there would change
  the signal set of every config. `HealthCheck` is bound in `python/bindings/_core.cpp:344`.
- `VirtualClock` has no frame setter; the sequence-range boundary test needs one (test-only).
- `Simulation` has an implicit move and no destructor; nothing closes anything at teardown today.
- Test fixtures: `tests/plugins/input_consumer` registers only `controls.elevator`;
  `tests/configs/no_alloc_write_seam.yaml` is a flat-Earth, in-domain fixture using it.
  `AllPlugins.EveryRegisteredPluginLoadsAndDescribesItself` loads every plugin (no `init`).
- Architecture §15 (line 529): "Every plugin's descriptor checked: ... ICD version for transports."

## Design

### `Transport` interface (`core/include/astra/interfaces/transport.hpp`, new)

```cpp
enum class AckStatus : std::uint8_t { kObserved, kExpired, kPeerClosed };
struct AckObservation { AckStatus status; std::uint32_t ack_seq; };
struct TransportSettings { std::uint32_t timeout_ms, handshake_timeout_ms, peer_timeout_ms; };
enum class CloseReason : std::uint8_t { kNormal, kAfterFailure };

class Transport : public Model {   // Model only as the plugin load vehicle; never scheduled
 public:
  [[nodiscard]] virtual std::uint32_t icd_version() const noexcept = 0;
  // Init: the header-only handshake (ADR-0024) within settings.handshake_timeout_ms. Throws on
  // failure. Afterwards the ack word holds its initial value, returned here (UINT32_MAX by protocol).
  [[nodiscard]] virtual std::uint32_t open(const TransportSettings& settings) = 0;
  // Per frame: store the request payload, then publish seq (release, then wake in shm).
  virtual void publish(const astra_sim_to_sw_v1& request, std::uint32_t seq) noexcept = 0;
  // Per frame: one wait. Returns the ack word observed after a wake, which MAY equal `unchanged`
  // (spurious wake, EINTR, a wake with no store), or kExpired / kPeerClosed. Every call between
  // two publish() calls shares ONE absolute deadline, fixed at the first such call (ADR-0023:
  // the only clock read, confined to this one function; the loopback reads none).
  [[nodiscard]] virtual AckObservation wait_ack(std::uint32_t unchanged) noexcept = 0;
  // Per frame, only after an observed ack equal to the published seq: copy the reply payload.
  virtual void read_reply(astra_sw_to_sim_v1& reply) noexcept = 0;
  // Once, at teardown. The shm meaning of kAfterFailure is PR 4's under ADR-0024.
  virtual void close(CloseReason reason) noexcept = 0;
};
```

**Ownership split (the question this PR's critique targets).** The *receive state machine*, the
validation, and every halt decision live in core (`LockstepExchange`, below); the transport owns
only the mechanics that need either the peer or the clock: storing/publishing, one bounded wait
against its own deadline, copying the reply. Core's loop:

```text
publish(request, seq)
loop: obs = wait_ack(last_ack)
      kExpired -> halt kTimeout;  kPeerClosed -> halt kPeerClosed
      obs.ack_seq == last_ack -> continue      (still waiting, never failure)
      obs.ack_seq == seq      -> break
      otherwise               -> halt kOutOfOrderAck
read_reply; record; validate; stage writes; last_ack = seq
```

So "unchanged means still waiting" and "moved to neither previous nor frame means failure" are
core logic, testable with the loopback (which can script unchanged observations), and PR 4's shm
plugin implements only the wait. Termination depends on the transport honoring its deadline
(contract above); the loopback's scripted sequences are finite.

### `LockstepExchange` (`core/include/astra/kernel/lockstep_exchange.hpp` + `.cpp`, new)

Owned by `Simulation` through `std::unique_ptr` (stable addresses for its registered counters and
for `HaltReason::source`; the same reason `health_` is a `unique_ptr`). It owns the transport's
`LoadedPlugin`, so its destructor calls `close()` before the plugin is dropped (`kNormal`, or
`kAfterFailure` once a halt occurred). Simulation gets no user-declared destructor.

Per exchange frame (`HaltReason exchange(ctx, vehicle, sample, earth) noexcept`), in this order:

1. **Sticky failure.** A prior transport halt is returned unchanged (see step() below).
2. **Sequence range** (ADR-0022): `ctx.frame >= UINT32_MAX - 1` halts `kSequenceExhausted`.
3. **Frame continuity:** after the first exchange, `ctx.frame != last_frame + divisor` halts
   `kFrameDiscontinuity` (unreachable in production, where the clock only advances by one per
   successful frame; reachable through the test-only clock preset).
4. **Pack** with `pack_sim_to_sw(request_, ctx, VehicleState{vehicle.state().data()}, sample,
   earth, previous_valid ? &previous : nullptr)`, `previous = {vehicle.published_wrench(),
   vehicle.latched_mass()}` (two new const accessors). `previous_valid` is true only when this
   Simulation itself completed frame `ctx.frame - 1` (the stage stores the last frame whose
   post-step hook ran), never inferred from the mass alone: `Vehicle::derivative()` calls
   `begin_frame()` without integrating (`vehicle.cpp:557`), so a probe can latch a real mass beside
   a never-published zero wrench (critique F-6). Frame 0 is therefore always null. Record the
   request.
5. **Publish** with `seq = static_cast<uint32_t>(ctx.frame)`, then the receive loop above.
6. **Read reply** into a zeroed member struct; record it (so the offending reply is in the stream).
7. **Validate** (ADR-0022 order, first failure halts): (1) magic, version, `reply.frame ==
   ctx.frame`; (2) finiteness of `elevator, aileron, rudder, throttle[0], throttle[1]` in that
   order; (3) `throttle[0] == throttle[1]`; (4) each of the five values against its configured
   limit (`lo <= v && v <= hi`, inclusive). All four checks cover **every command field, mapped or
   not** (critique F-2); mapping only decides what is written.
8. **Stage** each mapped field through its `InputHandle::write()` (throttle written once). No write
   happens unless every check passed. Increment `transport.exchanges`; `last_ack = seq`.

A halt is `HaltReason{kTransport, source, ctx.frame, vehicle.index()}` with `source` = the plugin
path (protocol failures) or a static ICD field name (`"astra_sw_to_sim_v1.elevator"`, ...; the
non-finite, throttle and limit failures). A `TransportFault` code (`kTimeout`, `kOutOfOrderAck`,
`kPeerClosed`, `kBadMagic`, `kBadVersion`, `kFrameMismatch`, `kFrameDiscontinuity`,
`kSequenceExhausted`, `kNonFiniteCommand`, `kThrottleMismatch`, `kCommandLimit`,
`kRecorderFull`) is kept for
diagnosis (`Simulation::transport_fault()`, and signal `transport.fault`), so a test can tell a
timeout from an out-of-order ack. `transport.missed` becomes 1 on a failure.

Signals `transport.exchanges`, `transport.missed`, `transport.fault` (unit `"1"`) are registered
only when a transport is configured, so a config without one has exactly today's signal set.
`HealthMonitor` is not changed apart from the enum: there is no action key to read (ADR-0022), so
the stage builds the halt itself.

### `Simulation` changes

- `from_config`: after `apply_initial_controls()`, if `transport` is present: require `physical`
  (else `ConfigError` at `transport`); load the plugin, `dynamic_cast` to `Transport` (else
  `PluginLoadError`), check `icd_version() == ASTRA_ICD_VERSION` (else `PluginLoadError`); call
  its `Model::init(transport_config, registry)` (the loopback reads only `transport.peer`); parse
  the sim-owned keys; resolve the command map with `vehicle.inputs().find()` (unknown name:
  `ConfigError` at `transport.commands.<field>`); register counters; then the existing health
  init, `registry_.freeze()`, and unknown-key check; **then** `open()` last, so a config error
  never starts a handshake. A throw from `open()` becomes `ConfigError` at `transport`.
- `step()`: at the top, `if (exchange_) { if (const HaltReason h = exchange_->pre_frame(ctx); h)
  return h; }`, which returns a prior transport halt unchanged (sticky: no environment step, no
  re-publication, clock unchanged) or the recorder-capacity halt (F-1). Inside the
  vehicle loop, after `set_environment(sample)` and before `vehicle.step(ctx)`, for the bound
  vehicle (index 0) on exchange frames, call the stage and return its halt. After
  `clock_.advance()`, the stage's post-step hook records vehicle 0's state with the new frame and
  notes the completed frame (F-6). All three are behind `exchange_ != nullptr`.
- Accessors: `transport_fault()`, `exchange_recorder()` (const pointer, null without a transport),
  and a test-only `preset_frame_for_testing(std::uint64_t)` (plus `VirtualClock::
  set_frame_for_testing`), the same public-but-named posture as `for_testing`.
- `for_testing` gains nothing (no transport there; tests use `from_config`).

### Config (`transport`, all keys under it; unknown keys are errors)

```yaml
transport:
  plugin: plugins/sim/transport/loopback
  divisor: 10                 # default 10, checked_divisor rules
  timeout_ms: 1000            # default 1000; integer in [1, UINT32_MAX], checked before narrowing
  handshake_timeout_ms: 5000  # default 5000; same range rule
  peer_timeout_ms: 5000       # default 5000 (Q-D); same range rule, then ConfigError at this key
                              # if < timeout_ms (critique F-3: 4294967296 must not narrow to 0)
  commands:                   # ICD field -> vehicle input name; each optional, at least one
    elevator: controls.elevator
    aileron: controls.aileron
    rudder: controls.rudder
    throttle: [controls.throttle, controls.throttle]   # exactly two, identical names
  limits:                     # all four keys required, mapped or not; [lo, hi], finite, lo <= hi
    elevator: [-0.35, 0.35]
    aileron: [-0.35, 0.35]
    rudder: [-0.35, 0.35]
    throttle: [0.0, 1.0]
  peer: { ... }               # read by the loopback plugin only (below)
```

Mapped fields are optional (vehicle-agnostic; the test plugin has only an elevator), but every
validation step, limits included, covers every reply field regardless of mapping, as ADR-0022
words them (critique F-2).

### `plugins/sim/transport/loopback` (new plugin)

In-process, no clock, no thread, allocation-free per frame. Its reply is produced by a config-built
responder (`transport.peer`): constant `commands` (all five ICD values), an optional `schedule` of
`{frame, <field>: value, ...}` changes (for R7), `mode_flags`, and an optional `fault` injection
`{frame, kind, ...}` with kinds `out_of_order_ack`, `stale_reply_frame`, `bad_magic`,
`bad_version`, `timeout` (scripted `kExpired`, a late ack), `peer_closed`, `non_finite`
(`field`, `value: nan|inf`), `throttle_mismatch`, `handshake_fail`; and `spurious_wakeups: N`
(N unchanged observations before every ack). The reply is zeroed before filling (ADR-0022,
Determinism). See Q-B on ADR-0007's "calls an in-process functor".

### Exchange recorder (`core/include/astra/kernel/exchange_recorder.hpp` + `.cpp`, new)

Preallocated at init: `sim.frames / divisor + 1` request and reply records and `sim.frames + 1`
state records. Streams: raw `astra_sim_to_sw_v1` (200 bytes), raw `astra_sw_to_sim_v1` (64 bytes),
state record `{uint64 frame; double y[13]}` (112 bytes): the initial state at frame 0 (recorded at
the end of `from_config`), then vehicle 0's rigid-body state after every successful frame, labeled
with the advanced frame, so the stream is states 0..N including the final one. `serialize()`
(allocates; not per frame) writes a fixed native-endian header: magic `"ASXR"`, recorder version 1,
the three record sizes, the three counts, then the streams. **Overflow halts** (architecture §15:
"Log ring-buffer overflow (`halt`; a silently truncated log is not a log)", critique F-1): at the
top of `step()`, before anything runs, a frame whose records (a state record, plus a request and a
reply on an exchange frame) cannot all fit halts `kTransport` with `TransportFault::kRecorderFull`
and source `"transport.recorder"`, sticky like every transport halt; the recorder never
reallocates. So a `run(N)` past `sim.frames` stops at capacity with a named halt and a complete
log. The Python binding and file dump are PR 6's.

### Smaller pieces

- `HealthCheck::kTransport` appended last (existing values keep their integers); Python
  `.value("TRANSPORT", ...)`.
- `kCoreApiVersion` 8 to 9 with the reason in `core_api.hpp.in` (Q-A).
- `Vehicle::published_wrench()` and `Vehicle::latched_mass()` (via a new
  `RigidBody6DofDynamics::mass_props()`), const, noexcept.
- A test plugin `tests/plugins/wrong_icd_transport` (`NO_LOAD_ALL`) returning ICD version 2, for
  the §15 descriptor check.

## Mapping and sequence

One Claude builder, one worktree, no packages.

| Req | Change / owned paths | Contract | Check |
|---|---|---|---|
| Interface | `core/include/astra/interfaces/transport.hpp`, `core/src/interfaces/transport.cpp` (key function), `core_api.hpp.in` | as above; API 9 | build all presets; `AllPlugins` |
| Stage | `core/include/astra/kernel/lockstep_exchange.hpp`, `core/src/kernel/lockstep_exchange.cpp`, `simulation.{hpp,cpp}`, `virtual_clock.hpp`, `vehicle.hpp`, `rigid_body_6dof_dynamics.hpp`, `health_monitor.hpp` (enum), `core/CMakeLists.txt` | ADR-0022 placement, order, halts | R5, R7 |
| Recorder | `core/include/astra/kernel/exchange_recorder.hpp`, `core/src/kernel/exchange_recorder.cpp` | fixed header, counts, overflow halts (F-1) | `SimulationTransport.Recorder*` |
| Loopback | `plugins/sim/transport/loopback/{CMakeLists.txt,loopback.cpp}` | scripted peer, no clock | `LoopbackTransport.*`, R5 |
| R5 | `tests/unit/kernel/simulation_transport_test.cpp` (suite `SimulationTransport`), `tests/unit/kernel/loopback_transport_test.cpp` (suite `LoopbackTransport`), fixtures `tests/configs/transport_*.yaml`, `tests/plugins/wrong_icd_transport` | each fault halts `kTransport` with the right `TransportFault`, source, frame, clock frame == halt frame; spurious wakeups keep waiting; NaN and inf per field under `sim.health.input` halt, warn, off; validation order (bad magic + NaN reports magic; NaN + mismatch reports NaN; mismatch + limit reports mismatch); no input staged on failure; sticky halt on repeated `step()`/`run()`; sequence range at `UINT32_MAX - 1` (divisor 1, clock preset), including repeated `step()`; config errors name their paths; handshake failure is `ConfigError` at `transport` | A3 |
| R5 additions (critique) | same suites | an out-of-limit **unmapped** field halts naming it (F-2); `timeout_ms`, `handshake_timeout_ms`, `peer_timeout_ms` each rejected at 0 and at 4294967296 with its own path (F-3); repeated `run()` past `sim.frames` halts `kRecorderFull` at exactly the capacity frame with every recorded count complete (F-1); a `derivative()` probe before a preset first exchange still yields `ACCEL_INVALID` (F-6) | A3 |
| R6 | `tests/physics/no_alloc_test.cpp` new `NoAlloc.ZeroHeapAllocationsOverOneThousandFramesWithLoopbackExchanges`, `tests/configs/no_alloc_transport.yaml` (100 exchanges, a schedule change, spurious wakeups, recorder on; asserts 1000 frames, no halt, 100 exchanges); plus `NoAlloc.ZeroHeapAllocationsOnTransportFaultsAndStickyHalts` measuring around a faulting exchange of each source kind (a protocol fault naming the plugin path, a limit fault naming an ICD field), ten repeated `step()` calls on the sticky halt, and the recorder-capacity halt (F-5) | zero allocations on success and failure paths | A3 |
| R7 | `SimulationTransport.ZeroOrderHold*` | latched `v0.controls.elevator` constant over the 9 frames between exchanges; a scheduled change at frame 10 is latched by the end of frame 10 and alters frame 10's published wrench relative to a no-change run | A3 |
| R8 | existing suites untouched; `SimulationTransport.NoTransportBlockRegistersNoTransportSignals`; `SimulationTransport.ConstantTrimRepliesMatchNoTransportRunBitForBit` (loopback replying the frozen `initial_controls` every exchange yields a state byte-identical to the same config without `transport`, over 300 frames with physics, atmosphere, and Dryden). Its claim is limited to **trajectory non-perturbation** (F-4); it says nothing about what the request carried | bit-identical | A3, full gate |
| Pack wiring | `SimulationTransport.RequestCarriesPreviousFrameWrench` (frame 0 has `ACCEL_INVALID`; frame 10's `ax` equals frame 9's published `wrench_total.force_b_x / mass`), `...RequestPaddingIsZero`; `...RequestUsesThisFramesEnvironmentSample` (F-4: Dryden wind, divisor 1; at each of 50 frames the recorded request's `alpha`, `beta`, `tas` equal `air_data` recomputed test-side from that frame's recorded frame-start state and `vehicles()[0].environment()` read right after that frame, and the test asserts adjacent frames' wind samples differ, so packing the previous sample fails) | PR 2 contract; ADR-0022 placement | A3 |
| Python | `python/bindings/_core.cpp`, one assertion in `tests/python/test_simulation.py` | enum value | full gate (pytest) |

Order: interface and accessors; recorder; stage and `Simulation` wiring; loopback; tests; bindings;
fast gate; A3; full gate.

## Validation and risks

**Acceptance (A3, from the worktree root):** `ctest --preset asan -R
'(Transport|Loopback|NoAlloc|SimulationTransport)' --no-tests=error`, then `./scripts/check-full.sh`
(all four presets incl. static-plugins, clang-tidy, pytest) with its log retained under
`docs/reviews/evidence/<sha>-full-gate.log`, and the M3 regression (`notional_transport_cruise_
regression.*`, `physics.*R1*`) passing with no file under `tests/golden/**` touched
(`git diff --stat origin/main -- tests/golden` empty). Then one Codex adversarial review (kernel
ordering, allocation, determinism, halt semantics) and the `/human-review` packet; stop before push.

**Risks.** (1) The no-transport path gains three null checks in `step()`; R8's byte comparison and
the frozen regression are the guard. (2) A transport whose `wait_ack` never expires would spin;
this is a contract on PR 4, stated in the header and reviewed there. (3) Sticky halts differ from
the other health halts, which re-run the frame on a repeated `step()`; this is deliberate (a
re-publication would duplicate a frame on the wire), stated in `simulation.hpp`. (4) The
loopback's fault injection lives in a shipped plugin; it is inert unless `peer.fault` is set.
(5) clang-tidy cognitive complexity on `from_config` and the stage: factor helpers, as before.
**Rollback:** revert the PR; nothing outside the `transport` block changes behavior.

No new dependency, no golden/tolerance/ADR edit, no architecture edit.

## Questions for Joe

- **Q-A, API version (repo mismatch).** ADR-0022 and the M4 plan say `kCoreApiVersion` 7 to 8,
  but M3 PR 4a already made it 8. **Recommend 8 to 9**, recorded in `core_api.hpp.in`; the ADR's
  intent (one bump for the new interface) holds and its wrong number is a factual slip, not a
  decision, so no superseding record. Alternative: a superseding ADR correcting the number.
- **Q-B, loopback "functor".** ADR-0007 (in force per ADR-0024) and architecture §10 say
  `LoopbackTransport` "calls an in-process functor". **Recommend** the config-scripted responder
  above (an in-process callable built from `transport.peer`): tests stay on `from_config` under all
  four presets and Python can use it in PR 6. A C++ `std::function` hook set after construction
  would need a core-visible loopback type for tests to `dynamic_cast` to. Say if you want that.
- **Q-C, recorder overflow: closed by architecture §15** (critique F-1). My draft recommended
  drop-and-count; §15 already requires a halt on log overflow. The plan now halts `kTransport`
  (`kRecorderFull`) before a frame whose records cannot fit. Remaining choice, yours: capacity
  from `sim.frames` (recommended; the PR 6 driver sets `sim.frames` to the run length) or a
  separate `transport.recorder_frames` key.
- **Q-D, `peer_timeout_ms` default** (ADR-0022 leaves it to PR 3, not below `timeout_ms`).
  **Recommend 5000 ms** (equal to the handshake default, five times `timeout_ms`).
- **Q-E, `PreviousFrame` validity: closed** (critique F-6). My draft's zero-mass fallback was
  false after a `derivative()` probe. `PreviousFrame` is now passed only when this Simulation
  completed the previous frame, which is null at frame 0 as PR 2 pins and also after a clock preset.

## Plan critique and decisions

One fresh, read-only Codex critique: `codex-companion.mjs task --fresh --prompt-file ...`
(companion 1.0.6, no `--write`, read-only sandbox; exit 0; thread `01a0dbd9-c0fe-7670-8b91-
f7adc0fcbcc8`). Raw output is in the session scratchpad and not retained in the repository, as for
the M4 plan's critique. Six findings, none rejected. Codex raised nothing on the core/transport
ownership split, the insertion point, sticky halts, `HaltReason` lifetime, or Q-A, Q-B, Q-D. I
checked each finding against the repository before accepting it.

| ID | Sev | Verdict | Evidence and resolution |
|---|---|---|---|
| F-1 recorder drop-and-count contradicts §15 | Major | Accepted | `docs/architecture.md:544`: "Log ring-buffer overflow (`halt`; a silently truncated log is not a log)". The recorder now halts `kRecorderFull` before a frame whose records cannot fit. Repeated-`run()` boundary test added. Q-C narrowed to where capacity comes from. |
| F-2 optional mapping narrowed limit validation | Moderate | Accepted | ADR-0022:54-56 lists "each command against its configured limit" with no routing exemption; my draft let an elevator-only map accept any finite rudder. Limits are now required for all four keys and checked for every field. Unmapped-field limit test added. |
| F-3 timeouts had no upper bound before narrowing to `uint32_t` | Moderate | Accepted | `simulation.cpp:47-53` (`checked_divisor`) guards the same narrowing for divisors. All three timeouts are range-checked to `[1, UINT32_MAX]` before comparing; 0 and 4294967296 tests per key. |
| F-4 no test pins "after the environment sample" | Moderate | Accepted | Constant replies ignore request content, so packing the previous frame's sample would pass R7, R8 and the pack tests. Added a Dryden, divisor-1 test comparing recorded air data with the current frame's sample and asserting adjacent samples differ; the R8 test's claim is narrowed. |
| F-5 allocation test covered only the success path | Moderate | Accepted | The draft's R6 fixture requires no halt. Added a no-alloc test around faulting exchanges of both source kinds, repeated sticky-halt calls, and the recorder-capacity halt. |
| F-6 zero-mass fallback false after a probe | Minor | Accepted | `vehicle.cpp:557`: `derivative()` calls `rigid_body_->begin_frame()` with no integration, latching a real mass beside a zero published wrench. Validity now comes from the stage's own record of the last completed frame; test added. |

Joe's decision on this plan (2026-09-25): **approved with one addition.** Q-A: 8 to 9, noted in
`core_api.hpp.in` and the human-review packet, no superseding ADR. Q-B: the scripted responder.
Q-C: capacity from `sim.frames`; recorder overflow under `kTransport` is fine. Q-D: 5000 ms.

**Addition (Joe): every halt is sticky once a transport is configured.** Any halt `step()` returns,
not only a transport one (a physics, domain, input-latch, or recorder halt, on an exchange frame or
not), is stored by the stage, returned unchanged by `pre_frame()`, and makes teardown call
`close(kAfterFailure)`. A later `step()` therefore returns the original `HaltReason` and never
re-publishes the frame or reports `kFrameDiscontinuity`. Stated in `simulation.hpp`; tested by
`SimulationTransport.NonTransportHaltOnExchangeFrameIsSticky` (a wrench NaN halt on an exchange
frame, then repeated `step()`: same `HaltReason`, one publication of that frame, fault code
`kNone`, clock unchanged). Without a transport, halts keep today's behavior (R8). A3 evidence must
show the new `NoAlloc` test names.

## Done when

A3 passes on the final candidate with retained evidence; the M3 regression passes unmodified; one
Codex adversarial review is dispositioned under the bounded fix loop; `/human-review` packet
written. Implementation completion is not Joe's approval, merge, or push.
