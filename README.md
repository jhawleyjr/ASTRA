# ASTRA — Aerospace Simulation, Test, and Research Architecture

A vehicle-agnostic six-degree-of-freedom simulation framework for commercial transport aircraft, built to run the aircraft's real embedded software against the sim — all-digital first, hardware-in-the-loop later.

**Status:** See `PLAN.md` for milestones and `STATE.md` for the latest session handoff.
The [documentation index](docs/README.md) links design, workflow, and reusable templates:

| Read this | For |
|---|---|
| `docs/architecture.md` | The accepted design. §16 has the acceptance criteria per milestone, §17 the ADRs to write first. |
| [docs/process/workflow.md](docs/process/workflow.md) | Risk-based builder/reviewer workflow, local validation, Joe's approval and merge/release boundaries. |
| [docs/templates/](docs/templates/) | Optional specs, plans, work packages, reviews, human packets, and ADR proposals. |
| `docs/decisions/0000-founding-decisions.md` | The index of every decision made before code existed, and the open questions deliberately deferred. |

## Protecting `main`

Two layers, both required:

1. **GitHub ruleset** (the backstop). Once, from a terminal with `gh` logged in:
   ```
   gh api -X POST repos/jhawleyjr/ASTRA/rulesets --input .github/rulesets/main.json
   ```
   This requires a pull request for every change to `main`, squash-merge only, blocks deletion and force-push, and requires the CI job `check` to pass on a branch that is up to date with `main`, so a red PR can't merge. The file is the ruleset: edit `.github/rulesets/main.json`, then re-apply it with `gh api -X PUT repos/jhawleyjr/ASTRA/rulesets/<id> --input .github/rulesets/main.json` (`gh api repos/jhawleyjr/ASTRA/rulesets` lists the id).
2. **Local hook** (the everyday guard). `.claude/settings.json` installs `.claude/hooks/no-main.sh` as a `PreToolUse` hook on Bash: it blocks any `git push` to `main` and any `git commit`/`merge`/`rebase` while `main` is checked out. Claude Code sees the block message and moves to a branch. `.claude/hooks/stop-check.sh` is the `Stop` hook: `just test-unit` runs when a turn ends and blocks the first failed stop attempt; a continued turn can report a blocker without retrying forever. A completed turn does not establish a passing gate. Both are in the repo for Claude Code sessions; other agents follow the same boundaries in `AGENTS.md`.

New Claude Code tasks start with `--worktree <name>`; other agents use one task branch/worktree per writer.
Claude plans, implements, validates, fixes and integrates; Codex reviews and adversarially reviews.
Preserve an explicitly assigned branch. See the workflow for setup and integration conventions.
Joe is the sole maintainer: his explicit decision or squash-merge is the human approval record.
The ruleset intentionally does not require a separate GitHub approving-review badge.

## Building and validation

Ubuntu 24.04, apt-only toolchain: provision once with `./scripts/setup-ubuntu.sh`.
Use `./scripts/check-fast.sh` while iterating and `./scripts/check-full.sh` before final review.
These wrap `just test-unit` and `just check`; the justfile remains the command source of truth.
The full gate builds/tests all four presets and runs Python, formatting, static-analysis and hook checks.
See [validation coverage and evidence](docs/process/validation.md) for details.

CI intentionally uses **GATE=local** to conserve GitHub Actions minutes. Full validation runs locally;
a green CI `check` is not evidence that the full gate ran. PRs start as drafts, and the workflow
explains when to mark ready and rerun checks after later pushes. No release pipeline is configured yet.
