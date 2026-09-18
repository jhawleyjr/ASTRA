# Agent procedures

Read [shared rules](../../AGENTS.md) and [workflow](../process/workflow.md) first.
These are portable procedures, not a new agent framework or automatic dispatch configuration.

- Claude planner: inspect Git, architecture/ADRs, affected source/tests, `justfile`, CMake and CI. Map each
  requirement to a change and executable acceptance. Use the [plan](../templates/implementation-plan.md).
  Resolve unstable interfaces before splitting work; plan-only assignments do not edit product code.
- Claude builder: [bounded implementation procedure](build.md).
- Claude validator: run [existing checks](../process/validation.md), independently check acceptance against the
  spec, and record actual outcomes and missing coverage. Do not change oracles to agree with output.
- Codex reviewer/adversarial reviewer: [independent review procedure](review.md); no product edits.
- Claude integrator: combine assigned, completed packages in dependency order in one checkout. Resolve
  mechanical conflicts; send semantic conflicts to the owner. Run the full gate on the integrated tree.
- Human: Joe owns intent, architecture, oracle changes, consequential approvals, merge and release.
- Claude Code: [provider procedures](claude-code.md), existing hooks and the revised
  [`/wrap` handoff skill](../../.claude/skills/wrap/SKILL.md) for any workflow stage.

For a normal feature use one Claude builder plus a fresh Codex reviewer. Claude owns all execution
roles; Codex owns review and adversarial review (including consequential-plan critiques). Roles
can be performed sequentially; no parallel agents are required. Each handoff contains repository
artifacts, scope, acceptance commands, and evidence, so it survives the loss of chat context.

## Invocable Claude skills

- [`/plan-feature`](../../.claude/skills/plan-feature/SKILL.md): intent → spec → plan → bounded packages.
- [`/review-cycle`](../../.claude/skills/review-cycle/SKILL.md): Codex review/adversarial dispatch, findings,
  Claude fixes, retest and Codex verification within the task budget.
- [`/human-review`](../../.claude/skills/human-review/SKILL.md): Joe's evidence/decision packet.
- [`/wrap`](../../.claude/skills/wrap/SKILL.md): handoff at the current stage, including blockers.

These skills reference the canonical rules/templates; they do not introduce a scheduler or extra
approval gates. The former retrospective command was removed; review process costs when useful
without a separate mandatory or maintained skill. See [dispatch details](review-dispatch.md) for the
installed companion's candidate-scope behavior.
