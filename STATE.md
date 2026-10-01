# STATE (rewritten once, at /wrap, never appended; ≤ 40 lines, says nothing that changes at merge)

## In flight
Plans into the repo (Tier 2, workflow config and docs): PR #83, draft, branch `worktree-claude-plans-dir`, base
`origin/main` `7f61ac1` (#82). Plan: `docs/plans/claude-plans-directory.md` (approved by Joe with four additions).
Stage: human review, second look. Joe reviewed `ac16597` and asked for six changes; all are in the candidate
`ae2f04c` (`b9f1519`: #49 provenance, the m2-pr2b title, the main-checkout draft move in `docs/agents/claude-code.md`
and `/wrap` labeled inferred, eight American spellings; `ae2f04c`: e26b recovered as
`docs/plans/m4-planning-session.md`). 27 never-committed ASTRA plans are recovered; `~/.claude/plans` holds only
the 43 other-project drafts. The PR body holds the facts, Joe's check, the sorting table, the checks and Codex.

## Last verified result
Run this session:
- Joe's check: a plan-mode session started at the worktree root wrote its draft to the worktree's
  `docs/plans/` (outcome 1). That test draft was deleted.
- `./scripts/check-full.sh` from a fresh configure on `ac16597`: exit 0, `check: OK`, about 1048 s
  (`docs/reviews/evidence/ac16597-gate-summary.log`). No re-gate after the docs-only review fixes (Joe's call).
- On `ae2f04c`: `python3 scripts/docs_check.py` exit 0; `git diff --check` clean. On `9dc924e`:
  `python3 -m json.tool .claude/settings.json` exit 0; `git diff 7f61ac1 -- .claude/settings.json` is one line.
- Codex: `review --base 7f61ac1` (`01a0f55d-a5be-7802-b1fd-3513f8f6a22a`) no actionable defects; verification
  `adversarial-review --base ac16597` (`01a0f57e-4993-7081-b552-b891910961da`) approve, six items PASS, new findings none.

## Next step
1. Joe decides approval of `ae2f04c` (PR body).
2. Before merge, rerun CI's `check` with `gh pr ready --undo` then `gh pr ready`.
3. Next M6 task: PR 5 (plots; PR 3 and PR 4 are in `main`) or PR 9 (benchmark), per `docs/plans/m6-analysis-tooling.md`.

## Tried / failed
- Correction rounds and failed integrations are in the PR body (0 of 2; 0). Joe's six items are his review, not a round.
- The worktree guard refused compound commands naming git and a `git -C` read (worked around per lesson
  2026-09-27), and one write: the first e26b recovery, a Python heredoc whose text mentioned git. Not retried;
  Joe chose `cp` plus an Edit-tool provenance line, checked with `diff` before the draft was deleted.
- No `~/.claude/plans` draft matched a `docs/plans/` blob byte for byte, so all 77 were read, guided by a
  line-similarity score against every historical `docs/plans/` and `docs/specs/` blob.

## Honesty rules
"A trace matched" is not "verified". Verified means the acceptance command ran and exited 0 in this session.
