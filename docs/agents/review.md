# Codex review and adversarial review procedure

Inputs: shared rules, spec/plan or short brief, exact base/head and full diff (including new files),
relevant surrounding source/tests, accepted ADRs, and completed validation evidence. Start fresh;
record Claude as builder and Codex as reviewer. Output: [findings](../templates/review-findings.md), no code edits.

1. Confirm scope/risk and validation coverage. Identify missing evidence before claiming completion.
2. Trace requirements to behavior and tests; inspect ownership/lifetime, error paths, config defaults,
   plugin loading, ICD compatibility, regressions and test/oracle changes. Review surrounding callers.
3. For numerical/physics changes inspect FRD/NED and `q_nb` direction, units, Earth position semantics,
   finite/domain behavior, invariants, fixed-frame time, random seeds/transforms, RK4 coupling,
   allocations/exceptions on the per-frame path, and independent reference tolerances, per
   [numerical and physics validation](../process/validation.md#numerical-and-physics-validation).
   Cite binding ADRs.
4. Report only actionable defects supported by a location, triggering scenario, impact, and evidence.
   Critical: severe safety/data/security or fundamental correctness failure. Major: requirement failure,
   regression or missing required check. Minor: bounded maintainability/clarity issue. Mark suggestions
   as nonblocking; do not invent defects to meet a quota or rewrite requirements.
5. Verify builder fixes/rejections against the actual delta and surrounding invariants; demand fresh
   validation where behavior changed. Track each finding's history, not just the latest severity count.
6. State reviewed scope, remaining risks and whether critical/major items remain. An interrupted check
   or unverified fix cannot count as independent approval (nor can self-review; AGENTS.md).

For expensive unpinned designs, use one plan critique before implementation. For physics changes,
include the adversarial pass and Joe's inspection required by the [workflow](../process/workflow.md).
On Tier 0 and Tier 1, one pass on the final diff; report a finding that is not a factual error or defect as a
follow-up. Stop at the correction budget (AGENTS.md) or a requirements disagreement; report attempts and the
decision Joe needs.

Claude dispatches via [review-cycle](../../.claude/skills/review-cycle/SKILL.md);
[scope selection](review-dispatch.md) must cover both committed and uncommitted parts of a mixed candidate.
