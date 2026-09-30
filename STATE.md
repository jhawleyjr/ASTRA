# STATE (rewritten once, at /wrap, never appended; ≤ 40 lines, says nothing that changes at merge)

## In flight
M6 PR 3 (Tier 2), the `astra` CLI (`astra run`, `astra trim`): PR #81, draft, branch `worktree-m6-pr3-cli`, base
`origin/main` `da52239` (#80). Plan: `docs/plans/m6-pr3-cli.md` (approved by Joe in plan mode with J1 to J6).
Stage: human review (step 10). Code candidate `4bb7607`; later commits are evidence and this handoff only. Every
gate the tier requires is met: the acceptance command, the fresh full gate, and a Codex review with its fix
verification. The PR body holds the checks, the R14 row-by-row test map, J4's fragment evidence, the review,
decisions D1 to D8 and the follow-ups.

## Last verified result
Run this session:
- `./scripts/check-full.sh` from a fresh configure (`build/` removed first) on `4bb7607`: exit 0, `check: OK`,
  972 s (`docs/reviews/evidence/4bb7607-gate-summary.log`); CTest 649/650/651/652, pytest 575 passed, 5 skipped.
- `.venv/bin/pytest tests/python -k cli` on `4bb7607`'s content: 68 passed.
- J4: `da52239`'s `apply_trim` and the new one give identical dicts and key order for `cruise.yaml` and
  `cruise_still_air.yaml` (as is, perturbed, blocks removed); script and output in the PR body.
- Codex: pass 1 (`01a0f374-1ac6-77a0-ad62-3026dd421a53`) needs-attention, R-001 and R-002, both fixed in
  `4bb7607`; the verification (`01a0f385-55b8-7853-9bb3-db5d7ec6efc4`) approve, new findings none.
  Findings file: `docs/reviews/m6-pr3-cli-review.md`.

## Next step
1. Joe reads the diff and tests, and decides approval of the candidate and D1 to D8 (PR body, "Needs Joe's
   decision"), plus whether the two follow-ups get open items: the YAML 1.1 `off` reading in the tools'
   `safe_load` → `from_dict` route, and §18 step 4 on a template copy (`earth/flat` refused by `astra.trim`).
2. Before merge, rerun CI's `check` with `gh pr ready --undo` then `gh pr ready` (local gate mode).
3. Next M6 task: PR 4 (units, Tier 3) or PR 9 (benchmark) depend only on PR 1; PR 5 (plots) needs PR 3 and PR 4.

## Tried / failed
- Correction rounds and failed integrations are in the PR body (1 of 2; 0).
- The worktree guard refused compound commands and heredocs with escapes, and running the main checkout's venv;
  the workaround was scripts in the scratchpad (the 2026-09-27 lesson).
- `gh pr edit --body-file` failed on the classic-Projects GraphQL error; the REST PATCH worked (2026-09-29 lesson).
- A `q_nb` variant for J4 was dropped: the core does not read `initial.state.q_nb`, so that config is invalid.
- A real trim non-convergence was not found (1 m/s and 20 m/s still converge); exit 1 is shown by a real
  early stop (`xtol=0.1`, residual 3.2e-5) and the non-convergence mapping by a monkeypatched raise.

## Honesty rules
"A trace matched" is not "verified". Verified means the acceptance command ran and exited 0 in this session.
