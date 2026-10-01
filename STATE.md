# STATE (rewritten once, at /wrap, never appended; ≤ 40 lines, says nothing that changes at merge)

## In flight
Plans into the repo (Tier 2, workflow config and docs): PR #83, draft, branch `worktree-claude-plans-dir`, base
`origin/main` `7f61ac1` (#82). Plan: `docs/plans/claude-plans-directory.md` (approved by Joe with four additions).
Stage: human review. Code candidate `ac16597`; `581df48` adds the gate summary, and this handoff commit follows.
`.claude/settings.json` gains only `"plansDirectory": "docs/plans"`. The draft-handling rule is in
`docs/agents/claude-code.md` and `/wrap`. 26 never-committed ASTRA plans were recovered into `docs/plans/`.
`~/.claude/plans` was sorted. The PR body holds the facts, Joe's check, the full sorting table and the checks.

## Last verified result
Run this session:
- Joe's check: a plan-mode session started at the worktree root wrote its draft to the worktree's
  `docs/plans/` (outcome 1). That test draft was deleted.
- `./scripts/check-full.sh` from a fresh configure (`build/` removed first) on `ac16597`: exit 0, `check: OK`,
  about 1048 s (`docs/reviews/evidence/ac16597-gate-summary.log`). CTest 658/659/660/661, pytest 1204 passed,
  5 skipped.
- `python3 -m json.tool .claude/settings.json` exit 0; `git diff 7f61ac1 -- .claude/settings.json` is one line;
  `python3 scripts/docs_check.py` exit 0; `git diff --check` clean.
- Codex `review --base 7f61ac1` (thread `01a0f55d-a5be-7802-b1fd-3513f8f6a22a`): no actionable defects.

## Next step
1. Joe reviews PR #83 and decides: approval of `ac16597`; `~/.claude/plans/pasted-content-id-e26b-m3-complete-
   whimsical-sketch.md` (delete, or recover as `docs/plans/m4-planning-session.md`); whether the British
   spellings kept verbatim in five recovered plans should be corrected.
2. Before merge, rerun CI's `check` with `gh pr ready --undo` then `gh pr ready`.
3. Next M6 task: PR 5 (plots; PR 3 and PR 4 are in `main`) or PR 9 (benchmark), per `docs/plans/m6-analysis-tooling.md`.

## Tried / failed
- Correction rounds and failed integrations are in the PR body (0 of 2; 0).
- The worktree guard refused compound commands naming git (and `git -C` to the main checkout, a read).
  Workaround: one plain command per call, or a script in the scratchpad (lesson 2026-09-27). No write or delete
  was refused.
- No `~/.claude/plans` draft matched a `docs/plans/` blob byte for byte, so all 77 were read, guided by a
  line-similarity score against every historical `docs/plans/` and `docs/specs/` blob.

## Honesty rules
"A trace matched" is not "verified". Verified means the acceptance command ran and exited 0 in this session.
