# Human review packet: M3 PR 4a -- ADR-0021 (DynamicsModel, RigidBody6DofDynamics, two-phase Vehicle init)

PR / candidate base and head: base `origin/main` at `26ad59482a7172e5f4ff65d6791cf565902815b3`
(`main`'s current tip; PR #41 merged, no PR open for this candidate yet -- branch
`worktree-m3-pr4a-dynamics-model`, not yet pushed). Head `e0edd16ce9d35e788f5709336151000081480205`,
working tree clean (`git status --short` empty). No commits landed on `main` since the base was
recorded; no re-validation against a moved base is needed.
Spec/plan/task: PLAN.md's M3 PR 4a line; ADR-0021
(`docs/decisions/0021-dynamics-model-owned-state.md`); implementation plan
`docs/plans/pr4a-dynamics-model.md` (Revision 3, approved by Joe before implementation began).
Risk: Tier 3 (per the plan's own tier and ADR-0021's own scope: a core interface/ABI change
touching every plugin, `kCoreApiVersion` bump, and the fixed-13-state assumption ADR-0003 pinned).
Decision requested: approve behavior and merge (this candidate's own PR does not yet exist on
GitHub; approval here is Joe's own engineering review of the local candidate, per
workflow.md's zero-approving-reviews, solo-maintainer design -- opening the PR and merging are the
next mechanical steps once this decision is recorded).

## What changes and why

Supersedes ADR-0003's fixed-13-double state-vector layout with a runtime-sized, model-owned state
buffer (ADR-0021), so a `Model` beyond the rigid body can own continuous state (e.g. a future engine
spool-lag state) without a core-level layout change. Concretely:

- **New `DynamicsModel` interface** (`core/include/astra/interfaces/dynamics_model.hpp`):
  `state_count()`, `initial_state()`, `derivative()`, `post_step()`. Copy/move all defaulted
  (unlike `Model`'s deleted copy/move), since `RigidBody6DofDynamics` is held by value in `Vehicle`
  and `Vehicle` itself is moved into `Simulation::vehicles_`.
- **`RigidBody6DofDynamics`** (`core/include/astra/kernel/rigid_body_6dof_dynamics.hpp`,
  `core/src/kernel/rigid_body_6dof_dynamics.cpp`) replaces `Eom` (deleted). `derivative()` is a
  byte-for-byte port of `Eom::operator()`'s prior arithmetic (R1, verified bit-exact below);
  `post_step()` renormalizes the quaternion, absorbing the old free `post_step_renormalize`
  function `Integrator` used to own.
- **`Model` gains five new defaulted virtuals** (`state_count`, `initial_state`,
  `integrate_state`, `bind_state`, `resolve_peers`) -- every existing subclass keeps compiling and
  behaving identically without any edit, since all five default to "no state." `kCoreApiVersion`
  bumps 7 -> 8 (vtable-shape-changing, not behavior-changing for any existing plugin).
- **`Vehicle`'s two-phase init**: phase one loads/inits every model, then seeds every model's own
  `initial_state()` into a combined state buffer and syncs a second, identically-sized stage buffer
  to match, *before* any model's `bind_state()`/`resolve_peers()` hook runs (see Independent review
  below -- this exact ordering was a Codex-found and Claude-fixed defect during this round); only
  then does it publish signals and bind. Phase two resolves every model's cross-model peer
  references via `SignalRegistry`, after every model has published, so two models with mutual state
  dependence resolve regardless of `vehicle.models` config order. `wrench_b()`/`integrate_state()`
  now read the stage buffer, refreshed before every RK4 sub-stage, giving sub-stage-exact
  (not frame-latched) cross-model state reads.
