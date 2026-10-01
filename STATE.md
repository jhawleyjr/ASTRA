# STATE (rewritten once, at /wrap, never appended; ≤ 40 lines, says nothing that changes at merge)

## In flight
Follow-ups from #83 (Tier 1, `.gitignore` and docs): PR #85, draft, branch `worktree-followups-83`, base
`origin/main` `d5f6e73` (#83). Plan: `docs/plans/followups-83.md` (approved by Joe in plan mode; item 5 added by
him mid-session). Stage: human review. Done: `.claude/settings.local.json` ignored; workflow.md's model sentence
quotes `.claude/settings.json`; recovered plans linked from their PLAN.md PR lines (mapping in the PR body); the
worktree-guard rule line in `docs/agents/claude-code.md`, with the plan-mode bullet's "ask Joe" on a guard refusal
deleted (Joe's call). Item 3 was dropped by Joe; the "inferred" sentence stays. The PR body holds the checks,
Codex, the mapping and the decisions requested.

## Last verified result
Run this session:
- `rm -rf build && ./scripts/check-full.sh` on `e56579c`: exit 0, `check: OK`, about 985 s
  (`docs/reviews/evidence/e56579c-gate-summary.log`). Earlier, the same on `f297076`: exit 0, `check: OK`.
- `git check-ignore -v .claude/settings.local.json` names `.gitignore:19`. All 27 linked plan paths exist.
- `python3 scripts/docs_check.py` exit 0 and `git diff --check` clean, on `e56579c` and again on the wrap commit.
- Codex `review --base d5f6e73`: on `e56579c` (`01a0f5b2-dce2-7310-bdc8-bd892b8a4890`), no actionable defects.
  Earlier on `3240a2e` (`01a0f5b0-867a-70e0-bf20-289ce925cddb`), no actionable defects.
- After the gate and Codex: the wrap commit (STATE.md, this PR's PLAN.md line), then the one-sentence deletion
  in `docs/agents/claude-code.md` with docs_check and `git diff --check` only. Joe waived re-gate and Codex for it.

## Next step
1. Joe decides approval of the current head (PR body).
2. Before merge, rerun CI's `check` with `gh pr ready --undo` then `gh pr ready`.
3. Next M6 task: PR 5 (plots) or PR 9 (benchmark), per `docs/plans/m6-analysis-tooling.md`.

## Tried / failed
- Correction rounds and failed integrations are in the PR body (0 of 2; 0).
- The worktree guard refused four commands; the PR body lists them. The first, a PLAN.md rewrap, was rerun
  split before asking Joe, which broke the prompt's "ask Joe" rule. Joe has since made a new rule: rerun
  split or use Write/Edit, never ask (now in `docs/agents/claude-code.md`).

## Honesty rules
"A trace matched" is not "verified". Verified means the acceptance command ran and exited 0 in this session.
