# STATE (rewritten once, at /wrap, never appended; ≤ 40 lines; says nothing that changes at merge)

## In flight
M5 PR 10 (Tier 2): PR #72 (draft), branch `worktree-m5-pr10-cleanup` off `origin/main` `b1cae7f`, plan
`docs/plans/m5-pr10-cleanup.md` (approved by Joe with three additions). Scope: the wall-clock symbol scan
(`scripts/wallclock_scan.py`, `just wallclock-scan`, in `just check`, as anchored families), `astra.fgbridge`
`pace=True` by default, Follow-up A's comment fixes, PLAN.md and architecture/validation doc updates.
Stage: implementation, full gate and Codex review complete for the candidate; human review is next. The PR body
records Joe's third-round exception and the stop rule he set for the last Codex pass.

## Last verified result
Run this session at `46185cb`: `./scripts/check-full.sh` exit 0, `check: OK`
(`docs/reviews/evidence/46185cb-gate-summary.log`); `.venv/bin/pytest tests/python -k "wallclock or fgbridge"`
exit 0. Later commits add only gate summaries, PLAN.md and STATE.md. Fresh-configure scan, default-target
check, mutations and every Codex thread id are in the PR body.

## Next step
1. Joe: decide on PR #72. Before merge, rerun CI's `check` with `gh pr ready --undo`, then `gh pr ready`.
   The last Codex pass found `epoll_pwait2`, `aio_suspend` and `recvmmsg` uncovered; per Joe's stop rule that is
   a follow-up (PLAN.md open item "Wall-clock scan, timed I/O waits"), not a reopened round. Say so if you would
   rather have it fixed in this PR.
2. Then: M6 planning is Joe's to launch.

## Tried / failed
Three correction rounds (two under the standing budget, the third by Joe's exception) each followed by a full
gate and a Codex verification; the counts and findings F1 to F4 are in the PR body. A lesson is in
`docs/lessons.md` (2026-09-28). No failed integrations.

## Honesty rules
"A trace matched" is not "verified". Verified means the acceptance command ran and exited 0 in this session.
