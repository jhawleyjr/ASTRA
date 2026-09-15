@AGENTS.md

## Claude Code

- Work in a worktree: sessions start with `claude --worktree <name>`. A PreToolUse hook (`.claude/hooks/no-main.sh`)
  blocks `git push` to `main` and any commit/merge/rebase while `main` is checked out. Push the branch, open the PR
  yourself; Joe squash-merges. Never put commit SHAs in process files.
- Model is `opusplan` (Opus plans, Sonnet executes) at `high`, subagents on Sonnet; pinned in `.claude/settings.json`.
  Never raise effort or model yourself; Joe escalates (`/effort xhigh` if a task stalls with full context, `/model
  fable` only after Opus failed with it). Physics diffs (`core/kernel/`, `core/util/`, `plugins/physical/`,
  transports, goldens) are read by Joe; that, not effort, is the check.
- Keep the context small: build/test output to a file, show the tail; one PR per session; no recon subagents.
  A backgrounded gate reports the wrapper's exit, not its own: verified means the log's `EXIT=0` / `check: OK`
  line was read, and a process started in the background may not exist yet when the next command runs.
- Never commit from a Cowork session (leaves `.git/index.lock`; see `docs/lessons.md`).
- Review before the PR is `/codex:review --base origin/main --wait` (other model, fresh context; reads `AGENTS.md`
  directly). Physics diffs also get `/codex:adversarial-review --base origin/main` with the ADRs as focus. Never
  the built-in `/code-review`; never the Codex stop-time gate; never a critique subagent. A plan deciding a design
  the ADRs do not pin (EOM, kinematics, integrator interface, health monitor) gets one `/codex:rescue` round in
  plan mode before Joe sees it, plan and acceptance command inlined (workflow.md §Review); other plans skip it.
