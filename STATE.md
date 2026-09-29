# STATE (rewritten once, at /wrap, never appended; ≤ 40 lines, says nothing that changes at merge)

## In flight
Plugin-test guard (Tier 2), M5 "PR 13": PR #75, branch `worktree-plugin-test-guard` off `origin/main` `c0c0439`.
Plan `docs/plans/plugin-test-guard.md` (approved by Joe in plan mode, no adjustments). Scope:
`astra_finalize_plugins()` (`cmake/AstraPlugin.cmake`) fails configure, shared and static, for an executable
compiled with `ASTRA_TEST_PLUGIN_DIR` that is not registered with `astra_add_plugin_test()`, naming each target
and its directory. `tests/embedded`'s `astra_autopilot_process_tests` is registered. There is a configure-only
regression in `tests/python/test_new_aircraft_build.py`, and PLAN.md has the PR 13 line with the open item
removed. Stage: implementation, the full gate, the Codex general pass and Joe's Cowork review are complete for
the candidate. Human review is next (step 10); the PR is a draft.

## Last verified result
Run this session: `./scripts/check-full.sh` from a fresh configure on `cb2853c`, exit 0, `check: OK`
(`docs/reviews/evidence/cb2853c-gate-summary.log`). CTest 621/622/623/624 per preset, the same as PR 12's gate.
Pytest 473, which is 471 plus the two new cases. Also run this session:
- the known case K and three scratch mutations (M1 shared and static, M2, M3), each seen to fire and each
  restored;
- the Codex general pass on `c0c0439..d4895d5`: no findings, thread id in the PR body.
Joe's Cowork session reported a scratch CMake project review with no findings; the cases are in the PR body.
Every commit after `cb2853c` is docs-only (evidence, plan file, PLAN.md, `docs/lessons.md`, this file).
`just docs-check` exits 0 on `7f64aff`. The full gate was not rerun for them, per Joe.

## Next step
1. Joe: decide on PR #75 (approval of the candidate). No adversarial Codex pass: Joe did not request one.
2. No GitHub CI until Oct 1: Joe is out of Actions minutes and has turned off the required check in the ruleset.
   Don't run the `gh pr ready --undo` / `gh pr ready` rerun, and don't treat a red or missing check as a blocker.
   The local ./scripts/check-full.sh is the evidence. On or after Oct 1, restore the required check when Joe says to.
3. Then: M6 planning is Joe's to launch. Open items with owners are in PLAN.md.

## Tried / failed
The auto-mode classifier refused the first plan-file write. It was not retried until Joe instructed it directly
in session (lesson 2026-09-29). CI `check` was not run (no Actions minutes). No correction rounds and no failed
integrations; the counts are in the PR body.

## Honesty rules
"A trace matched" is not "verified". Verified means the acceptance command ran and exited 0 in this session.
