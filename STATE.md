# STATE (rewritten each session, never appended; ≤ 40 lines; true at handoff)

## In flight
**M3 PR 4a** (ADR-0021: `DynamicsModel` interface + `RigidBody6DofDynamics`, `Model`'s five new
state virtuals, `Vehicle` two-phase init, combined state buffer). Plan approved by Joe, Revision 3:
`docs/plans/pr4a-dynamics-model.md`.

**R1/R1-Sim golden capture is done and the goldens are frozen**, committed through `ba383ab`.
`cross_preset_agreement: r1=true, r1sim=true` (all four presets), recorded in
`tests/golden/dynamics/rigid_body_6dof_r1_manifest.md`. Three capture attempts, all disclosed (full
history for the human-review packet, condensed here): (1) debug/release 1-ULP disagreement in a
runtime-computed sample, diagnosed (`-ffp-contract` expression-shape contraction, not FMA — no
`-march`/`-mfma` in this build) and fixed by hardcoding all five samples as literal bit patterns;
(2) re-run agreed and committed, but the generated R1-Sim header proved over-braced
(`-Wmany-braces-around-scalar-init`, fatal under `-Werror`) once actually compiled; fixed; (3) clean
re-run, both goldens compile under `-Werror`, committed as `ba383ab`. `docs/lessons.md` has the
verified-cause entry. **Joe removed `.claude/touch-goldens`; the guard is back on — `tests/golden/**`
untouched from here.**

## Last verified result
No full gate run yet this session (implementation not started). Carried over: PR #41's own full
gate `check: OK` on `8b77c24`, superseded by this session's own upcoming full-gate run once
implementation is complete.

## Next step
Implement the plan's "Mapping and sequence" table: write the R1/R1-Sim comparison tests first
(against the not-yet-existing `RigidBody6DofDynamics`/rewritten `Integrator`, so they fail to
compile until the implementation lands), then `DynamicsModel`, `RigidBody6DofDynamics` (retiring
`Eom`), `Model`'s five new virtuals, `kCoreApiVersion` 7→8, `wrench_source.hpp`'s purity-contract
amendment, `VehicleState`'s retype, `StateBuffer`, the rewritten `Integrator`, `Vehicle`'s two-phase
init/stage buffer/rigid-body signal publish, the `state_owner` test plugin, the coupling/duplicate/
missing-peer/R2/health-check tests, and `docs/architecture.md`'s status line. Fast checks while
iterating; full gate to a retained log under `docs/reviews/evidence/` on the candidate; Codex
general + adversarial review (Tier 3); human-review packet including the three-capture history above.

## Tried / failed
Correction rounds: 0/2. Failed integrations: 0/2.

## Honesty rules
"A trace matched" is not "verified". Verified means the acceptance command ran and exited 0 in this session.
