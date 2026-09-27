---
name: wrap
description: Hand off an ASTRA Claude session at its actual twelve-step workflow stage, recording validation, Codex findings, human decisions and the next action. Use when pausing or finishing work, including incomplete or blocked tasks.
---

# Workflow handoff

Claude owns this handoff. Read [shared rules](../../../AGENTS.md) and the
[workflow gates](../../../docs/process/workflow.md#coordination-and-handoff), then the current brief,
spec/plan, task status and review evidence where they exist. Use existing artifacts; small tasks
still need only STATE and a brief/PR body. An open PR or completed review is not a prerequisite.

## Establish the actual state

Inspect Git branch, diff (including untracked files), and existing task/check/review evidence.
Read current PR state if a PR exists and access is available; otherwise mark remote state unverified.
Find the first unfinished gate, using the workflow's risk tier and already recorded authorization:

- Intent/spec/plan unresolved: hand off the specific decision to Joe; do not begin dependent work.
- Implementation, validation or integration incomplete: Claude retains the next action. Record failed,
  running, stale or missing checks accurately; never turn a fast/local-CI pass into full validation.
  See the workflow's [stage transitions](../../../docs/process/workflow.md#the-twelve-steps) for what
  "implementation" and "integration" mean inside the twelve numbered steps.
- Codex review/adversarial review missing: record the required pass and candidate as pending.
  Open findings go back to Claude for fixes, then validation and Codex verification.
- Fix/integration budget exhausted or requirement disagreement: record attempts, owner and decision
  needed from Joe. Preserve retry counts across sessions; a new context does not reset them.
- Gates satisfied for human review: prepare Joe's packet. Only Joe's actual current-candidate decision
  establishes approval; his own squash-merge may record it. No separate GitHub approval badge is needed.
- Merge/release: record only what actually happened. Release authorization/validation is separate;
  a merged PR does not prove a release occurred or authorize one.

This skill records and packages the result; it does not start a new implementation/review-fix loop.
Reference completed check logs and candidate revisions rather than rerunning expensive checks just
to wrap. If later changes invalidate evidence, mark the affected gate pending and name the rerun.

## Update the handoff artifacts

1. **Review evidence/packet:** when requesting human review, use
   [`/human-review`](../human-review/SKILL.md) to prepare the packet in the PR body (or a review file).
   The PR body carries scope/requirements, check results (command, exit code, summary counts, a link to the
   [gate summary log](../../../docs/process/validation.md#evidence)), Codex results (verdict, thread id,
   findings), unresolved findings, assumptions/risks, critical files and rollback, plus the exact candidate
   revisions and dirty-tree details. A packet can be drafted earlier, but label missing gates; do not call it
   ready for approval. Link existing evidence rather than copy it.
2. **Task status, if present:** update packages, dependencies, workflow stage, findings, retry counts,
   blocker/owner and next action using the [manifest fields](../../../docs/templates/task-status.md).
   Do not create a manifest for a simple task or mark an integrated feature complete from one package.
3. **PLAN.md:** follow its header and [AGENTS.md](../../../AGENTS.md#read-first): add this PR's one line
   (scope, plan, packet) and change an item's status only when its stated acceptance and completion
   conditions actually hold. Check a milestone only when all of its requirements hold; do not advance to
   a new milestone while this task has unfinished gates. No separate retrospective ceremony is required.
4. **STATE.md:** rewrite it once, here, to at most 40 lines, with `## In flight`, `## Last verified result`,
   `## Next step`, `## Tried / failed` (including the current `Correction rounds: n/2; failed
   integrations: n/2`), and the existing `## Honesty rules` block verbatim. Include the branch/PR
   (or no PR), actual workflow stage, what remains and who owns it, and point to the PR body for results.
   Distinguish prior-session validation from commands run now; preserve useful product-task pointers.
   Per AGENTS.md, write nothing that changes when the PR merges (no merge status, no "awaiting merge"):
   name the PR and give the next task directly.
5. **docs/lessons.md:** append only a new lesson that actually occurred; do not duplicate old entries.
   Never modify goldens, tolerances or accepted ADRs as part of handoff.

## Git and next action

Under the standing authorization ([AGENTS.md](../../../AGENTS.md#commands-and-boundaries)), commit
the coherent work, push the task branch, and open the draft PR if none exists, or update its body with the
handoff/packet and results if one does. Results go in the body, not in a record-only commit. Preserve
unrelated changes and never sweep them into the commit. A push after the PR opens
does not re-run CI's `check`; note the `gh pr ready --undo` / `gh pr ready` rerun as a pre-merge step, per the
workflow. Inspect and record the result of every action (`gh pr view --json body`); never replace a PR body
with a claim that unrun checks passed. A blocked or dirty-tree handoff is still valid: push what is coherent
and say what is not.

End with a short handoff: actual stage, completed work, evidence/gaps, unresolved findings and the
next action/owner. Provide a 3–10 line continuation prompt with artifact paths and acceptance commands.
Resume the same task/worktree while its gates are unfinished; suggest a new `claude --worktree ...`
only for a genuinely new task. Claude owns continuation; Codex owns review. Joe decides merge/release.
