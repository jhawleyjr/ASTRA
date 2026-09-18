---
name: human-review
description: Prepare Joe's ASTRA engineering review packet from the current candidate, completed validation and Codex findings. Use at the human-review checkpoint or when Joe requests a packet; disclose incomplete gates in a draft.
---

# Prepare Joe's review packet

Claude owns preparation; Joe owns the decision. Read the
[human-review and release gates](../../../docs/process/workflow.md#human-approval-and-mergerelease-gates)
and use the [packet template](../../../docs/templates/human-review.md) in an existing PR body draft
or `docs/reviews/`. Reuse a brief when the risk tier permits; do not create duplicate artifacts.
Check the PR body and `docs/reviews/` for an existing packet covering this exact candidate (same
base/head and dirty-tree scope) first; update it in place rather than writing a second one.

- Identify the actual candidate (base/head and dirty-tree scope), requirements and concrete before/after
  behavior. Summarize changed files and point Joe to consequential code, tests, assumptions, and the
  end-to-end path a significant change actually traverses (see [Joe's engineering
  review](../../../docs/process/workflow.md#joes-engineering-review)).
- Link completed local validation/logs, relevant numerical/performance evidence (actual
  thresholds/measurements, not a pass/fail label) and any available plot/trajectory output, and
  independent Codex findings/fix verification. Record exact commands, results and missing/stale
  checks. A green local-mode GitHub check is not full validation. Do not rerun a current gate just
  to format a packet.
- State open findings, accepted risks and owner, compatibility/dependency/oracle decisions and rollback.
  Required validation or review missing means a draft with blockers, not ready for approval.
- Record the decision being requested and Joe's actual decision if already given. His explicit decision
  or own squash-merge is sufficient; no second human or GitHub approving-review badge is required.
  Changed candidates require the workflow's renewed validation/review. Never invent approval.
- For release preparation, cover the workflow's merged-tree checks, version/provenance, artifact checksums
  if any, smoke checks, rollback and observation owner. Keep release permission separate from merge.
- Save locally unless updating the PR is already authorized. Packet preparation itself does not push,
  publish, merge or release. Return the packet location and the exact remaining decision/next action.
