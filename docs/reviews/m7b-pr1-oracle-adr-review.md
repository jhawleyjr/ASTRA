# Review: M7b PR 1, ADR-0034 (the in-house oracle rule) and architecture rev 16

Spec/plan: [PR brief](../plans/m7b-pr1-oracle-adr.md), [milestone plan](../plans/m7b-closed-loop-oracles.md)
Builder: Claude Opus 5.5
Reviewer: Codex (companion 1.0.6, default model, `model_reasoning_effort = "high"` from `~/.codex/config.toml`;
fresh context; adversarial review against ADR-0013, ADR-0028, ADR-0029, validation.md and the M7b plan),
thread `01a14c53-61e5-74d0-bc4b-7ba50ff237cf`
Scope: committed branch diff `origin/main` `7a92115`..`895cebe` (clean tree): the candidate
`docs/plans/m7b-pr1-adr-0034-candidate.md`, the brief, `docs/architecture.md`,
`docs/plans/m7b-closed-loop-oracles.md`, README.md, PLAN.md
Validation: `just docs-check` exit 0, the scratch link check (candidate and both plans) 0 problems, and
`git diff --check origin/main..HEAD` exit 0 at `c070df3`; the full gate's result is in the PR body
Round: fix 1

Verdict: needs-attention, one finding. Codex reports the cited paths, test names and quotes check out, and the
architecture edits match amendment 11.

## Finding [R-001]

Severity: major (Codex medium)
Status: fixed-unverified
Location: `docs/plans/m7b-pr1-adr-0034-candidate.md:63-65` and `:109-113` at `895cebe`
Requirement/ADR: validation.md's numerical section, step 6; the M7b plan's Q12 and R13; ADR-0013
Failure: Decision 1 says "a tolerance is never derived from ASTRA's own output", but Decision 4's measured
sensitivity yardstick computes its bound from perturbed runs of the path under test, as Q12 requires for R13.
Read literally, R13 cannot satisfy both clauses, so a reviewer would hold two conflicting acceptance rules.
Requested outcome: distinguish prohibited fitting to the judged discrepancy from the pre-registered
sensitivity computation, and state how it relates to validation.md step 6 while keeping ADR-0013's and
ADR-0028's external-band prohibition.
Builder response: fixed. Verified against the candidate's text, Q12 (`m7b-closed-loop-oracles.md:666-676`) and
validation.md step 6. No decision changes; Q12 already decided the yardstick.
- Decision 1's clause now reads "a tolerance is never fitted to ASTRA's own output: no bound is set from the
  discrepancy it judges", and names the yardstick as the one bound computed from runs of the path under test,
  within Decision 4's limits.
- Decision 4's yardstick gains "Why it is not a fitted tolerance": its runs measure sensitivity to the fixed
  perturbation set and never read the judged discrepancy; validation.md step 6 sends a case no derivation
  reaches to Joe, and Q12 is his decision for heading invariance; each later use is pre-registered in a plan
  Joe approves; ADR-0013's and ADR-0028's external bands keep their prohibition.
Verification: pending (Codex verification pass of this fix batch).
History: initial review, open; fix 1, fixed-unverified.
Human exception if needed: none.

## Completion

Pending the verification pass.
