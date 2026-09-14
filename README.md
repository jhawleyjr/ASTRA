# ASTRA — Aerospace Simulation, Test, and Research Architecture

A vehicle-agnostic six-degree-of-freedom simulation framework for commercial transport aircraft, built to run the aircraft's real embedded software against the sim — all-digital first, hardware-in-the-loop later.

**Status:** pre-M0. No code yet. Everything needed to start is in `docs/`:

| Read this | For |
|---|---|
| `docs/architecture.md` | The design. Sole input to milestone M0. §16 has the acceptance criteria per milestone, §17 the ADRs to write first. |
| `docs/process/workflow.md` | How work happens: one Claude Code session per task, `just check` as the gate, hooks, skills, git rules. |
| `docs/decisions/0000-founding-decisions.md` | The index of every decision made before code existed, and the open questions deliberately deferred. |

## Protecting `main`

Two layers, both required:

1. **GitHub ruleset** (the backstop). Once, from a terminal with `gh` logged in:
   ```
   gh api -X POST repos/jhawleyjr/ASTRA/rulesets --input .github/rulesets/main.json
   ```
   This requires a pull request for every change to `main`, squash-merge only, and blocks deletion and force-push. When the CI workflow exists (M0), add `check` as a required status check to the ruleset (`.github/rulesets/main.json` → `required_status_checks`, then `gh api -X PUT repos/jhawleyjr/ASTRA/rulesets/<id>`), so a red PR can't merge.
2. **Local hook** (the everyday guard). `.claude/settings.json` installs `.claude/hooks/no-main.sh` as a `PreToolUse` hook on Bash: it blocks any `git push` to `main` and any `git commit`/`merge`/`rebase` while `main` is checked out. Claude Code sees the block message and moves to a branch. It is in the repo, so every clone gets it.

Claude Code sessions always start with `--worktree <name>`, which creates `.claude/worktrees/<name>` on branch `worktree-<name>` from the current remote `main` and blocks edits to the main checkout entirely.

## Starting M0

**Start M0 with** `claude --worktree m0 --model fable --effort xhigh --permission-mode plan` in this directory, then:

> Read `docs/architecture.md` and `docs/process/workflow.md`. The milestone is M0 as defined in architecture §16. Before writing anything, restate in one paragraph what you understood the plugin model (§4) and the ICD boundary (§10) to be, and propose how to split M0 into three or four PRs. First PR: `.claude/settings.json` (model `fable`, effort `high`), `CLAUDE.md`, and ADR-0001 through ADR-0011 in `docs/decisions/` from the table in §17, each with its rejected alternatives. Install the PreToolUse hook that guards `docs/decisions/**` only after the ADRs exist.

Plan mode here is a checkpoint, not a design step — the architecture doc is the design. If the restatement matches it, approve and let it run.
