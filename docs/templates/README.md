# Workflow templates

Copy only what the [risk tier](../process/workflow.md#risk-tiers) needs. Replace bracketed placeholders;
remove irrelevant sections with a short reason. A short brief or PR body can contain the same fields.
Markdown tables are the task manifest format; no YAML schema, parser or service is required.

| Template | Suggested destination |
|---|---|
| [Feature spec](feature-spec.md) | `docs/specs/<feature>.md` |
| [Implementation plan](implementation-plan.md) | `docs/plans/<feature>.md` |
| [Task manifest/status](task-status.md) | `docs/tasks/<feature>/STATUS.md` for multiple packages |
| [Work package](work-package.md) | `docs/tasks/<feature>/<package>.md` |
| [Review findings](review-findings.md) | PR review, or `docs/reviews/<feature>-review.md` |
| [PR description](pr-description.md) | PR body |
| [ADR proposal](adr.md) | Draft with the plan; Joe authorizes a new `docs/decisions/NNNN-title.md` |

Keep exact revisions in the PR body and the gate summary log; task briefs may cite or link this provenance.
Standing rules should not carry transient revision pins. Link documents
rather than reproducing architecture or PLAN. Approval fields start pending; agents never fill in a
human decision that has not occurred. Joe's explicit decision or squash-merge is the solo-maintainer
approval record; no separate GitHub approving-review badge is required.
