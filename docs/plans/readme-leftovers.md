# Plan: #86 leftovers, plus the plan-mode default

Status: approved by Joe in plan mode, 2026-10-01; item 7 added by Joe after approval.

## Context

Three small corrections left over from PR #86 (README rewrite). Joe approved this scope on 2026-10-01. He then
added one settings change: sessions start in plan mode. The three fixes are Tier 0 (docs only). The settings
change alters Claude's session behavior, so it falls outside Tier 0. Joe chose to keep it in this PR, with the
exception noted in the PR body and no full gate. Joe waived the Codex pass.

Base: `origin/main` at `61c028d` (fetched; it is still the tip). Worktree `.claude/worktrees/readme-leftovers`,
branch `worktree-readme-leftovers`, one draft PR.

## Changes

1. **`README.md`, "Run a config"** (lines 98-100). Rewrite the exit-code sentence so it matches what
   `python/astra/cli.py` returns (docstring lines 11-29; `_run` lines 280-338):
   - 0 otherwise; 1 on a halt; 2 under `--strict` when a warn counter is nonzero; 64 on a usage error;
   - 65 on a config or plugin error (`ConfigError`/`PluginError`). The current README says only "config";
   - 70 on an internal error (`EX_SOFTWARE`: a defect, with its traceback on stderr). The README omits this today;
   - 74 on a log error (the log could not be written, closed or published, a `LogError`, which includes one raised
     at construction) or when the summary could not be written to stdout.
   Keep the link to `python/astra/cli.py`. Make no other changes to the README.

2. **`docs/reviews/readme-rewrite-review.md`**, from PR #86's body:
   - R-001 and R-002: change `Status: fixed-unverified` to `Status: verified`. Change `Verification: pending
     (Codex fix verification).` to the fix-verification result: Codex fix verification on `367a84a..437db3c`,
     thread `01a0f5fe-14a3-7b13-9fb6-ff8000921eb9`, **approve**, the finding verified, no new findings. Append
     `fix verification, verified` to each History line.
   - Round line: replace "fix 1 (this commit), its Codex verification pending" with "fix 1 (`437db3c`); fix
     verification (thread `01a0f5fe-14a3-7b13-9fb6-ff8000921eb9`, approve, no new findings)".

3. **`.claude/skills/wrap/SKILL.md:71`**: delete "; ask Joe if the guard refuses", so the parenthetical reads
   "(`mv`; leave nothing untracked there)". Make no other wording changes. This follows
   `docs/agents/claude-code.md:9`.

4. **`.claude/settings.json`**: add `"permissions": { "defaultMode": "plan" }` as a top-level key, after
   `plansDirectory`. Change nothing else.

5. **`docs/process/workflow.md:319`** ("Model and effort"): add one clause saying sessions start in plan mode
   (`"permissions": {"defaultMode": "plan"}`).

6. **`docs/agents/claude-code.md:28`** (added by Joe after plan approval): replace "(inferred from that check, not
   observed)" with "(observed 2026-10-01: this PR's own session, started in the main checkout, wrote its draft
   there)". No other wording changes.

7. **Housekeeping**: move this draft into the worktree as `docs/plans/readme-leftovers.md` (`mv`; leave nothing
   untracked in the main checkout) and commit it with Joe's decision on the status line. Add this PR's one-line
   entry to PLAN.md and rewrite STATE.md at `/wrap`.

## Process

- Commit and push after each coherent step. Open the draft PR at the first commit; the title says Tier 0 plus the
  settings exception.
- Write the PR body to `docs/templates/pr-description.md`. Update it with `gh api -X PATCH
  repos/{owner}/{repo}/pulls/<n> -F body=@file`, never `gh pr edit`. The body records: the tier and the settings
  exception (Joe's call); the waived Codex pass and full gate; each check with its exit code; any guard refusal,
  rerun split or done with Write/Edit without asking.
- Run `/wrap` before merge.

## Verification

- `python3 scripts/docs_check.py`: exit 0.
- `git diff --check origin/main`: clean.
- `python3 -c 'import json; json.load(open(".claude/settings.json"))'`: settings.json still parses.
- Grep `README.md` for "70" and "74" and compare the sentence with `cli.py`'s docstring by eye. Grep the review
  record: no "fixed-unverified" and no "pending" remain.
- Not run: the full gate and Codex (both waived by Joe). The plan-mode default takes effect only in a new
  session, so this one cannot exercise it. The PR body says so.
