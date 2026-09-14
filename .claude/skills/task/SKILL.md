---
name: task
description: Start one ASTRA task (one PR, one verifiable outcome). Usage - /task <3-10 line task> [--physics] [--touch-goldens]
disable-model-invocation: true
---

Start of session for one task. The task text is in $ARGUMENTS: intent, the acceptance command, and the ADRs that
bind it. Flags may appear anywhere in it.

1. **Preconditions.** The SessionStart hook already printed STATE.md, the log, and the unchecked PLAN.md items;
   do not re-read them. If this is not a worktree session, stop and ask Joe to restart with
   `claude --worktree <name>`. If the main checkout showed uncommitted changes, say so before doing anything.
2. **Flags.**
   - `--touch-goldens`: Joe is lifting the oracle guard for this session. Run `touch .claude/touch-goldens`
     (gitignored; removed at the next SessionStart). Say once that goldens, tolerances, and ADRs are editable.
   - `--physics`: physics-touching task (`core/kernel/`, `core/util/`, `plugins/physical/`, transports, goldens).
     The session must have been opened with `--effort xhigh`; if the opener did not include it, say so and
     stop. Joe reads the diff; say that in the approach.
3. **Gate first.** If `justfile` exists, run `just check` and report pass/fail in one line. It must pass on
   the current HEAD before you start; if it fails, that is the task now, and Joe decides. If there is no
   `justfile` yet, say there is nothing to run.
4. **Plan.** If the diff fits in one sentence, skip plan mode and do it. Otherwise enter plan mode: recon in
   the repo (prefer the clangd/pyright LSP tools over grep), then a five-line approach at most, the exact
   acceptance command, and the list of files you expect to touch. Wait for Joe to approve.
5. **Build against the acceptance command.** The Stop hook runs the fast check. Postcondition is
   `just check` green plus the acceptance command exiting 0 in this session. A widened tolerance, narrowed
   comparison, regenerated golden, or weakened test is a blocking finding, not a fix.
6. **Review, then PR.** Run `/code-review` in a subagent with the intent line and the relevant ADRs, told to
   report only gaps that affect correctness or violate a decision record. Fix findings in this session or
   add a line to PLAN.md. Commit, push the branch, open the PR yourself with `gh pr create`; Joe squash-merges.
   Prose in commits, PR bodies, and comments is American English. No SHAs in process files.
7. Finish with `/wrap`.
