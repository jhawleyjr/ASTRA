# STATE (rewritten each session, never appended; ≤ 40 lines; true at handoff)

## In flight
**M3 PR 10** (fresh-checkout `tidy` fix). Tier 2 (build/gate behavior). Branch
`worktree-m3-pr10-fresh-tidy`, base `origin/main` `7a83a75`. No plan critique. Codex: one general
pass, plus fix verification if needed.

Outcome: `just check` passes from a fresh configure with no manual target build, proved by a
regression test. PLAN.md follow-up "`just check` fails at its own `tidy` step on a fresh checkout".

Plan first (`/plan-feature`, shown to Joe before any code): reproduce from a fresh
`cmake --preset asan` into a scratch build directory why `tests/capture/capture_cruise_regression`
(PR 8) fails `run-clang-tidy-18` while `tests/golden/dynamics/capture_r1_golden` (PR 4a, also
`EXCLUDE_FROM_ALL`) has passed; show the failure, both compile-database entries, and the missing
artifact by name. Fix preference: (1) a target property on both harnesses removing the missing-
artifact reference (e.g. `CXX_SCAN_FOR_MODULES OFF`); (2) `tidy` builds the harness targets first.
Not acceptable: excluding `tests/capture/` or `tests/golden/dynamics/` from tidy, or removing
`EXCLUDE_FROM_ALL`. If R1 has been broken on fresh checkouts since PR 4a, record it as a finding
and a correction to PR 9's lesson; do not widen scope.

Acceptance: (1) pytest regression (fresh configure + tidy's first invocation on the two harness TUs,
exit 0) passes, and fails against `7a83a75` with the fix reverted; (2) `./scripts/check-full.sh`
after `rm -rf build` gives `check: OK` with no manual target build, evidence retained; (3) both
capture scripts still build and run their harness (dry-run to scratch, unarmed refusal untouched,
nothing under `tests/golden/**` written); (4) full gate green.

## Last verified result
Acceptance (1): regression passes on the candidate (8.0 s); fails at `7a83a75` + test only, with the
manual fresh-configure/no-build/tidy sequence exit 1 (`docs/reviews/evidence/pr10-acceptance1-revert-
7a83a75.log`). Acceptance (3): harness builds and runs to scratch, cruise script refuses unarmed,
`tests/golden/**` untouched (`docs/reviews/evidence/pr10-acceptance3-harness.log`); R1 script's
refusal from a clean tree still to record. Plan Revision 2 approved by Joe with changes.

## Next step
`rm -rf build`, `./scripts/check-full.sh` (acceptance 2/4), R1 clean-tree refusal, Codex general
pass (`7a83a75..HEAD`), `/human-review` packet (finding: PR 4a's de-wiring, see plan decisions).

## Tried / failed
Correction rounds: 0/2. Failed integrations: 0/2.

## Honesty rules
"A trace matched" is not "verified". Verified means the acceptance command ran and exited 0 in this session.
