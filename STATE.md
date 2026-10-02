# STATE (rewritten once, at /wrap, never appended; ≤ 40 lines, says nothing that changes at merge)

## In flight
M6 PR 5, plots: PR #89, draft, branch `worktree-m6-pr5-plots`, base `origin/main` `468c3d7`. Plan:
`docs/plans/m6-pr5-plots.md`, approved by Joe in plan mode with revisions 1 to 4. Tier 2. Stage: human review
(step 10).
Done:
- `astra.plot` (YAML packages, both renderers, the escape hatch by module or by file).
- The framework, template and notional-transport packages; `astra plot`; matplotlib and plotly pins.
- R1 to R4 and R3b tests; the docs, with the branch-scope docs step listed in the PR body's Docs impact.

## Last verified result
Run this session:
- `rm -rf build && ./scripts/check-full.sh` on `e87ff45`: exit 0, `check: OK`, 658/659/660/661 CTest passed,
  pytest 1313 passed / 5 skipped ([summary](docs/reviews/evidence/e87ff45-gate-summary.log)).
- `.venv/bin/pytest tests/python -k plot`: 109 passed. Mutation run: 12 of 12 mutants killed (PR body).
- Codex `review --base origin/main` on `4f4dc4c`: no actionable defects
  (thread `01a0fa13-22b6-7260-8f02-f8bfb6e10122`).
- `python3 scripts/docs_check.py`: exit 0 on the wrap commit. The commits after `e87ff45` touch only docs, the
  evidence log, `docs/lessons.md` and this file (nothing the gate builds or tests but docs-check).

## Next step
1. Joe decides on the current head and on the items under the PR body's "Needs Joe's decision": each
   architecture.md edit (§1, §12 status line, §12 `astra.plot` and `astra.cli`, §15 row 65), and the ground
   track's use of `derived.v_n`.
2. Before merge, rerun CI's `check` with `gh pr ready --undo` then `gh pr ready`.
3. Next M6 task: PR 6 (Monte Carlo figures, `astra.mc_analysis`, `astra mc analyze/rerun`) or PR 9 (benchmark),
   per `docs/plans/m6-analysis-tooling.md`. PR 8 inherits C-12's report part (SVG ids repeat across files).

## Tried / failed
- Correction rounds and failed integrations are in the PR body (0 of 2; 0).
- Two mutants first survived (overwrite allowed, one salt per package); two tests were added and both mutants
  are now killed.
- The worktree guard refused 4 commands (a `sed` with a variable, a Python heredoc, a variable loop, a
  `git -C`). Each was redone with Edit, a script run by its path, or a plain command, and is listed in the PR body.

## Honesty rules
"A trace matched" is not "verified". Verified means the acceptance command ran and exited 0 in this session.
