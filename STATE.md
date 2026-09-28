# STATE (rewritten once, at /wrap, never appended; ≤ 40 lines; says nothing that changes at merge)

## In flight
M5 PR 8 (Tier 2): PR #70 (draft), branch `worktree-m5-pr8-fgbridge` off `origin/main` `3b5ca4a`, plan
`docs/plans/m5-tooling.md` PR 8 row (Q21 to Q23, R18 to R20) plus Joe's plan-mode amendments (header-silent
units follow FGFDM2Props; v_body from the Earth-relative v_b; floats within 1 float32 ulp; decoded-only
cross-field checks; the pilot-acceleration source traced in `/home/joe/repos/flightgear-2024.1.7/src/FDM`).
Stage: human review; the PR body is the description Joe reviews. Code candidate `5d51da9`; `4856b3b` adds only
its gate summary, and the next commit only this handoff. Scope: `python/astra/fgbridge.py`,
`tests/python/test_fgbridge.py`, `tests/golden/flightgear/fgnetfdm_v24.py` (the one golden path Joe authorized),
`scripts/fgnetfdm_layout_check.py` (by hand, never in `just check`), a validation.md row pair, PLAN.md's PR 8
line, `docs/reviews/m5-pr8-fgbridge-review.md`. No `core/` change, no new dependency.

## Last verified result
This session, at `5d51da9`: `./scripts/check-full.sh` exit 0 with `check: OK`
(`docs/reviews/evidence/5d51da9-gate-summary.log`; pytest 362 passed in 44.79 s). Acceptance
`.venv/bin/pytest tests/python -k fgbridge` exit 0, 29 passed. `python3 scripts/fgnetfdm_layout_check.py` exit 0
(sizeof 408, 58 members, every offsetof agrees; a wrong-offset copy exits 1). 20 scratch mutations, all killed,
including three where the test's mapping shares the packer's error and only the cross-field checks remain.
Codex: general and adversarial reviews found R-001 (the live bridge never drained the run log); fixed in two
rounds; the second verification approved with "New findings: none". Thread ids are in the PR body.

## Next step
1. Joe: decide on PR #70. "Needs Joe's decision" covers approval of `5d51da9`, the golden table's provenance,
   oracle findings 1 to 7 (including what FlightGear's pilot acceleration means in JSBSim and YASim), the
   ACCEL_INVALID zero packet at frame 0, and the live source's first-frame flat-Earth refusal. Correction
   rounds are used up (2 of 2): a new defect goes to Joe. Before merge, rerun CI's `check` with
   `gh pr ready --undo`, then `gh pr ready`.
2. M5 PR 9: closeout (check-off with evidence links, architecture §12 status, open items for the CLI, recorder
   retirement, wind events, closed-loop scenarios).

## Tried / failed
Correction and integration counts: PR #70's body holds them. The first candidate drove `Simulation.step()`
without draining the log (only `run()` drains), found by both Codex passes; the first fix drained after a
successful publish only, so a send error or refusal skipped it (lessons 2026-09-28). One gate at `6bb2cfc` was
stopped by SIGTERM once superseded. The worktree guard refused `sed` with a variable argument and a Python
heredoc that named git; Edit/Write and scripts run by path work. `gh api -X PATCH .../pulls/70 -F body=@file`.

## Honesty rules
"A trace matched" is not "verified". Verified means the acceptance command ran and exited 0 in this session.
