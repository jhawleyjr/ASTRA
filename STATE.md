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
`./scripts/check-full.sh` at `f3c1460` after `rm -rf build`: `check: OK`, exit 0, 389/389 × four
presets, pytest 115 passed, no manual target build (`docs/reviews/evidence/f3c1460-full-gate.log`).
Acceptance 1 and 3 evidence are in `docs/reviews/evidence/pr10-*`. Codex general pass on
`7a83a75..1fcd846`: no actionable defects (`docs/reviews/pr10-fresh-tidy-review.md`).

## Next step
Joe reviews `docs/reviews/pr10-fresh-tidy-human.md`, including finding 2 (the PR 4a history is
worded as "reachable", not "encountered"). Then push, `gh pr create --draft`, `gh pr ready`, once
authorized.

## Tried / failed
Correction rounds: 0/2. Failed integrations: 0/2.

## Honesty rules
"A trace matched" is not "verified". Verified means the acceptance command ran and exited 0 in this session.
