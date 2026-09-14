# ASTRA — Aerospace Simulation, Test, and Research Architecture

A vehicle-agnostic six-degree-of-freedom simulation framework for commercial transport aircraft, built to run the aircraft's real embedded software against the sim — all-digital first, hardware-in-the-loop later.

**Status:** M0 in progress (`PLAN.md` has the checklist). The design and process are in `docs/`:

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
   This requires a pull request for every change to `main`, squash-merge only, blocks deletion and force-push, and requires the CI job `check` to pass on a branch that is up to date with `main`, so a red PR can't merge. The file is the ruleset: edit `.github/rulesets/main.json`, then re-apply it with `gh api -X PUT repos/jhawleyjr/ASTRA/rulesets/<id> --input .github/rulesets/main.json` (`gh api repos/jhawleyjr/ASTRA/rulesets` lists the id).
2. **Local hook** (the everyday guard). `.claude/settings.json` installs `.claude/hooks/no-main.sh` as a `PreToolUse` hook on Bash: it blocks any `git push` to `main` and any `git commit`/`merge`/`rebase` while `main` is checked out. Claude Code sees the block message and moves to a branch. `.claude/hooks/stop-check.sh` is the `Stop` hook: `just test` runs when a turn ends and blocks the turn until it passes. Both are in the repo, so every clone gets them.

Claude Code sessions always start with `--worktree <name>`, which creates `.claude/worktrees/<name>` on branch `worktree-<name>` from the current remote `main` and blocks edits to the main checkout entirely.

## Building

Ubuntu 24.04, apt-only toolchain: run `./scripts/setup-ubuntu.sh` once, then `just check`. That is the whole gate: every CMake preset (`debug`, `release`, `asan`, `static-plugins`) builds with clang-18 `-Werror` and runs its tests, then clang-tidy, clang-format, and the hook test cases. `just build` and `just test` take one preset (default `asan`). CI runs the same `just check` on every pull request.

## Working on M0

Each M0 PR starts a fresh session: `claude --worktree <name> --effort xhigh --permission-mode plan`, with the PR's line from `PLAN.md` and the acceptance row from architecture §16 in the opening prompt. Plan mode there is a checkpoint, not a design step — the architecture doc is the design. If the restatement matches it, approve and let it run.
