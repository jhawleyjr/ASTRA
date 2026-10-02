# Claude Code procedures

Provider-specific procedures; shared rules remain in [AGENTS.md](../../AGENTS.md).

- Work in a worktree: sessions start with `claude --worktree <name>`. A PreToolUse hook (`.claude/hooks/no-main.sh`)
  blocks `git push` to `main` and any commit/merge/rebase while `main` is checked out, and refuses a commit message
  with a co-author trailer (`.claude/settings.json` sets `"attribution": false`; the scanner is
  `.claude/hooks/commit_trailer.py`). Commit, push and draft-PR
  rules are in AGENTS.md. A push after the PR opens does not re-run CI's `check`: see the
  `gh pr ready --undo` / `gh pr ready` rerun in [workflow](../process/workflow.md#human-approval-and-mergerelease-gates).
- Never ask Joe about a worktree-guard refusal: rerun as plain separate commands or use Write/Edit; note it in the PR body.
- Model and effort: [workflow](../process/workflow.md#model-and-effort); physics diffs are checked by Joe reading them, not by effort.
- Prefer the existing clangd/pyright LSP tools for symbol navigation; diagnostics are feedback, not the gate.
- Keep the context small: build/test output to a file, show the tail; one PR per session.
- Run the gate with the Bash tool's own `run_in_background`, never `nohup … &` inside it (that double-backgrounds it
  and the completion notice reports the wrapper's exit). Wait on the tool's returned process handle and read its final exit status and the log's
  `check: OK` line; do not select a PID with a global `pgrep` (see `docs/lessons.md`). Trim the log per
  [validation](../process/validation.md#evidence) and put the results in the PR body.
- Use [`/plan-feature`](../../.claude/skills/plan-feature/SKILL.md) for specs, repository-aware plans and
  packages; [`/review-cycle`](../../.claude/skills/review-cycle/SKILL.md) for Codex dispatch and bounded
  fixes; [`/human-review`](../../.claude/skills/human-review/SKILL.md) for the PR description Joe reviews.
- Plan-mode drafts land in the session's own `docs/plans/` under a random name (`plansDirectory` in
  `.claude/settings.json`). Once Joe approves a plan, rename the draft (`git mv`, or `mv` before its first add) to
  the plan's conventional name and commit it; never write a second copy, and never commit a random-named draft. A
  draft still awaiting Joe's decision at `/wrap` is renamed the same way, committed with a status line saying it is
  awaiting Joe's approval (never "approved") and pushed with the branch, so nothing is held only locally. Only a
  draft Joe has rejected is deleted. Start sessions at the checkout or worktree root: the path resolves from the
  session's root (a session started at a worktree root wrote into that worktree's `docs/plans/`, 2026-09-30).
  A session started in the main checkout that plans and then enters a worktree (EnterWorktree) writes its draft
  to the main checkout's `docs/plans/`, untracked (observed 2026-10-01: #87's own session, started in the main checkout, wrote its draft
  there): `mv` the draft into the worktree's `docs/plans/` before renaming it, and leave nothing untracked in the main checkout.
- Follow [review dispatch](review-dispatch.md): select working-tree versus committed-branch scope
  explicitly. A dirty candidate plus earlier commits needs combined coverage. Codex is read-only;
  the built-in Claude reviewer and automatic stop-time model reviews do not replace it.
- Use the revised [`/wrap`](../../.claude/skills/wrap/SKILL.md) when pausing or finishing meaningful work,
  including blocked tasks. It brings the docs current (its docs step), records the current gate, validation/review evidence, and next action, writes STATE
  once as main reads it after the merge (the PR's own stage goes in its PR body), pushes the task branch and opens or updates the draft PR; it does not advance unfinished work to the next milestone.
