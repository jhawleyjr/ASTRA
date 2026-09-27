# Agent procedures

Read [shared rules](../../AGENTS.md) and [workflow](../process/workflow.md) first.
These are portable procedures, not a new agent framework or automatic dispatch configuration.

- Claude planner: inspect Git, architecture/ADRs, affected source/tests, `justfile`, CMake and CI. Map each
  requirement to a change and executable acceptance. Use the [plan](../templates/implementation-plan.md).
  Resolve unstable interfaces before splitting work. A plan-only assignment may write the assigned
  spec/plan/brief, never product code, tests, or build/CI configuration; a written plan does not
  itself authorize implementation, which still follows the workflow's existing risk-based approval.
- Claude builder: [bounded implementation procedure](build.md).
- Claude validator: run [existing checks](../process/validation.md), independently check acceptance against the
  spec — confirming test cases and expected results trace to the spec/acceptance criteria and public
  interface, not only the implementation, with boundary and failure cases covered — and record actual
  outcomes and missing coverage. Do not change oracles to agree with output. Numerical/physics changes
  also follow [numerical and physics validation](../process/validation.md#numerical-and-physics-validation).
- Codex reviewer/adversarial reviewer: [independent review procedure](review.md); no product edits.
- Claude integrator: before applying packages, verify the intended target branch and exact base revision
  and start from a clean integration checkout on a task branch. If the assigned checkout contains
  unrelated changes, preserve them and use a separate integration worktree; never reset, clean, or stash
  another task's work. Combine assigned, completed packages in dependency order. Resolve mechanical
  conflicts; send semantic conflicts to the owner. Run the full gate on the integrated tree.
- Human: Joe's ownership is listed in [AGENTS.md](../../AGENTS.md#commands-and-boundaries).
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
