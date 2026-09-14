---
name: wrap
description: End an ASTRA session - rewrite STATE.md, append lessons, ensure the branch is pushed and the PR is open, print next session's opener.
disable-model-invocation: true
---

End of session. Do these in order; each is a file or a command, not a conversation.

1. **STATE.md** — rewrite it completely (never append), 40 lines or fewer, exactly these sections:
   `## In flight` (branch, PR number, what is done, what awaits Joe), `## Last verified result`
   (only what an acceptance command proved in this session; "a trace matched" is not verified),
   `## Next step` (the next unchecked PLAN.md item and any precondition on Joe's machine),
   `## Tried / failed` ("tried X, failed because Y", or "Nothing yet"), and the `## Honesty rules` block
   copied verbatim from the current file.
2. **PLAN.md** — tick the item this PR completes only if its acceptance passed; otherwise leave it.
3. **docs/lessons.md** — append one entry per thing that actually bit (numerical, build, or tooling), dated,
   newest at the bottom. Never rewrite an old entry. If a lesson became a rule, say where. Nothing bit: no entry.
4. **Branch and PR** — `git status` clean, branch pushed, `gh pr status` shows the PR open with a body that
   states the acceptance command and its result. If anything is missing, do it now.
5. **Do not touch** goldens, tolerances, or docs/decisions/ from here.
6. **Print the opener** for next time, ready to paste, on one line:
   `claude --worktree <next-name> [--effort xhigh if physics] --permission-mode plan` followed on the next
   line by the opening prompt for the next PLAN.md item, 3-10 lines, with its acceptance command.
