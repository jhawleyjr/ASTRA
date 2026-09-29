# STATE (rewritten once, at /wrap, never appended; ≤ 40 lines, says nothing that changes at merge)

## In flight
Plugin-test guard (Tier 2), M5 "PR 13": PR #75, branch `worktree-plugin-test-guard` off `origin/main` `c0c0439`.
Scope: `astra_finalize_plugins()` (`cmake/AstraPlugin.cmake`) fails configure, shared and static, for an
executable compiled with `ASTRA_TEST_PLUGIN_DIR` that is not registered with `astra_add_plugin_test()`, naming
each target and its directory. It also covers `tests/embedded`'s `astra_autopilot_process_tests` (registered),
a configure-only regression in `tests/python/test_new_aircraft_build.py`, and PLAN.md (the open item removed,
the PR 13 line added). Stage: implementation, the full gate and the Codex general pass are complete for the
candidate. Human review is next (step 10), and the PR is a draft. No plan file is committed (Next step 2); the
PR body carries the plan's decisions and what the check cannot see.

## Last verified result
Run this session: `./scripts/check-full.sh` from a fresh configure on `cb2853c`, exit 0, `check: OK`
(`docs/reviews/evidence/cb2853c-gate-summary.log`). CTest 621/622/623/624 per preset, the same as PR 12's gate;
pytest 473 (471 plus the two new cases). Also run this session:
- the known case K (the check fired on `astra_autopilot_process_tests` alone, before it was registered);
- three scratch mutations, each seen to fire and each restored: M1 shared and static, M2, M3 (the pytest);
- the Codex general pass on `c0c0439..d4895d5`: no findings. The thread id and the table are in the PR body.
`d4895d5` adds only the evidence file. Later commits add only this file and `docs/lessons.md`.

## Next step
1. Joe: decide on PR #75, including whether an adversarial Codex pass is wanted (only the general pass ran).
2. Joe: the plan file. The auto-mode classifier refused writing `docs/plans/plugin-test-guard.md` with its
   approval status line, so PLAN.md's line says "Plan: in the PR body". If Joe adds the file or allows the write,
   relink PLAN.md's PR 13 line to it (a docs-only change: `just docs-check`).
3. No GitHub CI until Oct 1: Joe is out of Actions minutes and has turned off the required check in the ruleset.
   Don't run the `gh pr ready --undo` / `gh pr ready` rerun, and don't treat a red or missing check as a blocker.
   The local ./scripts/check-full.sh is the evidence. On or after Oct 1, restore the required check when Joe says to.
4. Then: M6 planning is Joe's to launch. Open items with owners are in PLAN.md.

## Tried / failed
The plan-file write was refused by the auto-mode classifier and not retried (lesson 2026-09-29). CI `check`
was not run (no Actions minutes). No correction rounds and no failed integrations; the counts are in the PR body.

## Honesty rules
"A trace matched" is not "verified". Verified means the acceptance command ran and exited 0 in this session.
