# STATE (rewritten each session, never appended; ≤ 40 lines; true at handoff)

## In flight
Workflow-v3, branch `workflow-v3`, main checkout, committed through `e081d88` (pushed to
`origin/workflow-v3`). This session found `STATE.md` still describing the Step 10/11/R-006
reconciliation as uncommitted after `e081d88` had already committed it — corrected here. On
top of `e081d88`, fixed two independently reproduced `guard-oracle.sh` bugs (R-007: `cd --`/
option parsing misread the option token as the directory, bypassing the guard; R-008:
read-only `sed` without `-i` was misclassified as a write) and reconciled the review doc's
R-001–003 Status fields (still "fixed-unverified" after Codex had verified them on `b2ef1ac`).
Uncommitted, pending Codex dispatch (see Next step).

## Last verified result
`just hooks-test`: 92/92 (up from 77; 15 new R-007/R-008 lines), rerun fresh this session.
`just docs-check`: OK, 24 files, rerun fresh after each doc edit. Full local gate
(`./scripts/check-full.sh`) run to completion this session on the corrected candidate:
`EXITSTATUS=0`, `check: OK`, all four presets 274/274, elapsed 4:25.61 — retained verbatim at
`docs/reviews/evidence/round4-full-gate.log`. The prior `1a12623` full-gate result (same
verdict) previously existed only in gitignored `build/validation/full.log`; retained at
`docs/reviews/evidence/1a12623-full-gate.log` before this session's run overwrote it. No Codex
review has been dispatched yet for R-004 through R-008 or for the Step 10/11/Step-12 changes in
`e081d88` — none of the five open findings, nor `e081d88` itself, has Codex verification.

## Next step
Commit this round's changes (`guard-oracle.sh`, `guard-oracle.cases`, `workflow-v3-review.md`,
`docs/reviews/evidence/`, this file), then dispatch fresh Codex general review of the complete
candidate (base `origin/main`, head this commit) plus adversarial fix-verification naming
R-004 through R-008 by ID (`docs/agents/review-dispatch.md`'s recipe). Record verdicts in
`workflow-v3-review.md`; fix confirmed new defects within the remaining bounded budget and
rerun affected checks (full gate too, if product code changes). Then prepare Joe's
`/human-review` packet and run `/wrap`. Do not resume `PLAN.md`'s M2 PR 3b while gates are open.

## Tried / failed
Correction rounds: 2/2 — budget exhausted (unchanged; see `workflow-v3-review.md`'s Completion
section for the full history). This session's guard-oracle fixes (R-007/R-008) and the
STATE/review-doc reconciliation are explicitly Joe-authorized beyond that exhausted budget, per
this request's own text — not a 3rd autonomous round. Failed integrations: 0/2.

## Honesty rules
"A trace matched" is not "verified". Verified means the acceptance command ran and exited 0 in this session.
