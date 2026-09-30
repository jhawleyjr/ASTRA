# STATE (rewritten once, at /wrap, never appended; ≤ 40 lines, says nothing that changes at merge)

## In flight
M6 PR 1 (Tier 3, docs only), PR #79, draft, branch `worktree-m6-pr1-adr0027`, base `origin/main` `be32cbb`.
PR plan: `docs/plans/m6-pr1-adr0027.md` (Joe's D1 to D7). ADR-0027 (`docs/decisions/0027-run-log-finalization.md`,
superseding only ADR-0025's Finalization) and architecture rev 10 were **accepted by Joe on 2026-09-29**.
Stage: human review done (step 10). Every gate the tier requires is met, or waived by Joe. The PR body holds the
checks, the review, the waivers and the decisions.

## Last verified result
Run this session: `./scripts/check-full.sh` from a fresh configure (`build/` removed first) on `574f9fe`: exit 0,
`check: OK` (`docs/reviews/evidence/574f9fe-gate-summary.log`); CTest 621/622/623/624, pytest 473.
Every commit after `574f9fe` is docs only: the evidence file, STATE and two lessons; `c11c41d` (Joe's
human-review wording); `3371d07` (ADR-0027 and rev 10 accepted, "(proposed)" dropped from PLAN.md).

`python3 scripts/docs_check.py` exit 0 and `git diff --check` clean on `c11c41d` and on `3371d07`. For both
commits Joe waived a full-gate rerun and a further Codex pass. The Codex adversarial review (3 passes, final
verdict approve, no new findings) covers the text through `574f9fe`. The thread ids are in the PR body and in
`docs/reviews/m6-pr1-adr0027-review.md`.

## Next step
1. Next task: M6 PR 2 (Tier 3), ADR-0027's implementation, in a new worktree off `origin/main` once #79 is
   in `main`. Its plan starts from ADR-0027's validation list (R16, R16b with the three `.partial`-replacement
   cases, R17, R18, R18b with the `discard_log()` cases, R19b) and the four "PR 2 confirms" claims (chunk index
   type, `frame` column for halted runs, `load(log.path)` while the `Simulation` is alive, `kCoreApiVersion`
   unchanged). PR 2 also removes the (M6) Finalization open item from PLAN.md. PR 4 (units) and PR 9 (benchmark) depend
   only on PR 1 (the M6 plan's decomposition).
2. No GitHub CI until Oct 1 (local-gate mode). Do not treat a missing check as a blocker.

## Tried / failed
Codex F1 and N1 were design changes, decided by Joe as D6 and D7. The worktree guard refused focus text
containing "git diff" and a sed with a variable path; the workaround was a scratchpad focus file and the Write
tool. `gh pr edit` failed on Projects classic, so the body was updated with `gh api -X PATCH`. Correction rounds:
2 of 2; failed integrations: 0 (PR body).

## Honesty rules
"A trace matched" is not "verified". Verified means the acceptance command ran and exited 0 in this session.
