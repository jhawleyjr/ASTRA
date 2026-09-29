# STATE (rewritten once, at /wrap, never appended; ≤ 40 lines, says nothing that changes at merge)

## In flight
M5 PR 12 (Tier 2): PR #73, branch `worktree-plugin-test-helper` off `origin/main` `ffb395a`, with `origin/main`
`7320977` (#74) merged in (no rebase). Plan `docs/plans/plugin-test-helper.md` (approved by Joe with four
adjustments, all applied). Scope: `astra_add_plugin_test()` in `cmake/AstraPlugin.cmake`, the hardcoded test list
removed and every target migrated, `aircraft/_template/tests/` (the template's own tests, using the helper),
`tests/python/test_new_aircraft_build.py` (copy the template, build clean, run its tests; shared and static),
architecture §18, PLAN.md (this PR is "PR 12", plus one new open item).
Stage: implementation, full gate, the Codex general pass and the integration of #74 are complete for the
candidate; human review is next. The PR is marked ready; CI's `check` is not a gate until Oct 1 (Next step).

## Last verified result
Run this session: `./scripts/check-full.sh` from a fresh configure on `b8e3ab4`, exit 0, `check: OK`
(`docs/reviews/evidence/b8e3ab4-gate-summary.log`); the acceptance test, three scratch mutations and the
Codex pass (thread id in the PR body). After the merge, on `ac9d92c`: `just docs-check` exit 0,
`./scripts/check-fast.sh` exit 0 (508 of 508), and a comment-only equivalence check of the six code files #74
touched (all identical once comments and docstrings are removed). NOT rerun on `ac9d92c` or later: the full gate;
the PR body says why. Later commits add only this file and `docs/lessons.md`.

## Next step
1. No GitHub CI until Oct 1: Joe is out of Actions minutes and has turned off the required check in the ruleset.
   Don't run the gh pr ready --undo / gh pr ready rerun, and don't treat a red or missing check as a blocker. The
   local ./scripts/check-full.sh is the evidence. On or after Oct 1, restore the required check when Joe says to.
   (CI run 175 on `ac9d92c` failed with "The job was not started because recent account payments have failed"; no
   step ran.)
2. Joe: decide on PR #73. It ran only the general Codex pass; say if an adversarial pass is wanted.
3. Then: M6 planning is Joe's to launch. Open items with owners are in PLAN.md, including the new one (a
   shared-build test executable that omits `astra_add_plugin_test()` is caught only by a targeted or clean build).

## Tried / failed
CI `check` did not start (out of Actions minutes), so no CI result exists for any head of this PR. A mutation anchor check I
first used proved nothing (two lessons appended, 2026-09-29). No correction rounds and no failed integrations;
the counts are in the PR body.

## Honesty rules
"A trace matched" is not "verified". Verified means the acceptance command ran and exited 0 in this session.
