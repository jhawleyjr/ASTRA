# STATE (rewritten once, at /wrap, never appended; ≤ 40 lines, says nothing that changes at merge)

## In flight
M6 PR 3 (Tier 2), the `astra` CLI (`astra run`, `astra trim`): PR #81, draft, branch `worktree-m6-pr3-cli`, base
`origin/main` `da52239` (#80). Plan: `docs/plans/m6-pr3-cli.md` (approved by Joe in plan mode with J1 to J6).
Stage: human review (step 10), second look. Joe reviewed `4bb7607` and asked for three changes; all three are in
the code candidate `e1a8dec` (the review file updated; H-1, `astra trim` catching only `astra.trim`'s two expected
failures; two PLAN.md open items). Later commits are evidence and this handoff only. Every gate the tier requires
is met on `e1a8dec`. The PR body holds the checks, the R14 test map, J4's evidence, the review and D1 to D8.

## Last verified result
Run this session:
- `./scripts/check-full.sh` from a fresh configure (`build/` removed first) on `e1a8dec`: exit 0, `check: OK`,
  1035 s (`docs/reviews/evidence/e1a8dec-gate-summary.log`); CTest 649/650/651/652, pytest 579 passed, 5 skipped.
- `.venv/bin/pytest tests/python -k cli` on `e1a8dec`: 72 passed.
- Codex: pass 1 (`01a0f374-1ac6-77a0-ad62-3026dd421a53`) needs-attention, R-001 and R-002; their verification
  (`01a0f385-55b8-7853-9bb3-db5d7ec6efc4`) approve; H-1's verification (`01a0f3d9-81c1-74d2-a276-f740635a9d5f`,
  `5b8305f` to `e1a8dec`) approve, new findings none. `docs/reviews/m6-pr3-cli-review.md` still shows H-1 as
  pending (no record-only update); the PR body holds its verification.
- The YAML 1.1 readings behind the new (M6) open item were probed on this build (on/off/yes/no, 010, 1:30, 1_000).

## Next step
1. Joe reads the H-1 delta (`4bb7607..e1a8dec`) and decides approval of the candidate and D1 to D8 (PR body).
2. Before merge, rerun CI's `check` with `gh pr ready --undo` then `gh pr ready` (local gate mode).
3. Next M6 task: PR 4 (units, Tier 3) or PR 9 (benchmark) depend only on PR 1; PR 5 (plots) needs PR 3 and PR 4.

## Tried / failed
- Correction rounds and failed integrations are in the PR body (1 of 2; 0). H-1 is Joe's change, not a round.
- The worktree guard refused compound commands, heredocs with escapes, `$(...)` arithmetic and the main
  checkout's venv; the workaround was scripts in the scratchpad (the 2026-09-27 lesson).
- `gh pr edit --body-file` failed on the classic-Projects GraphQL error; the REST PATCH worked (2026-09-29 lesson).
- A `q_nb` variant for J4 was dropped: the core does not read `initial.state.q_nb`, so that config is invalid.
- A real trim non-convergence was not found (1 m/s and 20 m/s converge); exit 1 is shown by a real early stop
  (`xtol=0.1`, residual 3.2e-5) and the non-convergence mapping by a raise with `astra.trim`'s own message.

## Honesty rules
"A trace matched" is not "verified". Verified means the acceptance command ran and exited 0 in this session.
