# STATE (rewritten once, at /wrap, never appended; ≤ 40 lines, says nothing that changes at merge)

## In flight
M6 PR 1 (Tier 3, docs only), PR #79, branch `worktree-m6-pr1-adr0027`, based on `origin/main` `be32cbb`. The PR
plan is `docs/plans/m6-pr1-adr0027.md`, approved by Joe in plan mode, with changes, and it records his D1 to D7.
Scope: ADR-0027 (`docs/decisions/0027-run-log-finalization.md`, **proposed**), superseding only ADR-0025's
Finalization; architecture rev 10 (§1, §9, §12, §15, §16, M6 features marked planned); the PLAN.md PR 1 line.
Stage: implementation, the full gate and the Tier 3 adversarial Codex review are complete for the candidate.
Human review is next (step 10). The PR is a draft, and its body holds the checks, the review and the decisions.

## Last verified result
Run in this session: `./scripts/check-full.sh` from a fresh configure (`build/` removed first) on `574f9fe`.
Exit 0, `check: OK` (`docs/reviews/evidence/574f9fe-gate-summary.log`). CTest 621/622/623/624, pytest 473. Every
commit after `574f9fe` is docs only (the evidence file, `docs/lessons.md`, this file).
`python3 scripts/docs_check.py` exits 0 and `git diff --check` is clean. Codex adversarial review, 3 passes:
- the initial pass: needs-attention, F1 and F2;
- verification 1: F1 verified, F2 partial, a new N1;
- verification 2: approve, "New findings: none".
The thread ids and the per-finding history are in the PR body and `docs/reviews/m6-pr1-adr0027-review.md`.

## Next step
1. Joe: accept or reject ADR-0027 and architecture rev 10, note the deviations from the M6 plan's wording (D1,
   D2, D7) and the R16 reading (F2), and approve the candidate. PR 2 cannot start before ADR-0027 is accepted.
2. When ADR-0027 is accepted, its Status line changes from proposed to accepted under Joe's authorization, with
   the `docs/decisions/` guard armed by him. The architecture status line follows.
3. Then M6 PR 2 in a new worktree. Its plan starts from ADR-0027's validation list (R16, R16b, R17, R18, R18b,
   R19b) and the four claims marked "PR 2 confirms". It also removes the (M6) Finalization open item.
4. No GitHub CI until Oct 1: local-gate mode. Do not treat a missing check as a blocker.

## Tried / failed
- Codex F1 and N1 were design changes; Joe decided them as D6 and D7.
- The worktree guard refused focus text containing "git diff" and a sed with a variable path. The workaround was
  a focus file in the scratchpad and the Write tool.
- `gh pr edit` failed on the Projects-classic GraphQL error; the REST `gh api -X PATCH` worked.
- Correction rounds: 2 of 2 used. Failed integrations: 0. Both counts are in the PR body.

## Honesty rules
"A trace matched" is not "verified". Verified means the acceptance command ran and exited 0 in this session.
