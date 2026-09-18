# Feature: [name]

Status: draft | approved | superseded
Owner: [human requirement owner]
Risk: [0–3 and reason]
Intent/approval: [Joe's instruction or decision reference; pending if absent]
Architecture/ADRs: [links]
System context: [where this fits in the existing data/scheduler flow; what produces its inputs
and what consumes its outputs. Link the relevant architecture section instead of restating it.]

## Outcome and boundaries

[Problem, user-visible result, motivation, non-goals, invariants, compatibility constraints.]

Assumptions: [what must already be true for this to work; what upstream components/models
guarantee rather than this feature re-checking; what this feature does not itself validate.]

## Contract and failure behavior

[Inputs/outputs, units, frames, time, lifecycle/reset semantics, valid domain, missing/invalid/NaN
inputs, errors/status behavior, determinism and seed requirements. Mark unresolved decisions.]

## Acceptance

| ID | Observable requirement | Executable check and independent expected result/source |
|---|---|---|
| R1 | [Given/when/then] | [Command, oracle source/page, tolerance rationale] |

[Performance/allocation bounds and numerical accuracy where applicable. Existing behavior to preserve.
Human-owned goldens/tolerances stay unchanged unless separately authorized.]

## Questions and decision

[Question, consequence, owner, answer/reference. Joe's spec approval/date or pending.]
