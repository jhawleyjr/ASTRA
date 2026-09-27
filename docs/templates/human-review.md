# Human review packet: [feature]

PR / candidate base and head: [exact revisions in this evidence record; confirm base is `main`'s
current tip before requesting the merge decision, per workflow.md's final-integration guidance —
name it explicitly if `main` has advanced and what, if anything, that required re-validating]
Spec/plan/task: [links or brief]
Risk: [tier and reason]
Decision requested: [approve behavior/merge/release; distinguish each]

## What changes and why

[Concrete before/after behavior, requirement coverage, diff summary/stat, unexpected scope.]

## Evidence

| Check / acceptance | Result / date / duration | Summary log / candidate |
|---|---|---|
| [fast/full/targeted/link checks] | [pass/fail/not run, actual exit, summary counts] | [`<head>-gate-summary.log` link and revision; dirty-tree disclosure] |

[Toolchain, relevant seeds/reference provenance, numerical/performance changes, coverage limitations.
CI is intentionally local-gate mode to conserve GitHub minutes; green CI is not a full-gate result.]

## Independent review

[Claude builder/Codex reviewer models, Codex verdict and thread id, findings, fix rounds, remaining
critical/major/minor, rejected or accepted-risk findings with evidence. If unavailable, explicitly pending.]

## Joe's attention

[Critical files/tests and consequential decisions, assumptions, risks, changed dependencies/interfaces,
rollback/revert plan. For release: merged-tree validation, version/provenance, smoke checks, rollback.]

## Decision record

State: pending | approved | changes-required
Human decision/date/reference: pending
Approved candidate/scope: pending
Merge observed: not merged
Release authorization: pending / not requested

Significant tradeoff rationale: [what was accepted and why, tied to the evidence above, e.g. a
measured cost against a stated budget; omit if nothing was traded off]
Accepted risks: [risk, owner, follow-up trigger; "none" is valid]
Nonblocking follow-ups: [tracked item per deferred minor/suggestion finding or improvement idea;
these do not block this decision]
Architectural decisions: [none, or a link to the accepted/proposed ADR this candidate introduces
or depends on, per [the ADR template](adr.md); never record an architecture decision only here]

If `changes-required`, name the actual defect category and follow
[workflow.md's routing](../process/workflow.md#joes-engineering-review): implementation defect
(fix/retest/re-review), planning defect (revise plan), requirements gap (revise specification),
or architecture conflict (Joe's own architecture decision/ADR); never patch around a wrong plan
or a missing requirement through repeated fix rounds.

Joe's explicit decision or squash-merge records approval in this solo-maintainer repo; no GitHub
approving-review badge is required. Do not infer approval from a model, passing check, or elapsed time.
A substantive push after approval needs renewed inspection and validation of the changed candidate.
