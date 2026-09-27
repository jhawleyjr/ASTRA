# Review: M5 PR 3, derived flight-condition signals

Spec/plan: [`docs/plans/m5-tooling.md`](../plans/m5-tooling.md) (PR 3 row, Q13, R10),
[ADR-0026](../decisions/0026-derived-flight-condition-signals.md)
Builder: Claude Opus 5.5
Reviewer: Codex (companion 1.0.6; fresh context, read-only; general review and adversarial review)
Scope: committed branch diff `origin/main` (`82686ec`) to `5f67fe8`: the `DerivedSignals` stage, its wiring in
`core/src/kernel/simulation.cpp`, `Vehicle::has_physics`, `tests/unit/kernel/derived_signals_test.cpp` and one
NoAlloc case. The worktree was clean.
Validation: at `5f67fe8`, the plan's acceptance regex under `asan` passed 86/86 and the base-versus-candidate
recorder SHA-256 matched on three loopback configs. The full gate was started and then stopped when F1
superseded the candidate. Results for the fix are in the PR body.
Round: initial, then fix 1.

## Finding [F1]

Severity: major (reported as high)
Status: fixed-unverified
Location: `core/src/kernel/derived_signals.cpp:65-72` at `5f67fe8`
Requirement/ADR: [ADR-0026](../decisions/0026-derived-flight-condition-signals.md) (ax ay az are frame k-1's
k1-stage `wrench_total` force over frame k-1's mass)
Failure: without a transport, a halted frame is retried. `Vehicle::integrate` publishes frame k's k1 wrench before
a later RK4 stage, or a later vehicle, can halt, and its rollback restores only state. On the retry
`last_completed_frame_` is still k-1, so `pack()` passed the live `published_wrench()` as a valid PreviousFrame:
frame k's failed-attempt wrench, published as frame k-1's specific force.
Requested outcome: take the wrench and mass from a snapshot made when the whole frame completes, and cover a
retry after a mid-integration halt.
Builder response: fixed. `DerivedSignals::note_completed(vehicles, frame)` now copies each physics vehicle's
`published_wrench()` and `latched_mass()` into a preallocated `Slot::previous` when the frame completes
(`simulation.cpp` step_frame, before `clock_.advance()`), and `pack()` passes `&slot.previous`. On every completed
frame the snapshot equals the live values the exchange reads, so the exchange's request is unchanged (R10 rerun).
New test `DerivedSignals.ARetriedFrameAfterAMidIntegrationHaltKeepsThePreviousFrameWrench`: nesc_aero drag plus
`nan_wrench` (NaN from k2 at frame 5) on flat Earth with isa1976; frame 5 is attempted twice and ax must equal
frame 4's force over mass bitwise both times. With `pack()` reading the live wrench again (a temporary mutation),
the test fails on the second attempt. The later-vehicle halt Codex also names takes the same path (the snapshot
is taken only once every vehicle integrated) but has no test of its own: `for_testing` has no atmosphere, so no
frame-varying wrench is available there.
Verification: Codex adversarial fix verification at `7cf5a47`, thread `01a0e228-4022-7970-9f4b-5716c8149c68`:
approve; F1 holds, including the later-vehicle halt by source inspection; new findings: none.
History: round 1, open, Codex adversarial review; fix 1, fixed-unverified; verified at `7cf5a47`. The full gate at
`7cf5a47` then failed only on clang-tidy (`step_frame` cognitive complexity 26, threshold 25). The next commit
builds the stage unconditionally (it registers nothing without physics) and gives it the Earth model at init, so
`step_frame` calls `pack()` and `note_completed()` with no added branch. It is a refactor with no behavior change
(R10 rerun identical).
Human exception if needed: none.

## Completion

Codex general review at `5f67fe8`: no actionable defects. Adversarial review at `5f67fe8`: needs-attention, F1
only. Critical/major open: F1 pending verification. Reviewer recommendation is not Joe's approval.
