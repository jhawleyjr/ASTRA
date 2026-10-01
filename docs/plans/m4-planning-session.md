# M4 Lockstep planning session — execution plan

Recovered from `~/.claude/plans/pasted-content-id-e26b-m3-complete-whimsical-sketch.md` on 2026-09-30. Approval: the file records none (its draft of `m4-lockstep.md` says spec/plan approval is pending Joe); the work merged as #50. Never committed: neither the file nor git history says why.

## Context

M3 is complete (main `827077b`, #49). Joe asked for a planning-only session for M4 Lockstep (Tier 3:
transport/concurrency, `icd/`, health behavior): housekeeping, a new worktree off `origin/main`,
`docs/plans/m4-lockstep.md` (spec + PR decomposition + open questions), one Codex plan critique, and one
commit (plan + rewritten STATE.md), then stop for Joe's review. No push, no implementation code, no
research subagents (ASTRA rule; I read every input directly, and will run no Explore/Plan agents).

Plan mode blocked all writes/commands during the read pass, so nothing below has been executed yet.

## Steps once approved

1. **Housekeeping, main checkout.** `git worktree remove .claude/worktrees/m3-pr10-checkoff`; then
   `git worktree prune`. Leave all four local `worktree-*` branches. (Observed: that worktree is at `6aca14a`;
   check it is clean before removing. If it has uncommitted work, stop and ask rather than `--force`.)
2. **New worktree.** `git fetch origin`; confirm `origin/main == 827077b82ea5349afa0c81949bd282c5c9ac3b14`
   (local `main` already matches); `git worktree add .claude/worktrees/m4-lockstep-plan -b worktree-m4-lockstep-plan origin/main`.
   One `git` command per Bash call (lessons 2026-09-14 worktree-guard entry); files via Write/Edit only.
3. **Write `docs/plans/m4-lockstep.md`** (spec + plan in one artifact, Tier 3 so full fields): content below.
4. **Codex plan critique**, once: resolve `CODEX_COMPANION` from the installed plugin (not a recursive `find`),
   `/codex:rescue --fresh --wait` read-only, focus built from a single-quoted heredoc, exit status captured right
   after the call, log to scratchpad. Include plan path, acceptance commands, ADR-0006/7/8/9/10/17 context. Record accepted/rejected
   findings (with evidence) in the plan's "Plan critique" section. If Codex is unavailable, record critique as pending.
5. **Rewrite `STATE.md`** (≤ 40 lines): In flight = M4 planning, awaiting Joe's spec/plan approval; artifact link;
   base `827077b`; last verified result = M3 gate evidence pointer (unchanged; this session ran no product gate);
   next step = Joe reviews the plan and answers the open questions; `Correction rounds: 0/2. Failed integrations: 0/2.`
6. **One commit** (message via `git commit -F` from the scratchpad; American English; attribution line only as the
   session reminder allows; no push). Verify `docs-check` (`python3 scripts/docs_check.py`, `git diff --check`)
   since only docs change. Then stop. Do not touch PLAN.md (duplicate noted in the plan, not removed).

## Content of `docs/plans/m4-lockstep.md`

### Repository findings (observed, verified this session)
- No `embedded/`, no `plugins/sim/`, no `Transport` interface, no logger of any kind exist. `Simulation::step`
  (`core/src/kernel/simulation.cpp:562`) has no exchange stage; order today is environment scheduler → per-vehicle
  {sample env → `vehicle.step` → `InputRegistry::latch` → `integrate` → atmosphere domain check} → clock advance.
- `PluginDescriptor::create` returns `unique_ptr<Model>`, so a transport plugin must be a `Model` (or the
  descriptor changes; both need `kCoreApiVersion` 7→8).
- ADR-0017 disclosed the M4 gap (transport is not a `WrenchSource`, has no `bind_inputs`). **Resolution proposed:**
  the exchange stage is owned by `Simulation` (core), not by a plugin: it resolves `InputRegistry::find()` handles
  once at init from a config-declared command map and calls `handle.write()` before `latch()`. This needs no seam
  extension and gives zero-lag commit the same frame (§5). The ICD-field→input-name map is aircraft knowledge and
  lives in config, never `core/` (principle 6).