- **`StateBuffer`** (`core/include/astra/kernel/state_buffer.hpp`): copy deleted, move-construct-
  only, move-assignment deleted (Joe's own explicit design requirement from plan review).
- **`Integrator`** rewritten: owns its own RK4 scratch, sized once from a runtime `state_count`
  (was a fixed `StateVector`-sized array); `post_step_renormalize` removed (now
  `RigidBody6DofDynamics::post_step`).
- **`HealthMonitor::check_state`** gains a generic per-state-owner finiteness check
  (`NamedStateSlice`) alongside the existing rigid-body-specific checks.
- **`tests/plugins/state_owner`** (new): a configurable state-owning `Model` test plugin, reused
  across every coupling/duplicate/missing-peer/pointer-stability/hook-timing test.

Diff shape: 49 files changed, 10587 insertions(+), 407 deletions(-) against `origin/main`. The
insertion count is dominated by three things Joe should discount when judging "how big is this
really": (1) the two retained full-gate logs (`docs/reviews/evidence/{a30dd50,8e5ad1a}-full-gate.log`,
~7300 lines together, evidence artifacts, not code), (2) the frozen golden-capture harness/fixture/
goldens (`tests/golden/dynamics/**`, ~830 lines, human-owned, captured once and never touched again
after `ba383ab` -- see the three-capture history below), and (3) `docs/plans/pr4a-dynamics-model.md`
itself (~520 lines, already Joe-approved before implementation began). The actual product diff is
`core/` (~1000 lines) plus new/updated tests (~800 lines). No unexpected scope: every touched file
maps to a named clause in the plan's "Mapping and sequence" table.

## Evidence

