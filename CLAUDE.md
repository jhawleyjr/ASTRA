@AGENTS.md

## Claude Code

- Work in a worktree: sessions start with `claude --worktree <name>`. A PreToolUse hook (`.claude/hooks/no-main.sh`)
  blocks `git push` to `main` and any commit/merge/rebase while `main` is checked out. Push the branch, open the PR
  yourself; Joe squash-merges. Never put commit SHAs in process files.
- Model and effort are pinned in `.claude/settings.json` (`opusplan` at `high`, subagents on Sonnet); never change
  them. Escalation is Joe's (workflow.md §Model and effort); physics diffs are checked by Joe reading them, not by effort.
- Keep the context small: build/test output to a file, show the tail; one PR per session; no recon subagents.
- Run the gate with the Bash tool's own `run_in_background`, never `nohup … &` inside it (that double-backgrounds it
  and the completion notice reports the wrapper's exit). Then block on the real process (`tail --pid=$(pgrep -x just)
  -f /dev/null`) and read the log's `EXIT=0` / `check: OK` line; nothing is verified before that line has been read.
- Never commit from a Cowork session (leaves `.git/index.lock`; see `docs/lessons.md`).
- Review before the PR is Codex (other model, fresh context; reads `AGENTS.md`), run by the session from Bash:
  `node "$(find ~/.claude/plugins -name codex-companion.mjs | head -1)" review --base origin/main --wait`; physics
  diffs also `adversarial-review --base origin/main --wait <focus naming the ADRs>`. The `/codex:*review` slash
  commands are user-only wrappers of that script; do not wait for Joe to run them. Never
  the built-in `/code-review`; never the Codex stop-time gate; never a critique subagent. A plan deciding a design the
  ADRs do not pin and that is expensive to unwind (M1: EOM, integrator, health monitor; M2: how gravity and
  atmosphere models get geodetic position from the Earth model, the wind injection point) gets one `/codex:rescue`
  round in plan mode before Joe sees it, plan and acceptance command inlined (workflow.md §Review); other plans skip it.
- End every session by running `/wrap` yourself (it is model-invocable); Joe does not trigger it.
