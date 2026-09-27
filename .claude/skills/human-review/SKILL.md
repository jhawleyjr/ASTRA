---
name: human-review
description: Write the ASTRA PR description Joe reviews from the current candidate, completed validation and Codex findings. Use at the human-review checkpoint or when Joe asks for the PR description; disclose incomplete gates in a draft.
---

# Write the PR description for Joe's review

Claude owns preparation; Joe owns the decision. Read the
[human-review and release gates](../../../docs/process/workflow.md#human-approval-and-mergerelease-gates)
and write the PR body from the [PR description template](../../../docs/templates/pr-description.md): a plain
description with no title line and no decision block. Reuse a brief when the risk tier permits; do not create
duplicate artifacts. If the PR body already describes this exact candidate (same base/head and dirty-tree
scope), update it in place rather than writing a second one.

- **Summary and Changes:** the actual candidate (base/head and dirty-tree scope), requirements and concrete
  before/after behavior. Summarize changed files; name unexpected scope.
- **Checks:** completed results (command, exit code, summary counts, a link to the
  [gate summary log](../../../docs/process/validation.md#evidence)), with relevant numerical/performance
  evidence (actual thresholds/measurements, not a pass/fail label) and any plot/trajectory output. Record
  missing/stale checks. A green local-mode GitHub check is not full validation. Do not rerun a current gate
  just to format a description.
- **Codex review:** verdict, thread id, findings, fixes, fix verification and the round counts.
- **Where to look:** consequential code, tests, assumptions and the end-to-end path a significant change
  actually traverses (see [Joe's engineering review](../../../docs/process/workflow.md#joes-engineering-review)),
  open findings, accepted risks and owner, compatibility/dependency/oracle decisions and rollback. Required
  validation or review missing means a draft with blockers, not ready for approval.
- **Needs Joe's decision:** each decision requested. Never write Joe's answer into the body: his explicit
  decision in the PR or his squash-merge records it (AGENTS.md); no GitHub approving-review badge is required.
  Changed candidates require the workflow's renewed validation/review.
- **Follow-ups:** nonblocking items and where each is tracked.
- For release preparation, also cover the workflow's merged-tree checks, version/provenance, artifact
  checksums if any, smoke checks, rollback and observation owner. Keep release permission separate from merge.
- Push the task branch and update the draft PR body
  ([AGENTS.md](../../../AGENTS.md#commands-and-boundaries)); verify the body with `gh pr view --json body`.
  Return the PR link and the exact remaining decision/next action.