- `HealthCheck` is a `uint8` enum bound to Python (`python/bindings/_core.cpp:346`); a `kTransport` value, a
  `sim.health.transport` key, and a signal are additive. `Simulation.run/step` bindings do not release the GIL
  (no `gil_scoped_release` in `_core.cpp`), so an in-process Python thread cannot kill the peer mid-run.
- `cruise.yaml` already carries the trim (elevator 0.02667 rad, throttle 0.4986, θ=α=0.02359 rad, v_b=(229.94,0,5.42)),
  Dryden seed 42, `w20_knots 15`, `sigma_high 3.0` m/s, and a `# light` comment. Frame 0 of that run already has a real
  Dryden gust `(-3.08, 0.91, 6.63)` m/s (lesson 2026-09-22): the autopilot starts in turbulence, not still air.
- Architecture is internally inconsistent on the M4 count: §11 says "10,000 lockstep frames", §16/ADR-0007/PLAN.md say
  "10,000 exchanges"; default divisor is 10, so these differ by 10× (100 s vs 10 s sim time).
- ICD `astra_sim_to_sw_v1` has fields the sim cannot fill from existing utilities without a decision: `cas` (not in
  `air_data.hpp`), `ax/ay/az` (kinematic vs specific force unstated), `lat/lon` (undefined for flat Earth), `status_flags`
  (no bit meanings defined). `astra_sw_to_sim_v1.throttle[2]` vs one shared `controls.throttle` input (ADR-0017 default).
