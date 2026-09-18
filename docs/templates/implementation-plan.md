# Implementation plan: [feature]

Spec/brief: [link]
Risk: [tier and reason]
Status: draft | approved | superseded
Plan approval: [Joe's existing authorization/decision reference or pending]

## Repository findings

[Existing abstractions, affected paths and callers, ownership/lifetime, tests, build/toolchain/CI
constraints. Distinguish implemented paths from future architecture. Note current Git/unrelated work.]

## Mapping and sequence

| Requirement | Change / owned paths | Dependencies / interface contract | Acceptance command |
|---|---|---|---|
| R1 | [smallest change] | [stable contract or prior package] | [existing check plus targeted test] |

[One Claude builder by default. If splitting, name integrator and dependency waves; no overlapping writers.]

## Validation and risks

[Fast/full gate, behavior regressions, independent oracle/reference, boundary/failure tests, numerical
and performance evidence as applicable. Compatibility, migration/rollback and documentation changes.
Scope exclusions, new dependencies or human-owned files requiring separate approval.]

## Plan critique and decisions

[One Codex critique for expensive unpinned design, otherwise explain why unnecessary.
Findings accepted/rejected with evidence, unresolved questions/owner, Joe's decision.]

## Done when

[Requirements proved, complete validation, independent review, human packet; links to work packages
only if needed. Implementation completion is distinct from human approval/merge/release.]