| Check / acceptance | Result / date / duration | Log or retained artifact / candidate |
|---|---|---|
| R1 (rigid-body bit-identity vs. frozen golden, all 5 sampled states) | Pass, bit-exact | `tests/physics/dynamics/rigid_body_r1_golden_test.cpp`; `tests/golden/dynamics/rigid_body_6dof_r1_golden.hpp` (frozen at `ba383ab`) |
| R1-Sim (full-`Simulation`, frozen `cruise.yaml` fixture, bit-identity after the fixture's own frame count) | Pass, bit-exact | `tests/physics/dynamics/cruise_end_to_end_r1_golden_test.cpp`; `tests/golden/dynamics/cruise_end_to_end_r1_golden.hpp` (frozen at `ba383ab`) |
| R2 (zero heap allocation over 1000 frames, state-owning model configured) | Pass | `tests/physics/no_alloc_test.cpp::ZeroHeapAllocationsOverOneThousandFramesWithAStateOwningModel` |
| Sub-stage freshness (own state via `bind_state()`, peer state via `SignalRegistry`) | Pass, RK4-exactness closed form (own: exponential, tol 1e-10; peer: degree-2 polynomial, tol 1e-15) | `tests/unit/kernel/vehicle_state_coupling_test.cpp` |
| Mutual cross-model dependence, both `vehicle.models` orders | Pass (parameterized) | `vehicle_state_coupling_test.cpp::MutualStateDependence` |
| Hook-time initial-value visibility, both orders (this round's own R-003 fix) | Pass (parameterized) | `vehicle_state_coupling_test.cpp::HookTimeReadsSeeTheRealSeededInitialValuesNotZeroInBothModelOrders` |
| Duplicate state-signal name -> `ConfigError`; missing required peer -> `ConfigError` naming it | Pass | `vehicle_state_coupling_test.cpp` |
| `StateBuffer` pointer stability across `Vehicle`'s own move into `Simulation::vehicles_` | Pass | `vehicle_state_coupling_test.cpp::PointerIntoStageBufferSurvivesTheVehicleMoveIntoSimulation` |
| Full four-preset gate, pre-review-fix baseline (`just check`: format/docs/hooks/ruff, debug/release/static-plugins/asan at 380/380, tidy, pyright, pytest) | `check: OK`, 2026-09-21 | `docs/reviews/evidence/a30dd50-full-gate.log` (superseded by the row below) |
| Full four-preset gate, current head (384/384 across all four presets -- 4 more tests than the baseline row, from this round's own regressions) | `check: OK`, 2026-09-21 | `docs/reviews/evidence/8e5ad1a-full-gate.log` (current) |
| `just tidy` / `just format`, current head | Clean, zero findings | run inline each fix round, not separately retained beyond the full-gate log above (tidy/format are steps inside `just check`) |

Toolchain (all rows above): clang 18.1.3, Eigen 3.4.0, glibc++ 20240908, Ubuntu 24.04,
`x86_64-pc-linux-gnu`, no `-march`/`-mfma` -- see the R1 manifest excerpt below for exact per-preset
flags. No numerical tolerance was introduced or widened by this candidate; R1/R1-Sim are bit-exact
comparisons, not tolerance-based. CI stays `GATE=local`; the rows above are local-gate evidence, not
a green GitHub check standing in for it.

### R1/R1-Sim golden capture: three-capture history

Per Joe's explicit instruction, the full history, not just the clean final result:

1. **First capture attempt**: sample states 1/2/4's quaternion components were computed at capture
   time via `std::sin`/`std::cos`/`Eigen::Vector4d::normalize()`. Debug and release presets
   disagreed by 1 ULP on sample 2's `w`/`x` components. Claude's first hypothesis (a hardware FMA
   instruction contracting the arithmetic differently) was **wrong and unverified** -- Joe corrected
   it directly ("This is baseline x86-64 with no `-march` or `-mfma`, so there is no FMA instruction
   to contract"). Re-diagnosed properly: printed the six underlying trig intermediates
   (`ca,sa,cb,sb,cc,sc`) under both presets and found them bit-identical; the divergence was in the
   multiply-add *combination* only (`-ffp-contract` contracting a multi-op expression differently by
   optimization level). Fixed by hardcoding all five samples as literal `uint64_t` bit patterns via
   `std::bit_cast<double>` (values taken from a release-preset run, disclosed in the manifest),
   eliminating runtime trig/expression-contraction risk entirely. `docs/lessons.md` has the
   verified-cause entry.
2. **Second capture attempt**: re-ran with the hardcoded samples; all four presets agreed and the
   goldens were committed. A proactive post-commit check (compiling the generated golden headers
   directly under `-Werror`, which nothing had done yet) found `cruise_end_to_end_r1_golden.hpp`
   over-braced (`-Wmany-braces-around-scalar-init`, fatal under this repo's `-Werror`) -- the
   harness's own print routine double-wrapped an already-complete `{{...}}` initializer in a
   redundant outer brace pair. Fixed in the harness.
3. **Third (final, clean) capture**: re-ran; all four presets agreed on both goldens
   (`cross_preset_agreement: r1=true, r1sim=true`), both golden headers compile clean under
   `-Werror`, committed at `ba383ab`. Manifest: `tests/golden/dynamics/rigid_body_6dof_r1_manifest.md`
   (`baseline_revision: 26ad594`, `capture_revision: 609769a`). Joe then removed
   `.claude/touch-goldens`, re-arming the guard; `tests/golden/**` has not been touched since.

## Independent review

Builder: Claude (Sonnet 5). Reviewer: Codex (installed `openai-codex` plugin, version 1.0.6),
three fresh-context passes per Tier 3 -- general review, adversarial review, and a fix-verification
adversarial review. Findings recorded in `docs/reviews/pr4a-dynamics-model-review.md`.

- **R-001** (major, health_monitor.cpp): `check_state()`'s quaternion-norm check returned `apply()`'s
  own result unconditionally, including its falsy result under a non-halting `quaternion` action --
  silently skipping the new state-owner NaN check whenever drift also exceeded tolerance, even under
  `nan: halt`. Fixed: only an actual halt now short-circuits past the extra-slices check. Regression
  added. Verified fixed by the fix-verification pass.
- **R-002** (minor, tests/plugins/state_owner): the coupling fixture never exercised `bind_state()`
  itself (only a peer's `SignalRegistry`-resolved read or a constant rate) -- freshness/pointer-
  survival tests would still pass if `Vehicle` stopped calling `bind_state()` or bound the wrong
  buffer. Fixed: an opt-in `self_rate` config key makes an isolated instance's own derivative
  self-referential (`dq/dt = k*q`), closed form `q(t) = q0*exp(k*t)`. Regression added. Verified
  fixed.
- **R-003** (major, vehicle.cpp): `bind_state()`/phase two's `resolve_peers()` could observe a
  still-zero stage buffer, since the only sync ran at the very end of `init()`, after both hooks had
  already run -- a model reading its bound pointer eagerly (not only caching it for a later
  per-frame call) would see zero instead of its real seeded initial value. Fixed: extracted
  `seed_initial_state()`, which seeds `combined_` (rigid body, then every state owner) and syncs
  `stage_` to match, called before `bind_and_publish_state_owners()` or the `resolve_peers()` loop.
  Regression added, parameterized over both `vehicle.models` orders. Verified fixed by a **fresh**
  fix-verification adversarial pass that read the current diff directly (not the commit message)
  and confirmed the exact call ordering by line number.

All three findings: **verified fixed**, zero remaining critical/major/minor findings. The
fix-verification pass separately inspected the entire fix delta and its affected callers/invariants
for newly introduced defects: **"New findings: none."** No findings were rejected or deferred.

## Joe's attention

Critical files/tests to read first, in the order the plan's own end-to-end path traverses:
`core/include/astra/interfaces/dynamics_model.hpp` (the new contract) ->
`core/src/kernel/rigid_body_6dof_dynamics.cpp` (byte-for-byte port, worth a direct diff against the
old `Eom::operator()` if you want to re-derive R1's guarantee yourself rather than trust the test)
-> `core/src/kernel/vehicle.cpp`'s `seed_initial_state()`/`bind_and_publish_state_owners()`/`init()`
(the corrected two-phase ordering, R-003's fix) -> `tests/unit/kernel/vehicle_state_coupling_test.cpp`
(shows how every acceptance clause was actually interpreted).

Assumptions/risks carried forward, not introduced by this candidate: the pre-existing ADR-0012
domain-check lookahead gap (documented in `vehicle.hpp`'s own `check_domain` comment, unchanged by
this PR). Newly introduced surface: `Model`'s five new virtuals are part of the versioned plugin ABI
(`kCoreApiVersion` 8) -- an out-of-tree plugin built against version 7 is rejected at load, not
silently mis-dispatched; no out-of-tree plugin exists yet, so this is a settling bump, not a break
for anyone real, matching every prior interface-growing bump's own precedent.

Compatibility: `VehicleState::vector()`'s return type changed from `const StateVector&` to
`Eigen::Map<const StateVector>` by value; no plugin call site changes (plugins only ever use
`p_n()`/`v_b()`/`q_nb()`/`omega_b()`, never `vector()` directly). One pre-existing test's
address-identity assertion was rewritten to value-equality (never a contract `VehicleState`'s own
header promised).

Rollback: revert this branch's commits back to `3d89ede` (STATE.md handoff, pre-implementation);
`tests/golden/**` and `docs/plans/pr4a-dynamics-model.md` would need Joe's own decision on whether
to keep or also revert, since they represent separately-authorized, already-accepted artifacts
independent of the implementation itself.

## Decision record

State: pending
Human decision/date/reference: pending
Approved candidate/scope: pending (proposed: head `e0edd16` against base `origin/main` at `26ad594`)
Merge observed: not merged
Release authorization: not requested (no release pipeline exists yet; out of scope for this PR)

Significant tradeoff rationale: none identified beyond what's already recorded in the approved plan
(retiring `Eom` outright rather than keeping it alongside `DynamicsModel`; integrator-owned scratch
sized once at `Vehicle::init()` rather than shared across vehicles; deferring an `initial.state`
schema; per-model `check_state` naming via `NamedStateSlice` rather than a richer per-scalar scheme).
Accepted risks: none beyond the ADR-0012 domain-check lookahead gap, already accepted and documented
before this PR.
Nonblocking follow-ups: none recorded from this review round (zero minor/suggestion findings
deferred; R-002 was minor severity but was fixed, not deferred).
Architectural decisions: ADR-0021 (`docs/decisions/0021-dynamics-model-owned-state.md`), accepted
2026-09-20, this candidate's own implementation.
