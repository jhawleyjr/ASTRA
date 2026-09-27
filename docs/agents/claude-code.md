# Claude Code procedures

Provider-specific procedures; shared rules remain in [AGENTS.md](../../AGENTS.md).

- Work in a worktree: sessions start with `claude --worktree <name>`. A PreToolUse hook (`.claude/hooks/no-main.sh`)
  blocks `git push` to `main` and any commit/merge/rebase while `main` is checked out. Commit, push and draft-PR
  rules are in AGENTS.md. A push after the PR opens does not re-run CI's `check`: see the
  `gh pr ready --undo` / `gh pr ready` rerun in [workflow](../process/workflow.md#human-approval-and-mergerelease-gates).
- Model and effort: [workflow](../process/workflow.md#model-and-effort); physics diffs are checked by Joe reading them, not by effort.
- Prefer the existing clangd/pyright LSP tools for symbol navigation; diagnostics are feedback, not the gate.
- Keep the context small: build/test output to a file, show the tail; one PR per session.
- Run the gate with the Bash tool's own `run_in_background`, never `nohup … &` inside it (that double-backgrounds it
  and the completion notice reports the wrapper's exit). Wait on the tool's returned process handle and read its final exit status and the log's
  `check: OK` line; do not select a PID with a global `pgrep` (see `docs/lessons.md`). Trim the log per
  [validation](../process/validation.md#evidence) and put the results in the PR body.
- Use [`/plan-feature`](../../.claude/skills/plan-feature/SKILL.md) for specs, repository-aware plans and
  packages; [`/review-cycle`](../../.claude/skills/review-cycle/SKILL.md) for Codex dispatch and bounded
  fixes; [`/human-review`](../../.claude/skills/human-review/SKILL.md) for Joe's packet.
- Follow [review dispatch](review-dispatch.md): select working-tree versus committed-branch scope
  explicitly. A dirty candidate plus earlier commits needs combined coverage. Codex is read-only;
  the built-in Claude reviewer and automatic stop-time model reviews do not replace it.
- Use the revised [`/wrap`](../../.claude/skills/wrap/SKILL.md) when pausing or finishing meaningful work,
  including blocked tasks. It records the current gate, validation/review evidence, and next action, writes STATE
  once, pushes the task branch and opens or updates the draft PR; it does not advance unfinished work to the next milestone.
