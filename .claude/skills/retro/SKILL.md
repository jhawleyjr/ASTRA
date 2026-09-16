---
name: retro
description: Milestone retrospective, run by Joe only - propose rules to delete and at most one rule to add. Produces a diff or nothing.
disable-model-invocation: true
---

Run at the end of a milestone (a `- [x] **M<n>**` line in PLAN.md), by Joe. Output is a diff to AGENTS.md /
CLAUDE.md / `.claude/rules/` / the skills / the hooks and their `.cases`, or the single line "nothing to change".
Do not apply the diff; Joe does. No subagents: the tree is small and the point is one reader holding all of it.

1. Read PLAN.md (the milestone and its PRs), AGENTS.md and CLAUDE.md, `.claude/rules/` if present, every
   `.claude/skills/*/SKILL.md`, `.claude/hooks/*.sh` and `.claude/hooks/tests/*.cases`, and docs/lessons.md. Read
   the git log of those files since the previous milestone was ticked, so you know when each rule arrived.
2. **Delete candidates:** every rule or convention in AGENTS.md / CLAUDE.md that nothing in this milestone's commits,
   lessons, review findings, or hook cases exercised, and anything the tree already enforces (`.clang-tidy`,
   `.clang-format`, `pyproject.toml`, the hooks, `just --list`) or a linked doc already says. One line each: the
   rule and why it looks dead. Anything an agent would get right without being told is a candidate by definition.
3. **Add candidate:** at most one. Only something that bit at least twice (two lessons entries or two review
   findings on one theme). One line, under AGENTS.md "Non-standard conventions" or as a CLAUDE.md bullet, and only
   if a delete makes room.
4. **Skills and hooks:** for `/wrap`, `/retro`, and each hook: did it run, did it misfire (a lessons entry naming
   it), is any branch unreached by a `.cases` line, is any step dead. Propose the edit or nothing; a hook edit is
   verified by running `.claude/hooks/tests/run.sh` on it.
5. **Cost:** run `scripts/tokens.py` over this milestone's transcripts (`~/.claude/projects/*ASTRA*/**/*.jsonl`)
   and print output tokens, cache reads, and model per session. Propose one change to `docs/process/workflow.md`
   §Model and effort only if a number shows the current rule wrong.
6. **Follow-ups:** anything the milestone deferred that belongs as a line in the next milestone's PR list in
   PLAN.md. Propose the lines; tick nothing.
7. Check AGENTS.md + CLAUDE.md stay at or under 100 lines together and STATE.md under 40 after the diff; no SHAs in
   process files; nothing under docs/decisions/, goldens, or tolerances.
8. Print the unified diff, or "nothing to change".
