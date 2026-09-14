# Lessons (append-only)

Numerical, build, and tooling gotchas that actually bit us. One entry each; newest at the bottom. If a lesson turns into a rule, it goes in CLAUDE.md and the entry here says so.

- **2026-09-14 — git from a Cowork session leaves `.git/index.lock` behind.** The Cowork shell mounts the repo without delete permission, so any git command that touches the index (`git status` without `--no-optional-locks`, `git add`, `git commit`) can leave a zero-byte `index.lock` it cannot remove, and the next commit fails. Rule: in Cowork, only read-only git with `git --no-optional-locks`, and never commit from there — commits belong to Claude Code sessions (see `docs/process/workflow.md`).
