# STATE (rewritten once, at /wrap, never appended; ≤ 40 lines, says nothing that changes at merge)

## In flight
Leftovers from #86, plus the plan-mode default: PR #87, draft, branch `worktree-readme-leftovers`, base
`origin/main` `61c028d`. Plan: `docs/plans/readme-leftovers.md` (approved by Joe in plan mode; he added the
claude-code.md item afterward). Tier 0 (docs only), with one exception Joe chose: `.claude/settings.json` gains
`permissions.defaultMode: "plan"`. Joe waived the full gate and Codex. Stage: human review. Done: README exit
codes match `cli.py` (70 added); R-001/R-002 marked verified in `docs/reviews/readme-rewrite-review.md`; `/wrap`'s
"ask Joe if the guard refuses" removed; claude-code.md's draft-location note is now observed; the settings change
and workflow.md's clause for it.

## Last verified result
Run this session, on `bacbe6e` and on the wrap commit:
- `python3 scripts/docs_check.py`: exit 0, `docs-check: OK`. `git diff --cached --check`: clean.
- `.claude/settings.json` parses; `permissions` is `{'defaultMode': 'plan'}`.
- Not run: `./scripts/check-full.sh` and Codex (both waived by Joe). Plan mode as the default takes effect only in a
  new session; no session has run under it yet.

## Next step
1. Joe decides whether to approve the current head, including the settings change in a Tier 0 PR (PR body).
2. Before merge, rerun CI's `check` with `gh pr ready --undo` then `gh pr ready`.
3. Next M6 task: PR 5 (plots) or PR 9 (benchmark), per `docs/plans/m6-analysis-tooling.md`. The next session should
   start in plan mode; if it does not, note that in its PR.

## Tried / failed
- Correction rounds and failed integrations are in the PR body (0 of 2; 0).
- The first scripted edit of the wrap skill missed because the phrase spans a line break; it was rerun on the
  in-line fragment. The line was not reflowed, to honor "no other wording changes".
- No guard refusals.

## Honesty rules
"A trace matched" is not "verified". Verified means the acceptance command ran and exited 0 in this session.
