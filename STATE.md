# STATE (rewritten each session, never appended; ≤ 40 lines; true at handoff)

## In flight
**PR 4 implementation is STOPPED** (Joe's mid-session instruction). Joe's stated framework intent
(`docs/refs/astra_framework_intent_and_state_audit.md`, main checkout, gitignored) overrides
ADR-0000's "focused on commercial transport aircraft" clause. `docs/plans/
m3-framework-realignment.md` drafts three superseding ADRs, **all proposed, none accepted**:
**ADR-0019** (purpose), **ADR-0020** (`AeroModel`/`PropulsionModel` become true marker interfaces,
no sealed `final`, no core-defined `Inputs` struct — supersedes ADR-0018's `Inputs`/`final` parts),
**ADR-0021** (core `DynamicsModel` interface + model-owned continuous states — supersedes ADR-0003's
fixed-13-layout decision). No code this session.

Every audit §4 claim was verified directly against code (exact line citations in plan file); all
substantively accurate. Three defects in this session's own prior PR 4 plan (`docs/plans/
pr4-notional-aero-turbofan.md`, now superseded) confirmed true, not defended: V→0 "fix" used `rho`,
which `AeroInputs` doesn't carry; `thrust_ref_n` left zero throttle margin; a claimed PLAN.md
follow-up line was never written.

Each ADR got its own Codex plan critique + re-verification wherever a fix changed a real claim (all
findings/fixes recorded under each ADR's "Approval" section): ADR-0019 — 1 critique, 1 medium,
fixed. ADR-0020 — 1 critique + 1 re-verify, 1 high + 1 medium + 1 new, all fixed (high: removed
isfinite guard can cause *permanent*, not delayed, silent corruption — R-001's own failure mode).
ADR-0021 — 1 critique + 2 re-verify, 3 high + 2 medium, all fixed (wrench/state coupling now
explicitly frame-latched via `SignalRegistry`, not sub-stage-exact; `VehicleState`'s constructor and
`Integrator`'s functor both needed real signature changes wrongly called "unchanged"; R1 needed
representation-level comparison across a full integration step, not `==` on a bare derivative).

## Last verified result
None this session — planning only, no gate run.

## Next step
Joe decides each ADR independently in `docs/plans/m3-framework-realignment.md`. On acceptance: move
accepted ADR(s) into `docs/decisions/`, rewrite `docs/architecture.md`'s affected sections,
implement ADR-0020 first (unblocks PR 4), rebuild `docs/plans/pr4-notional-aero-turbofan.md`
against it, then ADR-0021 before PR 5.

## Tried / failed
Correction rounds: 0/2 used (no implementation this session). Failed integrations: 0/2.

## Honesty rules
"A trace matched" is not "verified". Verified means the acceptance command ran and exited 0 in this session.
