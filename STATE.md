# STATE (rewritten once, at /wrap, never appended; ≤ 40 lines; says nothing that changes at merge)

## In flight
Workflow calibration (Joe's retro decisions A–F, 2026-09-26), Tier 2, docs and skills only: PR #60, branch
`worktree-workflow-calibration` off `origin/main` `b6f2078`. PLAN.md is compacted to a milestone checklist,
open items and one line per PR; AGENTS.md states each rule once and the other files link to it; Tier 0/1 get one
Codex pass on the final diff; results go in the PR body; gates commit a trimmed
`docs/reviews/evidence/<head>-gate-summary.log`; Cowork review is scoped to Tier 2/3. The PR is merged once
`origin/main` contains it. Until then its body names what remains: the Codex result, then Joe's review.

## Last verified result
This session, on the branch: `python3 scripts/docs_check.py` exit 0; `git diff --check` exit 0; the open-item
mapping (every `- [ ]` item in `origin/main`'s PLAN.md matched to its new line, and the recon-subagent item closed
by this PR at Joe's direction) and a PLAN.md path-existence check, both scratch scripts whose output is in the
PR body. `just check` was waived by Joe (no hook, script or build change). The Codex verdict is in the PR body,
not here. Prior-session: M4 acceptance evidence at `4e418e1` (`docs/reviews/evidence/4e418e1-*`).

## Next step
1. While PR #60 is open: Joe reviews it from the PR body (retro findings, changes, the open-item mapping,
   disagreements resolved in AGENTS.md's favor, and results). A push after the PR opened needs the
   `gh pr ready --undo` / `gh pr ready` rerun of CI's `check` before merge.
2. After it: the next task is Joe's to assign, either M5 Tooling planning or an item in PLAN.md's Open items.
3. Follow-ups named in PR #60's body: none of them block it.

## Tried / failed
Correction rounds: 0/2; failed integrations: 0/2 at this handoff. Any later fix is recorded in PR #60's body.

## Honesty rules
"A trace matched" is not "verified". Verified means the acceptance command ran and exited 0 in this session.