- No wall-clock symbol scan exists (§15's determinism guard is prose only; grep finds no implementation).
- CI is `GATE=local`; ADR-0008's "CI builds embedded standalone" therefore has to be a `just check` recipe.
- MIL-F-8785C p.49 Fig. 7 (read this session at low resolution): the "light" curve at ~33,000 ft (10,000 m) appears to
  be well under 5 ft/s (~1.5 m/s); the shipped `sigma_high: 3.0` m/s (9.8 ft/s) looks closer to "moderate". Unverified
  by a proper chart read; flagged as Q, and Fig. 7 digitization is a human-owned golden per `dryden.cpp` header.

### Spec — requirements (each cites its M4 acceptance clause)
| ID | Requirement | Check / oracle |
|---|---|---|
| R1 | 10,000 exchanges, zero missed: sim counter `health.transport_missed == 0`, exchanges == 10,000, every reply echoes its request frame, magic/version valid; autopilot's own exit summary independently reports 10,000 handled | driver test reads both counters + recorder |
| R2a | Altitude within ±30 m of initial 10,000 m at every exchange sample from truth ICD fields, cross-checked against final `Vehicle` state | driver test over recorder |
| R2b | Heading within ±2° of commanded (0°) at every sample, angle-wrapped | same |
| R2c | Discrimination: same run with an impaired autopilot (zero gains / constant trim output) is *reported*; if open loop also passes the band, the band is recorded as non-discriminating (lesson 2026-09-19) | driver test, informational unless Joe says gate |
| R3 | Bit-identical across two runs: byte-equal exchange recorder + final state bytes (SHA-256 recorded); still equal with injected autopilot scheduling jitter; changing the Dryden seed makes them differ (comparison is live) | two fresh-process runs |
| R4 | Peer death: autopilot SIGKILLed (and separately `abort()` via test flag) after exchange N → `HaltReason{kTransport, source = transport plugin path, frame}` within timeout + declared slack; no hang; clock does not advance past halt; no post-halt frame stepped; state finite | driver test with hard test timeout |
| R4b | Sim death → autopilot exits nonzero within its HAL timeout (no orphan) | driver test |
| R5 | Stale/duplicate/out-of-order ack, wrong echo frame, bad magic/version, late ack → halt naming transport; NaN command → existing `health.input` halt naming the input | unit tests on loopback + shm |
| R6 | Zero heap allocation over 1000 frames including exchanges | extend `tests/physics/no_alloc_test.cpp` pattern |
| R7 | ZOH: commanded inputs constant across the 9 frames between exchanges; a new command is committed the frame it arrives (no lag) | unit test on latched values |
| R8 | Config: no `transport` block ⇒ behavior bit-identical to today (M3 frozen regression untouched); unknown/absent keys and unknown input names ⇒ `ConfigError` naming path | existing goldens + config tests |
| R9 | `embedded/` builds standalone with only a C++17 compiler, links only `icd/`+`embedded/hal/`; autopilot binary imports no `astra_core` symbol (`nm -D` test) | new `just` recipe + test |
| R10 | HAL: `hal_wait_frame`/`hal_send`/`hal_now`; `hal_now` derives from the frame, never a clock; wait has a timeout and reports it | HAL unit tests |
| R11 | `icd/astra_icd.h` layout untouched (comment-only edits at most); static asserts hold under clang-18 and `gcc -std=c11` | existing `tests/icd` |
| R12 | Every filled `sim_to_sw` field has a stated definition and a hand-computed oracle test; unfillable fields (flat-Earth lat/lon) are a defined value + status flag, never garbage | unit tests |

### Proposed PR decomposition (dependency order; one writer per PR)
| PR | Scope | Tier | Binding ADRs | Acceptance commands |
|---|---|---|---|---|
| 1 | Decision records + architecture edits: new ADR (exchange stage, config-declared command map, seq sentinel, handshake, exchange recorder as M4 "log", monotonic-clock timeout exception to ADR-0010); §5/§10/§11/§15 wording; ADRs are Joe-authorized files | 3 (docs; policy) | 0006, 0007, 0008, 0009, 0010, 0011, 0017 | `python3 scripts/docs_check.py`, Codex review; full gate at Joe's discretion (M3 PR 0 precedent) |
| 2 | ICD truth packing: pure functions state→`astra_sim_to_sw_v1` (attitude, air data incl. new `cas`, altitude/vertical speed, lat/lon, accel), oracle unit tests | 3 (`icd/` semantics, numerical util) | 0001, 0002, 0006 | `ctest --preset asan -R icd_pack`; `just check` |
| 3 | Core seam: `Transport` interface, `Simulation` exchange stage + command map, `HealthCheck::kTransport`/`sim.health.transport`/signals, exchange recorder, `plugins/sim/transport/loopback`, `kCoreApiVersion` 8, Python enum | 3 (kernel, transport, health) | 0006, 0007, 0009, 0011, 0016, 0017 | `ctest --preset asan -R 'transport|loopback|no_alloc'`; `just check`; M3 goldens pass unmodified |
| 4 | `plugins/sim/transport/shm` + `embedded/hal` shm backend, one shared layout header, futex protocol, two-process echo-peer tests | 3 (concurrency) | 0006, 0007, 0008, 0009 | echo-peer 10,000-exchange test, kill test, protocol-error tests; `just check` |
| 5 | `embedded/autopilot` (4 PID loops, C++17), standalone-build recipe, no-`astra_core` symbol test, gain design vs `astra.linearize`, scripted-frame unit tests | 2 | 0006, 0008 | `just embedded-standalone` (new), `ctest -R autopilot` |
| 6 | Python driver `astra.lockstep` (subprocess lifecycle) + M4 acceptance tests R1–R4; GIL release on `run` if the kill test needs it | 3 | 0007, 0008, 0010 | `.venv/bin/pytest tests/python/test_lockstep.py`; `just check`; retained evidence |
| 7 | M4 closeout: PLAN.md check-off, follow-ups, STATE | 0–1 | none | `docs_check` |

Plan critique for each Tier 3 PR: PR 3 and PR 4 get their own (ownership of the exchange stage; futex ordering); PR 1 is this plan's own critique.

### Open questions for Joe (unpinned by the architecture)
1. **Count:** "10,000 exchanges" (100,000 frames, 100 s at divisor 10) or 10,000 frames (§11)? Recommend exchanges; §11 gets corrected.
2. **Wall clock:** a futex timeout with spurious-wakeup handling needs one `CLOCK_MONOTONIC` read. ADR-0010/AGENTS.md restrict wall-clock access to the `RealTime` pacer. Recommend a narrow, ADR-recorded exception (transport timeout only; never feeds sim state, so determinism holds; expiry is a halt, never a value). Alternative: relative-timeout loop with a bounded retry count, no clock read.
3. **Timeout value:** recommend `transport.timeout_ms` default 1000 (ASan/valgrind-safe), tests use 200; handshake timeout separate (autopilot startup) default 5000. "Within one timeout" = timeout + a declared scheduling slack (proposal: 250 ms).
4. **HAL boundary:** single shared layout header in `embedded/hal/` consumed by the sim plugin (one source of truth) vs ADR-0007's "mirrored byte-for-byte" copies plus a layout test. Recommend the single header. HAL calls: add a timeout return and an init/close; `hal_now` frame-derived.
5. **Process lifecycle:** who creates/unlinks the segment (recommend sim creates `O_EXCL`, unlinks on close/next start; name `astra_<sim pid>_<run>`); Python driver launches the autopilot subprocess and reaps it (zombies defeat `kill(pid,0)`, so the timeout, not pid probing, is the detector); autopilot must exit when the sim dies (HAL timeout).
6. **Sequence words:** ADR-0007's `req_seq = frame` collides with a zero-initialized word at frame 0. Recommend initializing both words to `UINT32_MAX` (sentinel), rejecting `sim.frames ≥ 2^32−1` at init, wrap-safe compare; no ADR supersession needed. §15's "one round-trip before frame 0": recommend a header-only handshake, not a probe frame.
7. **"Light Dryden" as config:** current `sigma_high: 3.0 m/s`, `w20_knots: 15` is labeled light but Fig. 7 at 10 km suggests lighter. Needs your chart read (Fig. 7 is a human-owned golden). Proposal: a named `cruise_light_turbulence.yaml` (architecture §4 tree already lists it) pinning your chosen numbers, seed 42, plus a non-gating 8-seed margin sweep.
8. **Which log satisfies "bit-identical" before M5's HDF5:** recommend a preallocated in-memory exchange recorder (every ICD frame pair, both directions, plus final state bytes, dumped raw after the run). Frames zero-initialized so padding is deterministic. M5's HDF5 supersedes it; comparison is same-build, same-machine only (lesson 2026-09-21: cross-preset bits differ).
9. **Actuators/deflection range (PLAN.md:527):** recommend no dynamic `ActuatorModel` in M4 (ideal, commanded = achieved); add config-declared per-command limits that *halt* on violation (no clamping, which would hide autopilot faults). Alternative: defer entirely. Decide before PR 3.
10. **Throttle[2]:** ICD has two, ADR-0017 shares one `controls.throttle`. Recommend command map accepts both and requires equality (warn/halt otherwise) rather than changing turbofan/trim/linearize names in M4.
11. **ICD fields:** `cas` definition; `ax/ay/az` = specific force or kinematic; flat-Earth `lat/lon`; `status_flags` bits. Recommend specific force (what an IMU would report), documented in header comments only.
12. **Autopilot trim biases:** how it learns them (driver passes `astra.trim` output as CLI args vs first-frame capture). Recommend explicit args, integrators start at bias; first reply at frame 0 must equal trim to avoid a startup transient.
13. **Envelope monitors (PLAN.md 600/605):** the two bullets are byte-identical duplicates; remove one in the PR 7 closeout. Recommend *not* folding the feature into M4.
14. **Validation gap to accept:** ASan cannot see futex/memory-order bugs, and TSan cannot follow cross-process futex synchronization. Mitigation: 1e6-exchange stress test, review focused on acquire/release, single-writer-per-word discipline.

### Validation and risks
`just check` remains the definition of done for PRs 2–6; fast loop `check-fast.sh`. Risks: startup transient (Dryden frame-0 gust), autopilot gains stability margin vs a phugoid with ζ≈0.027, test runtime under ASan (100k frames), stale `/dev/shm` segments after failed runs (cleanup fixture), LSan-under-ptrace environment limit (lesson 2026-09-17). No new dependency (futex via `syscall`), no golden/tolerance edits; ADR files are Joe-authorized.

### Plan critique and decisions
Recorded after the Codex pass (accepted/rejected with evidence). Spec/plan approval: pending Joe.

## Verification of this session's own deliverable
`git worktree list` shows the new worktree at `827077b` and no `m3-pr10-checkoff`; `git log --oneline -2` in it shows one new commit above `827077b`;
`git show --stat HEAD` touches exactly `docs/plans/m4-lockstep.md` and `STATE.md`; `python3 scripts/docs_check.py` exits 0; no push (`git status -sb` shows no upstream).
