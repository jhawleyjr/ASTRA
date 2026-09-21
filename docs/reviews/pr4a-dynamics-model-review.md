# Review: M3 PR 4a -- ADR-0021 (DynamicsModel, RigidBody6DofDynamics, two-phase Vehicle init)

Spec/plan: docs/decisions/0021-dynamics-model-owned-state.md, docs/plans/pr4a-dynamics-model.md
(Revision 3, approved)
Builder: Claude (Sonnet 5)
Reviewer: Codex (openai-codex plugin 1.0.6; general review and adversarial review, both fresh
context, per Tier 3)
Scope: branch diff against origin/main (merge-base 26ad59482a7172e5f4ff65d6791cf565902815b3),
including uncommitted/new files at time of each dispatch. General review ran against head 4ba0662.
Adversarial review ran against head ff5c11b (after the general review's own fixes were committed).
Validation: `ctest --preset asan` (382-384/382-384 pass across rounds), `just tidy`/`just format`
clean after each fix. Full four-preset gate retained at docs/reviews/evidence/a30dd50-full-gate.log
(pre-review baseline); a fresh full-gate run is pending after this round's fixes, tracked in
STATE.md.
Round: initial (general + adversarial, both round 1)

## Finding R-001

Severity: major
Status: verified
Location: core/src/kernel/health_monitor.cpp:116-119 (pre-fix)
Requirement/ADR: ADR-0021's extra_slices generic state-owner NaN check; PLAN.md M1 PR 4's "|q| drift
> 1e-6" quaternion check
Failure: `check_state()`'s quaternion-norm branch did `return apply(quaternion_action_, ...)`
unconditionally. `apply()` returns a falsy HaltReason under both `kWarn` and `kOff`, not only
`kHalt`. Under `sim.health.quaternion: warn` (or `off`) with drift above tolerance, this returned
early with a falsy result, skipping the `extra_slices` loop below it entirely -- a state-owning
model's own NaN was never checked that frame, even under `sim.health.nan: halt`.
Requested outcome: only an actual halt from the quaternion check should short-circuit past
extra_slices; a non-halting quaternion action must still let the state-owner NaN check run.
Builder response: fixed. Changed to `if (const HaltReason halt = apply(...); halt) { return halt; }`,
falling through to extra_slices otherwise. Added regression
`QuaternionWarnDoesNotSuppressAStateOwnerNanHaltUnderNanHalt`
(tests/unit/health/health_monitor_test.cpp).
Verification: reviewer (adversarial pass, head ff5c11b) reported "Both prior fixes hold by
inspection." Also independently confirmed by rerunning
`ctest --preset asan -R HealthMonitor.QuaternionWarnDoesNotSuppressAStateOwnerNanHaltUnderNanHalt`
(passes) as part of the full 384/384 suite run after the fix commit.
History: round 1 (general review), found and fixed same round; committed a7afa6a.

## Finding R-002

Severity: minor
Status: verified
Location: tests/plugins/state_owner/state_owner.cpp (pre-fix, whole file)
Requirement/ADR: docs/plans/pr4a-dynamics-model.md's Mapping table, acceptance clause for
"sub-stage-exact cross-model state reads (own, via bind_state(), and a peer's, via SignalRegistry)"
Failure: the coupling fixture's driver/reader roles exercised a peer's SignalRegistry-resolved read
and a plain constant rate, but no configuration ever made a model's derivative depend on its own
bind_state()-bound pointer. The own-state freshness and pointer-survival tests would still pass if
Vehicle stopped calling bind_state() or bound the wrong buffer -- a coverage gap, not a runtime
defect.
Requested outcome: cache and use the own-state pointer in a state-dependent derivative, then verify
its evolution and validity after the vehicle move.
Builder response: fixed. Added an opt-in `self_rate` config key: an isolated instance's own
derivative becomes `dq/dt = k*q` (self-referential exponential growth), closed form
`q(t) = q0*exp(k*t)`. Added regression
`OwnStateBindingIsSubStageFreshAndMatchesExponentialGrowth`
(tests/unit/kernel/vehicle_state_coupling_test.cpp), tolerance 1e-10 (RK4's own O(dt^5) truncation
error at dt=0.001 is far smaller; a stale or unbound read diverges by orders of magnitude more).
Verification: reviewer (adversarial pass, head ff5c11b) reported "Both prior fixes hold by
inspection." Also independently confirmed via the same full 384/384 suite run.
History: round 1 (general review), found and fixed same round; committed a7afa6a.

## Finding R-003

Severity: major
Status: verified
Location: core/src/kernel/vehicle.cpp, `Vehicle::init()`/`bind_and_publish_state_owners()` (pre-fix)
Requirement/ADR: ADR-0021's "Stage buffer invariant" section: "`Vehicle::init()` performs the
identical copy once, at the end of phase one, so the invariant already holds before frame 0's own
`step()` ever runs."
Failure: `bind_and_publish_state_owners()` called each state-owning model's own `initial_state()`
interleaved with `bind_state()` in the same per-owner loop, and `init()` synced `stage_` from
`combined_` only once, at the very end -- after both `bind_state()` (phase one) and phase two's
`resolve_peers()` loop had already run against a `stage_` buffer still holding `StateBuffer`'s
default zeroed contents. A model whose `bind_state()` or `resolve_peers()` dereferences its bound
pointer immediately (not only caches it for a later per-frame `integrate_state()`/`wrench_b()`
call) observed zero instead of the real seeded initial value. The rigid-body block's own initial
state had the identical exposure.
Requested outcome: seed all initial state, including the rigid-body state, and synchronize `stage_`
before binding and peer resolution.
Builder response: fixed. Extracted `Vehicle::seed_initial_state()`: seeds the rigid-body block via
`rigid_body_->initial_state()`, then every state-owning model's own `initial_state()`, into
`combined_`, then syncs `stage_` to match -- called from `init()` before
`bind_and_publish_state_owners()` (now `bind_state()`-and-publish only, `initial_state()` removed
from its loop) or phase two's `resolve_peers()` loop ever run. The now-redundant sync at the very
end of `init()` was removed (nothing between the new sync and the end of `init()` writes
`combined_`: `bind_state()` hands out a read-only `const double*`, `resolve_peers()` only resolves
and caches pointers per `Model::resolve_peers()`'s own contract). `tests/plugins/state_owner` gained
`own_value_at_bind`/`peer_value_at_resolve` diagnostic signals that eagerly capture what each hook
saw at the moment it ran (registered under a try/catch translating `SignalRegistry::add()`'s plain
`astra::Error` to `ConfigError`, matching `bind_and_publish_state_owners()`'s own established
precedent, so the pre-existing `DuplicateStateSignalNameThrowsConfigError` test still gets a
`ConfigError`, now from an earlier collision point on the new diagnostic names rather than the
`state.<i>` signal). Added regression
`HookTimeReadsSeeTheRealSeededInitialValuesNotZeroInBothModelOrders`, parameterized over both
`vehicle.models` orders (reusing the existing `MutualStateDependence` fixture).
Verification: reviewer (same adversarial pass that raised this finding, head ff5c11b, is the pass
that found it -- not yet re-verified by a fresh pass after the fix). Independently confirmed by
rebuild + `ctest --preset asan` (384/384 pass, including the new parameterized regression in both
orders) and `just tidy`/`just format` clean. A fresh fix-verification adversarial-review pass against
this finding is the next step before the full-gate rerun and human-review packet.
History: round 1 (adversarial review), found; fixed same round, committed ff5c11b; fix-verification
pass pending.

## Completion

Major/critical open count: 0 (R-001, R-003 major; both fixed, R-001 independently verified by the
reviewer's own adversarial pass, R-003 pending its own fix-verification pass). R-002 minor, fixed.
No rejected findings. No deferrals. Scope reviewed: full branch diff against origin/main, both
passes. Validation gap: the full four-preset gate has not yet been rerun against the
fix-containing head (ff5c11b) -- the retained log at a30dd50-full-gate.log predates all three
fixes; a fresh gate run and a fix-verification adversarial pass (per
docs/agents/review-dispatch.md's "Fix verification" section) are both still pending before this
round can close.
