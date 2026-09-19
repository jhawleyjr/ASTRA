# Review: `gravity/inverse_square` centripetal-term fix (case05 gravity anomaly)

Candidate: working-tree diff, `worktree-m2-case05-gravity` (uncommitted at review time).
Spec/plan: `docs/plans/nesc-case05-gravity-centripetal.md` (approved by Joe, 2026-09-19).
Scope reviewed: `git diff --scope working-tree` — `plugins/physical/gravity/inverse_square/inverse_square.cpp`,
`tests/unit/gravity/inverse_square_test.cpp`, `tests/physics/nesc/case04_dropped_sphere_round_non_rotation.yaml`,
`tests/physics/nesc/case05_dropped_sphere_round_rotation.yaml`, `PLAN.md`, `docs/plans/nesc-harness.md`,
`docs/plans/nesc-case05-gravity-centripetal.md` (new, untracked).

## General review (Codex, `review --scope working-tree`)

Verdict: no actionable defects. "The correction matches the NED gravity contract and preserves
non-rotating behavior. All eight InverseSquareGravity tests passed using the existing ASan binary (exit
0, leak detection disabled); no rebuild or full gate was performed [by the reviewer — the builder's own
full-gate evidence is recorded in the plan/PLAN.md]."

## Adversarial review (Codex, `adversarial-review --scope working-tree`, tier-3 physics focus)

Focus: independent re-derivation of the centripetal correction's sign/frame convention (ADR-0001/
ADR-0003), cross-check against `wgs84_j2`'s own Groves-2.133 term and `eom.cpp`'s actual use of `g_n()`,
bit-identity of the default path, independence and tolerance soundness of the ECEF oracle test, ADR-0012
domain validity, per-frame allocation/exception constraints, honesty of the D1 residual's disclosure, and
plausibility of the reported R4/R8 dry-run numbers.

Verdict: **approve**. "No material defects found. Independently derived NED signs match the
implementation, WGS-84 precedent, and EOM contract. Component-wise addition preserves default bits; the
bit_cast test checks all components. The ECEF oracle and tolerance are sound. No new singularity,
allocation, or throwing path identified. D1 remains explicitly disclosed as authorized."

**One accuracy correction surfaced and fixed, not a defect in the code**: the plan/`PLAN.md`/
`nesc-harness.md` originally claimed case04's and case05's post-fix `t=30s` diffs "agree to 4 significant
figures." Codex's own independent recomputation found this an overclaim — the two values agree to within
~1% relative difference (e.g. `-0.014685` m vs. `-0.014545` m), not to 4 significant figures. The
underlying substantive claim (case05 landing at the same order of magnitude/tier-3 residual level as
case04, not three orders of magnitude away) is correct and unaffected; only the precision of the
"agreement" wording was wrong. Fixed in all three documents (this session, before this record was
written) rather than left standing.

## Completion

No critical/major findings. No fix-verification round needed (nothing was rejected or left open). Next
step: `/human-review` packet for Joe's decision.
