# STATE (rewritten each session, never appended; ≤ 40 lines; true at handoff)

## In flight
Workflow-v3, branch `workflow-v3`, main checkout, committed through `e115c75` (pushed). Fixed
R-007/R-008 (`guard-oracle.sh` `cd --`/option parsing and read-only-`sed` bugs), reconciled
STATE.md's stale "uncommitted" claim and the review doc's R-001–003 Status fields, dispatched
fresh Codex review of `e115c75`: general clean; adversarial fix-verification confirmed R-004/
R-005 but found R-006/R-007/R-008 each still incomplete (4 new findings, R-009–R-012 — pipeline
`cd` not tracked, clustered `cd` options like `-Pe` unrecognized, sed `w`/`W` write commands
missed, `-e`'s attached script text misread as `-i`). All four reproduced directly against the
pre-fix hook, fixed, and covered by 12 new regression cases. Uncommitted, pending one final
Codex dispatch (see Next step) — this batch's fix-verify cycle is not open-ended.

## Last verified result
`just hooks-test`: 104/104 (up from 92), rerun fresh. `just docs-check`: OK, 24 files. Full
local gate rerun twice this session on each successive candidate: after R-007/R-008
(`EXITSTATUS=0`, `check: OK`, 274/274×4, elapsed 4:25.61, `docs/reviews/evidence/
round4-full-gate.log`) and again after R-009–R-012 (`EXITSTATUS=0`, `check: OK`, 274/274×4,
elapsed 4:20.61, `docs/reviews/evidence/round4-fixverify-full-gate.log`). Codex reviewed
`e115c75` (general + adversarial, both retained at `docs/reviews/evidence/
e115c75-codex-*.log`); R-009–R-012 have no Codex pass yet.

## Next step
Commit this fix-verification batch (`guard-oracle.sh`, `guard-oracle.cases`,
`workflow-v3-review.md`, `docs/reviews/evidence/`, this file) and push. Dispatch one focused
Codex adversarial fix-verification naming R-006 through R-012 against the new head (per
`docs/agents/review-dispatch.md`'s recipe); record verdicts. Per this request's own bound
("does not permit unlimited correction rounds"), this closes Round 4 regardless of outcome —
an open finding after that pass is recorded and handed to Joe, not chased further. Then prepare
Joe's `/human-review` packet and run `/wrap`; do not resume `PLAN.md`'s M2 PR 3b meanwhile.

## Tried / failed
Correction rounds: 2/2 — budget exhausted (unchanged; see `workflow-v3-review.md`'s Completion
section). This session's entire Round 4 (R-007 through R-012, plus the STATE/review-doc
reconciliation) is the one bounded fix batch this request authorized beyond that budget — not a
3rd autonomous round, and the Codex-found R-009–R-012 fixes are this same batch's fix-verify
cycle, not a new round. Failed integrations: 0/2.

## Honesty rules
"A trace matched" is not "verified". Verified means the acceptance command ran and exited 0 in this session.
