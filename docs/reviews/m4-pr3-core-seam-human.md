# Human review packet: M4 PR 3, core seam

PR / candidate base and head: base `794158d` (`origin/main`, #52's merge; re-fetched 2026-09-25 and unmoved).
Product candidate `add18b1` on branch `worktree-m4-pr3-core-seam` (three commits: `126cfed` implementation,
`d9ab7de` pyright fix, `add18b1` Python API-version pin plus retained logs). Clean tree at `add18b1`; the commit
adding this packet and the A3 log adds documents only. Not pushed; no PR opened.
Spec/plan/task: [PR 3 plan](../plans/m4-pr3-core-seam.md) (approved by Joe 2026-09-25 with the all-halts-sticky
addition; Q-A to Q-D decided), under the [M4 plan](../plans/m4-lockstep.md) PR 3 row, R5 to R8, A3, and ADR-0022,
ADR-0023, ADR-0024.
Risk: Tier 3 (kernel per-frame order, a new plugin-facing interface, health/halt behavior).
Decision requested: approve behavior for this PR and its squash-merge (after I push and open the draft PR, which
needs your go-ahead). No release requested.

## What changes and why

**Before:** no `Transport`, no exchange stage; `astra_sim_to_sw_v1` was packable (PR 2) but nothing called the packer.
**After:** a config with a `transport` block runs a lockstep exchange for vehicle 0 at `frame % divisor == 0`, after
the environment sample and before `vehicle.step()`; commands are validated and staged through `InputRegistry`
handles, so the same frame's `latch()` commits them (zero-order hold between exchanges). Every failure halts
`HealthCheck::kTransport`. Without a `transport` block nothing changes (R8).

End-to-end path of one exchange (read in this order):

1. `Simulation::from_config` (`core/src/kernel/simulation.cpp`): after `vehicle.init` (every `bind_inputs()`) and
   `apply_initial_controls`, requires `physical`, builds `LockstepExchange` (parses keys, resolves the command map
   with `InputRegistry::find()`, loads the plugin, checks `icd_version`, registers `transport.*` signals); then
   health init, registry freeze, unknown-key check; **then** `open()` (records the frame-0 state and runs the handshake).
2. `Simulation::step` wraps `step_frame`: `pre_frame()` returns a stored halt (sticky) or a recorder-capacity halt;
   after the frame, `note_halt()` stores any halt, or `post_step()` records the state and marks the frame completed.
3. `step_frame` → `sample_environment` (the former inline block, moved unchanged for clang-tidy) →
   `set_environment` → **`LockstepExchange::exchange`** → `vehicle.step` → `latch` → `integrate`.
4. `core/src/kernel/lockstep_exchange.cpp`: sequence range (`frame >= UINT32_MAX - 1` halts), continuity, pack with
   `PreviousFrame` only if this Simulation completed frame k-1, publish, the receive loop (unchanged ack means wait;
   `== seq` means done; anything else means out-of-order), read and record the reply, validate in ADR-0022's order
   (magic/version/frame, finite, throttle equality, limits on **every** field), then stage the mapped fields.

Your decisions as implemented: Q-A `kCoreApiVersion` 8→9 with the reason in `core_api.hpp.in` (ADR-0022's "7 to 8"
was drafted against a stale number; no superseding ADR); Q-B the loopback's responder is config-scripted
(`transport.peer`); Q-C recorder capacity from `sim.frames` (exchanges `ceil(frames/divisor)`, states
`frames + 1`), and overflow halts `kTransport` (`kRecorderFull`, source `transport.recorder`); Q-D
`peer_timeout_ms` default 5000. **Your addition:** with a transport, every halt `step()` returns is stored and
returned unchanged by later `step()`/`run()` calls (no environment step, no re-publication, never
`kFrameDiscontinuity`), and teardown passes `CloseReason::kAfterFailure`; stated in `simulation.hpp`. Without a
transport, halts stay non-sticky (tested).

Diff (`git diff --stat 794158d add18b1`, excluding `docs/reviews/evidence/`): 28 files, +2624/-39 (27 files,
+2279/-39 of code and tests; the rest is the plan). New: `interfaces/transport.hpp` (+ key
function), `kernel/lockstep_exchange.{hpp,cpp}`, `kernel/exchange_recorder.{hpp,cpp}`,
`plugins/sim/transport/loopback/`, `tests/plugins/wrong_icd_transport/` (NO_LOAD_ALL), two unit test files, one
no-alloc fixture, the plan. Changed: `simulation.{hpp,cpp}`, `vehicle.{hpp,cpp}` (`published_wrench()`,
`latched_mass()`), `rigid_body_6dof_dynamics.hpp` (`mass_props()`), `virtual_clock.hpp`
(`set_frame_for_testing`), `health_monitor.hpp` (`kTransport` appended last), `core_api.hpp.in`, `_core.cpp`
(`TRANSPORT`), `no_alloc_test.cpp` (+2 tests), `test_simulation.py` (+1), `test_package.py` (pin 8→9), CMake
lists. **No file under `tests/golden/**`, no tolerance file, no ADR, no architecture edit** (`git diff 794158d
add18b1 -- tests/golden docs/decisions` is empty).

## Evidence

| Check / acceptance | Result | Log / candidate |
|---|---|---|
| Full gate `./scripts/check-full.sh` | **`check: OK`, exit 0**; debug, release, static-plugins, asan each 450/450; tidy, format-check, pyright clean; pytest 116 passed | [`add18b1-full-gate.log`](evidence/add18b1-full-gate.log), `add18b1` |
| A3 `ctest --preset asan -R '(Transport\|Loopback\|NoAlloc\|SimulationTransport)' --no-tests=error` | exit 0, 63/63: 26 `SimulationTransport`, 7 `LoopbackTransport`, 10 `physics.NoAlloc` incl. the new `ZeroHeapAllocationsOverOneThousandFramesWithLoopbackExchanges` and `ZeroHeapAllocationsOnTransportFaultsAndStickyHalts` | [`add18b1-a3.log`](evidence/add18b1-a3.log) |
| M3 regression unmodified | `notional_transport_cruise_regression.*.MatchesTheFrozenTrajectoryBitwise` and `...TwoRunsFromTheSameConfigAreBitwiseIdentical` pass (also in every gate preset) | same A3 log |
| Failed integration 1 | `126cfed`: all four presets passed; **pyright** failed (my new test called `int()` on a nanobind `enum.Enum`) | `126cfed` full-gate log, pyright failure (log removed at Joe's request, 2026-09-25) |
| Failed integration 2 | `d9ab7de`: all four presets passed; **pytest** failed: `test_package.py:31` pins `core_api_version() == 8`, the hand-bumped companion to Q-A I missed | `d9ab7de` full-gate log, pytest API-version-pin failure (log removed at Joe's request, 2026-09-25) |

Requirement coverage (tests in `tests/unit/kernel/simulation_transport_test.cpp` unless noted):

- **R5**: `EachProtocolFaultHaltsNamingThePluginAtItsFrame` (out-of-order ack, stale reply frame, bad magic, bad
  version, timeout, peer closed: each `kTransport`, plugin path, frame 30, clock 30, the fault code, the failed reply
  not applied); `UnchangedAckObservationsKeepWaitingAndNeverFail`;
  `NonFiniteCommandHaltsNamingTheFieldUnderEveryInputAction` (nan, inf, -inf × 5 fields × `sim.health.input` halt,
  warn, off = 45 cases); `ThrottleMismatchHaltsNamingThrottle`; `CommandLimitsAreInclusive...`;
  `AnUnmappedFieldOutsideItsLimitStillHalts` (F-2); `ValidationFollowsTheAdrOrder`; `AFailedReplyStagesNoInputAtAll`
  (latches by hand after the halt: a valid elevator beside an out-of-limit rudder is not staged);
  `SequenceRangeHaltsAtTheReservedValueIncludingOnRepeatedSteps`; `AFrameCounterDiscontinuityHalts`;
  `RecorderCapacityHaltsAtTheFirstFrameBeyondSimFrames` (F-1); config errors naming paths, including every timeout
  at 0, -1 and 4294967296 (F-3); handshake failure is `ConfigError` at `transport`; no-physics, non-Transport and
  wrong-ICD plugins are rejected. `loopback_transport_test.cpp` checks the responder directly.
- **Your addition**: `NonTransportHaltOnExchangeFrameIsSticky` (wrench NaN on exchange frame 20: three more `step()`
  calls return the identical `HaltReason`, same `source` pointer; frame 20 published once; fault `kNone`; missed 0;
  close reason `kAfterFailure`); `ATransportHaltIsStickyAndNeverRepublishes`; `WithoutATransportHaltsAreNotSticky`.
- **R6**: the two new `NoAlloc` tests (100 exchanges with 3 unchanged acks each and two schedule changes; then a
  plugin-path fault, an ICD-field fault, the recorder halt and a sticky wrench halt, each followed by ten `step()`
  calls, each asserted to halt at its expected frame).
- **R7**: `ZeroOrderHoldHoldsBetweenExchangesAndCommitsOnTheArrivalFrame`: over 30 frames the latched
  `v0.controls.elevator` and the value the wrench source read at k1 and k4 equal the latest exchange's command,
  changing exactly on frames 10 and 20.
- **R8**: every existing test unmodified; `NoTransportBlockRegistersNoTransportSignals`;
  `ConstantTrimRepliesMatchNoTransportRunBitForBit` (cruise.yaml, Dryden, 300 frames: `memcmp` of the combined state
  equals 0 against the same config without `transport`; its claim is limited to non-perturbation).
- **Placement (F-4)**: `RequestUsesThisFramesEnvironmentSample` (divisor 1, 50 Dryden frames: recorded `alpha`,
  `beta`, `tas` equal `air_data` recomputed from that frame's recorded start state and sample, and adjacent wind
  samples are asserted to differ). **PreviousFrame (F-6)**: `RequestCarriesThePreviousFramesPublishedWrench`,
  `PreviousFrameIsInvalidWithoutACompletedFrameEvenAfterAProbe`.
- **Recorder/determinism**: `RecorderSerializesAFixedHeaderThenTheStreams` (48-byte header, sizes 200/64/112,
  counts, total length); `RecorderBytesAreIdenticalAcrossTwoRunsAndDifferWithTheSeed` (seed 42 twice equal, 43
  differs).

Toolchain: clang-18, Ubuntu 24.04, same machine for every run. GitHub CI is local-gate mode and has not run (not pushed).

## Independent review

- **Plan critique** (fresh read-only Codex `task`, companion 1.0.6): six findings F-1 to F-6, all verified and
  accepted before your approval; recorded in the plan.
- **Adversarial review** (Codex `adversarial-review --base origin/main`, thread `01a0dc04-1107-72e2-b590-684f93d814fd`,
  exit 0) on `d9ab7de`, focused on kernel ordering, allocation, determinism, halt semantics, PreviousFrame/init
  order, and whether the tests discriminate: **verdict approve, no material findings**
  ([`d9ab7de-codex-adversarial.log`](evidence/d9ab7de-codex-adversarial.log)). It read the gate log while the gate
  was still running and noted that, as it should have.
- **Coverage gap:** the review saw `d9ab7de`. `add18b1` changes only `tests/python/test_package.py` (the pin
  8→9 and its comment) and adds logs, with no product code; I did not request a second review for that delta.
  Correction rounds used: 0/2. Failed integrations: 2/2 (above). A third failure would have gone to you; the third run passed.

## Joe's attention

- **The receive state machine is split** (`transport.hpp` header comment). Core loops on `wait_ack()` and classifies
  each observation; termination depends on the transport's contract that every wait after a publication shares one
  deadline and then returns `kExpired`. PR 4's shm plugin must honor that; the loopback's deadline is its script.
- **Sticky halts with a transport** differ from every other configuration: `run()` after any halt returns the
  stored halt immediately. A Python user who expects a re-step to re-run the frame will not get one once a
  transport is configured.
- **`close(kAfterFailure)` is observable only as `transport_close_reason()`**: the loopback's `close()` does
  nothing. What the shm side does after a failure (for example, whether the peer exits 0) is PR 4's to decide under
  ADR-0024.
- **Test hooks on shipped classes:** `Simulation::preset_frame_for_testing` and `VirtualClock::set_frame_for_testing`
  are public, named, and use the same posture as `for_testing`. The loopback ships its fault injection; it is
  inert unless `transport.peer.faults` is set.
- **Recorder memory** is preallocated from `sim.frames` (about 11 MB of state records per 100k frames). A
  `sim.frames` large enough to exhaust memory throws `std::bad_alloc` at init, not a `ConfigError`, and
  `sim.frames` near `UINT64_MAX` would wrap the capacity arithmetic. Neither is reachable from a sane config; not
  guarded in this PR (follow-up C, below).
- **Command map**: every field is optional, but at least one must be mapped. Limits are required for all four
  keys, and every validation step covers every field. `throttle` must be two identical input names.
- Rollback: revert the squash commit. The no-transport path differs only by null checks and the moved
  `sample_environment`, pinned by the frozen regression.

## Decision record

State: approved
Human decision/date/reference: Joe, 2026-09-25: "Approved: behavior and scope of add18b1". Before push, the two
failed-gate logs were removed (rows kept above, as in PR 2) and follow-up C recorded; those are document-only
commits on top of `add18b1`.
Approved candidate/scope: `add18b1` (product), plus document-only commits (this packet, evidence changes).
Merge observed: not merged (Joe squash-merges; draft PR)
Release authorization: not requested

Significant tradeoff rationale: sticky halts with a transport (Joe's addition) trade re-step flexibility for never
re-publishing a frame.
Accepted risks: termination of the receive loop depends on the transport's deadline contract (PR 4 must honor it).
Owner: Joe.
Nonblocking follow-ups: **Follow-up C** (Joe, 2026-09-25, not fixed in this PR): recorder sizing for an absurd
`sim.frames` -- preallocation throws `std::bad_alloc` at init instead of a `ConfigError` naming `sim.frames`, and
`sim.frames` near `UINT64_MAX` wraps the capacity arithmetic (`sim_frames + 1`, `sim_frames + divisor - 1`) in
`LockstepExchange`'s constructor. STATE.md's Follow-ups A and B from PR 2 are unchanged.
Architectural decisions: ADR-0022, ADR-0023, ADR-0024 (accepted); no new ADR. The `kCoreApiVersion` number
discrepancy with ADR-0022 is recorded in `core_api.hpp.in` per Q-A.
