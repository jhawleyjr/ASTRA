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
   [`/human-review`](../human-review/SKILL.md) to prepare the packet in a PR body draft or review file.
   Include scope/requirements, current validation and retained logs, Codex general/adversarial results,
   unresolved findings, assumptions/risks, critical files and rollback. A packet can be drafted earlier,
   but label missing gates; do not call it ready for approval. Link existing evidence rather than copy it.
   Retain exact candidate revisions and dirty-tree details in evidence; link/cite them from the handoff.
2. **Task status, if present:** update packages, dependencies, workflow stage, findings, retry counts,
   blocker/owner and next action using the [manifest fields](../../../docs/templates/task-status.md).
   Do not create a manifest for a simple task or mark an integrated feature complete from one package.
3. **PLAN.md:** change only the assigned item's status when its stated acceptance and completion
   conditions actually hold. Keep implementation, approval, merge and release distinct. Check a
   milestone only when all of its requirements hold; do not advance to a new milestone while this
   task has unfinished gates. No separate retrospective ceremony is required.
4. **STATE.md:** rewrite to at most 40 lines, true now, with `## In flight`, `## Last verified result`,
   `## Next step`, `## Tried / failed` (including the current `Correction rounds: n/2; failed
   integrations: n/2`), and the existing `## Honesty rules` block verbatim. Include the branch/PR
   (or no PR), actual workflow stage, links to evidence, what remains and who owns it.
   Distinguish prior-session validation from commands run now; preserve useful product-task pointers.
5. **docs/lessons.md:** append only a new lesson that actually occurred; do not duplicate old entries.
   Never modify goldens, tolerances or accepted ADRs as part of handoff.

## Git and next action

A local or dirty-tree handoff is valid. Do not demand a clean tree or create commits, push, open a PR,
mark ready, merge or release merely because `/wrap` ran. Complete commit/push/draft-PR actions only
when already authorized by the task; preserve unrelated changes. Missing authorization is a pending
handoff action, not a reason to interrupt for permission. Inspect and record the result of any action;
never replace an existing PR body with a claim that unrun checks passed.

End with a short handoff: actual stage, completed work, evidence/gaps, unresolved findings and the
next action/owner. Provide a 3–10 line continuation prompt with artifact paths and acceptance commands.
Resume the same task/worktree while its gates are unfinished; suggest a new `claude --worktree ...`
only for a genuinely new task. Claude owns continuation; Codex owns review. Joe decides merge/release.
