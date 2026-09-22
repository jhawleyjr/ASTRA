# STATE (rewritten each session, never appended; ≤ 40 lines; true at handoff)

## In flight
**M3 PR 8** (trimmed `cruise.yaml`, 300 s regression trajectory, phugoid cross-check). Tier 3.
Branch `worktree-m3-pr8-cruise-regression`, base `origin/main` `553700f`. Plan
`docs/plans/pr8-cruise-regression.md` Revision 2 (approved, five conditions met) plus the J-1/J-2 note.

Joe approved pre-capture `ed10734`. Commit B `3d0d3a6` holds the golden, captured at `4ce6a9f`
(four presets byte-identical, no halt). Commit C `bffc751` holds the golden test plus Joe's
J-1/J-2. **R-003 (the re-capture path fails to build once the golden is removed): fix authorized
by Joe, 2026-09-22, outside the correction budget; rounds stay 2/2.** The fix makes step 4 build
only the harness target, with a guard test on the real path. Packet:
`docs/reviews/pr8-cruise-regression-human.md`; findings:
`docs/reviews/pr8-cruise-regression-review.md`. The arming marker has been removed (checked absent).

## Last verified result
`./scripts/check-full.sh` at `bffc751`: `check: OK`, exit 0, 389/389 on each of four presets,
pytest 113 passed. The R-003 fix commit needs its own gate: see Next step.

## Next step
Full gate on the R-003 fix commit → Codex fix verification of the R-003 delta → packet update. On
green: push the branch, `gh pr create --draft`, `gh pr ready`, confirm GitHub's check on the pushed
head (all authorized by Joe, 2026-09-22). The squash-merge is Joe's.

## Tried / failed
Correction rounds: 2/2 (R-001, R-002). J-1/J-2 (Joe's findings) and R-003 (Joe-authorized) are
outside the budget. Failed integrations: 0/2.

## Honesty rules
"A trace matched" is not "verified". Verified means the acceptance command ran and exited 0 in this session.
