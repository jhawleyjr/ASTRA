---
name: retro
description: Milestone retrospective, run by Joe only - propose rules to delete and at most one rule to add. Produces a diff or nothing.
disable-model-invocation: true
---

Run at the end of a milestone, by Joe. Output is a diff to CLAUDE.md / `.claude/rules/` / the skills, or the
single line "nothing to change". Do not apply the diff; Joe does.

1. Read CLAUDE.md, `.claude/rules/` if present, the skills in `.claude/skills/`, and docs/lessons.md. Read the git log since
   the previous milestone was ticked in PLAN.md.
2. **Delete candidates:** every rule or convention in CLAUDE.md that nothing in this milestone's commits,
   lessons, or review findings exercised. For each, one line: the rule and why it looks dead. Anything an
   agent would get right without being told is a delete candidate by definition.
3. **Add candidate:** at most one. Only something that bit at least twice (two lessons entries or two review
   findings on the same theme). One line, placed under "Non-standard conventions".
4. **Cost:** run `scripts/tokens.py` over this milestone's transcripts (`~/.claude/projects/*ASTRA*/**/*.jsonl`;
   readable from Claude Code, not from Cowork) and print output tokens, cache reads, and model per session. Propose
   one change to `docs/process/workflow.md` §Model and effort only if a number shows the current rule wrong.
5. Check CLAUDE.md stays under 100 lines and STATE.md under 40 after the proposed diff.
6. Print the unified diff, or "nothing to change".
