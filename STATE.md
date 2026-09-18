# STATE (rewritten each session, never appended; ≤ 40 lines; true at handoff)

## In flight
Workflow-v3, branch `workflow-v3`, base `origin/main` `40b484b`. This session's batch (R-004,
R-005, R-006, gate reordering, review-verification doc fixes) is committed and pushed to
`origin/workflow-v3` per Joe's explicit instruction. See `docs/reviews/workflow-v3-review.md`
(three rounds recorded; R-006's fix is not yet its own entry there — see this commit's message).

## Last verified result
(1) R-004/R-005 status normalized to canonical **fixed-unverified**; Codex verification still
not dispatched, per Joe's "stop after validation" instruction. (2) R-006 fixed: `guard-oracle.sh`
now tracks the command's actual working directory (its `cwd` field plus any literal `cd` inside
the command) before checking write targets, closing the `cd <guarded-dir> && <write>` bypass for
all three guarded categories; also normalizes Edit/Write `file_path` (`..` traversal). 21 new
`guard-oracle.cases`; `hooks-test` 77/77 (was 56). Documented gap: a `cd` to a variable/glob/
`~`/`-` is not followed. (3) `just check` reordered: format-check/docs-check/hooks-test/ruff
(build-independent) now run before the four preset builds; same steps/failure propagation,
verified via `just --dry-run check`. (4) `review-dispatch.md`'s fix-verification focus now
requires an explicit per-finding verdict (incl. rejections) plus a bounded fix-delta/callers/
invariants check reporting "New findings: none" or a structured list; `review-findings.md`'s
Completion field captures it. Full gate rerun after (2)+(3): all four presets 274/274,
`hooks-test` 77/77, `docs-check` OK (24 files), pyright 0 errors, pytest 32/32, `EXITSTATUS=0`,
`check: OK` (self-caught and corrected a double-backgrounding slip mid-run). `docs-check`/pytest
reverified clean after (4) (doc-only, no rebuild needed).

## Next step
Dispatch fresh Codex general + adversarial review of everything since `b2ef1ac` (now pushed),
supplying R-004/R-005/R-006's dispositions and requesting the new-findings result this session's
own doc fix requires. Add R-006 as its own recorded finding in `docs/reviews/workflow-v3-review.md`.
Then `/human-review` packet. After acceptance, resume `PLAN.md`'s M2 PR 3b; recheck `main` first.

## Tried / failed
Correction rounds: 2/2 — budget exhausted (round 1: D1-D4/D6-D8 verified clean; round 2:
R-002/R-003/D5/cleanup/tokens.py verified clean but surfaced R-006). This batch is explicitly
Joe-authorized beyond the exhausted budget, not an autonomous 3rd round — none of it has been
through Codex review yet. Failed integrations: 0/2. A mid-run double-backgrounding slip was
self-caught before being reported as a result; no commit affected.

## Honesty rules
"A trace matched" is not "verified". Verified means the acceptance command ran and exited 0 in this session.
