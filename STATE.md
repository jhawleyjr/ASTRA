# STATE (rewritten once, at /wrap, never appended; ≤ 40 lines; says nothing that changes at merge)

## In flight
M5 PR 10 (Tier 2): PR #72 (draft), branch `worktree-m5-pr10-cleanup` off `origin/main` `b1cae7f`, plan
`docs/plans/m5-pr10-cleanup.md` (approved by Joe with three additions). Scope: the wall-clock symbol scan
(`scripts/wallclock_scan.py`, `just wallclock-scan`, in `just check`), `astra.fgbridge` `pace=True` by default,
Follow-up A's comment fixes, PLAN.md and architecture/validation doc updates. Stage: blocked at step 9 (fix and
retest): the two autonomous correction rounds are used and Codex's last verification pass raised a third finding.

## Last verified result
Run this session at `5528eb2`: `./scripts/check-full.sh` exit 0, `check: OK`
(`docs/reviews/evidence/5528eb2-gate-summary.log`); the same held at `4ea1900` and `a2e8546`. `04789bf` (branch
head when the PR body was written) adds only a gate summary. Fresh-configure scan, default-target check, the
scan and pacing mutations, and every Codex thread id are in the PR body. Not run: anything after `5528eb2`
(no code has changed since).

## Next step
1. Joe: decide F3, the open Codex finding: `ualarm` is not flagged (reproduced with an `nm` probe). Options in the
   PR body: a third correction round (one symbol plus a compiled case in `tests/python/test_wallclock_scan.py`, then
   the full gate and one Codex verification), accept as is, or the approved-set redesign of the scan.
2. Claude, once Joe decides: apply the fix if authorized, rerun `./scripts/check-full.sh`, commit its summary,
   dispatch one fix verification (`docs/agents/review-dispatch.md`), update the PR body.
3. Joe: decide on PR #72. Before merge, rerun CI's `check` with `gh pr ready --undo`, then `gh pr ready`.
4. Then: M6 planning is Joe's to launch.

## Tried / failed
Two correction rounds (the counts and findings F1 to F3 are in the PR body), each followed by a full gate and a
Codex verification. The round-2 fix added the neighboring families as well; `ualarm` still escaped. A lesson is
in `docs/lessons.md` (2026-09-28). No failed integrations.

## Honesty rules
"A trace matched" is not "verified". Verified means the acceptance command ran and exited 0 in this session.
