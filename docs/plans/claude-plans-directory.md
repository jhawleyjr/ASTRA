# Plans into the repo: `plansDirectory` and sorting ~/.claude/plans

Risk: Tier 2 (agent workflow configuration and docs; no product code).
Status: **approved by Joe, 2026-09-30, in plan mode, with four additions folded in below** (the third step-1
outcome, the settings diff against `7f61ac1`, hash-first sorting, and no retry after a refusal).
Base: `origin/main` `7f61ac1` (#82). Branch: `worktree-claude-plans-dir`.

## Context

Plan-mode drafts landed in `~/.claude/plans/` under random names, outside git, so approved plans were copied into
`docs/plans/` by hand and drafts piled up (77 files on 2026-09-30, ASTRA's and another project's). Setting
`plansDirectory` puts drafts in the repo; sorting the old drafts recovers any plan that was never committed; a
written rule keeps random-named drafts out of commits and keeps pending drafts from living only locally.

## Facts established beforehand (Claude Code 2.1.286, installed bundle, 2026-09-30)

- Schema text: "Custom directory for plan files, relative to project root. If not set, defaults to
  ~/.claude/plans/". A path outside the project root logs "plansDirectory must be within project root" and is
  ignored (default used); a diskless launch also uses the default.
- The online settings reference (code.claude.com/docs/en/settings-reference) says the default is `.claude/plans`
  in the working directory and relative paths resolve from the working directory. Whether 2.1.286's "project
  root" is the start directory or the git root was not determinable from the minified code; Joe's check settles
  it for a worktree.
- No change needed to `stop-check.sh`, `session-start.sh` (its main-checkout `git status --short` warning about a
  stray draft is wanted), `scripts/docs_check.py`, `guard-oracle.sh` or `/plan-feature`. `/wrap` commits "the
  coherent work" and could commit a random-named draft, so it gets the rule.

## Scope

Files: `.claude/settings.json` (one key only; model, effort, env, hooks, attribution unchanged),
`docs/agents/claude-code.md`, `.claude/skills/wrap/SKILL.md`, `docs/plans/*` (new plan files), `PLAN.md` (one
line), the gate summary under `docs/reviews/evidence/`. Plus reading and deleting in `~/.claude/plans`. Out of
scope: AGENTS.md, `docs/decisions/`, `tests/golden/**`, any other file outside the repo.

## Order of work

1. Add `"plansDirectory": "docs/plans"` to `.claude/settings.json`, nothing else. Commit, push, open the draft PR.
   Stop and ask Joe to start a plan-mode session at the worktree root, have it write a plan, and report which
   `docs/plans/` the draft landed in (the worktree's or the main checkout's). Nothing else in the worktree until he
   reports. Third outcome: the draft lands in `~/.claude/plans/` (key not honored); then stop, report, write no rule.
2. Delete that test draft and record its path in the PR body. If it is in the main checkout and the worktree guard
   refuses the delete, ask Joe to delete it and record the path he confirms.
3. Sort `~/.claude/plans`. First match each file by content hash against every `docs/plans/` blob in git history
   (`git log --all`); read only the unmatched ones. Each recovered plan gets one line saying it was recovered from
   `~/.claude/plans` on 2026-09-30, with its approval status as the file or history shows it. Cases:
   - ASTRA plan already committed (same content, or an earlier draft of a committed plan): delete.
   - ASTRA plan never committed: commit under `docs/plans/<conventional-name>.md`, with why it was never committed
     if the file or history shows it.
   - Another project's plan: leave it.
   - Unsure: leave it and list it for Joe.
4. Write the rule for the observed outcome only: `docs/agents/claude-code.md` (a few lines),
   `.claude/skills/wrap/SKILL.md` (one sentence), this file, and `PLAN.md`'s one line.
5. Checks: `python3 -m json.tool .claude/settings.json`; `git diff 7f61ac1 -- .claude/settings.json` is exactly the
   one added key; `python3 scripts/docs_check.py`; `git diff --check`. Then `./scripts/check-full.sh` from a fresh
   configure (`build/` removed first), exit 0 plus `check: OK`, with the trimmed gate summary committed. Then one
   Codex pass on the final diff.

Refusals: if any write or delete is refused (auto-mode classifier, worktree guard), do not retry by another route
(lessons 2026-09-27, 2026-09-29); name the refusal and ask Joe.

## Joe's check (step 1)

Outcome 1. A plan-mode session started at the worktree root wrote its draft to the worktree's own `docs/plans/`:
`.claude/worktrees/claude-plans-dir/docs/plans/this-is-only-a-wild-backus.md` (2026-10-01 02:24:01 UTC,
untracked); nothing new appeared in the main checkout. The draft was deleted in step 2. So for a worktree session,
"project root" is the session's root (the worktree), not the main checkout. Joe's review added the inferred
case (not observed): a session started in the main checkout that plans and then enters a worktree writes its
draft to the main checkout's `docs/plans/`, so the rule moves such a draft into the worktree before renaming it.

## Sorting result (step 3)

No draft matched a `docs/plans/` blob byte for byte, so all 77 were read (with a line-similarity score against
every historical `docs/plans/` and `docs/specs/` blob as a guide). `docs/plans/` was first used on 2026-09-19
(#31), so drafts from before then cannot have a committed counterpart.

- **Deleted, earlier drafts of committed plans (6):** `lucky-sniffing-possum.md` (`m6-pr3-cli.md`),
  `splendid-forging-meteor.md` (`m6-pr4-units.md`), `pasted-content-id-2d8b-m6-pr-deep-heron.md`
  (`m6-pr2-finalization.md`), `pasted-content-id-39b6-m6-pr-wild-pearl.md` (`m6-pr1-adr0027.md`),
  `pasted-content-id-e039-plugin-test-guar-hashed-pearl.md` (`plugin-test-guard.md`),
  `pasted-content-id-5269-astra-add-plugin-encapsulated-sun.md` (`plugin-test-helper.md`).
- **Recovered and committed, then deleted (26):** M0 kickoff and PRs 2 to 4, M1 PRs 1 to 4, M2 PRs 1, 1b, 2,
  2b, 2c and the PR 3 split plan, the workflow v3 review, the M3 PR 10 closeout, M4 PR 1, the workflow
  calibration, M5 PRs 1 and 5 to 9, Follow-up B, and the M6 plan critique. Each file's first lines name its draft,
  its approval as the file or history shows it, and why it was never committed. Eight British spellings in five
  of them were normalized to American English at Joe's review, as their provenance lines say.
- **Left, another project (43):** the Algorithmic-Trader repo's drafts (its `plans/`, `runs/`, `src/journal`,
  IBKR, autopilot and classifier work): the 25 `t0-*`/`t1-*` drafts and 18 others listed in the PR body.
- **Left for Joe (1):** `pasted-content-id-e26b-m3-complete-whimsical-sketch.md`, the M4 planning session's plan.
  It contains an early version of `m4-lockstep.md`'s content plus that session's own steps, so it is neither
  clearly an earlier draft of the committed plan nor clearly a separate plan.
- This plan's own draft (`pasted-content-id-b36c-plans-into-streamed-puzzle.md`) is deleted once this file is
  committed.
