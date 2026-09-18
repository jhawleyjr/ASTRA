---
name: plan-feature
description: Prepare an ASTRA feature specification, repository-aware plan and bounded work packages from Joe's intent. Use before substantial implementation or when requirements or interfaces need clarification.
---

# Plan an ASTRA change

Claude owns planning. Read [shared rules](../../../AGENTS.md), the
[risk tiers](../../../docs/process/workflow.md#risk-tiers), architecture/ADRs and the assigned intent.
Inspect actual Git state, relevant implementation/callers/tests, tooling and constraints before planning.

1. Establish outcome, non-goals, invariants and risk. Reuse Joe's existing authorization and approved
   decisions; identify only consequential missing decisions. This pass writes the assigned spec/plan/
   brief only, never product code, tests, or build/CI configuration; a written plan does not itself
   authorize implementation, which still follows the workflow's existing risk-based approval.
2. Write testable behavior using the [spec fields](../../../docs/templates/feature-spec.md): contracts,
   units/frames, failure/reset cases, compatibility and independent acceptance. Capture assumptions
   (what must already be true, what upstream components/models guarantee rather than this feature
   re-checking, what it does not itself validate) and system context (where it fits in the existing
   data/scheduler flow, its producers and consumers), linking architecture instead of restating it.
   Link authoritative sources/oracles rather than inventing tolerances or moving human-owned tests.
3. Map requirements to owned paths, existing abstractions and executable checks using the
   [plan fields](../../../docs/templates/implementation-plan.md); distinguish observed facts from
   assumptions and proposed changes, citing paths/symbols for consequential ones (keep routine plans
   concise). Check the spec's assumptions against the repository; record mismatches and consequences
   instead of silently rewriting requirements or expanding scope, and take a consequential conflict to
   Joe before dependent implementation, reconciling spec and plan with his decision -- independent
   planning continues meanwhile. Note uncertainty, rollback and required human decisions. A small
   change can keep spec and plan together in a short brief.
4. Use one Claude builder by default. Only split genuinely independent work after interfaces are
   stable; use [packages](../../../docs/templates/work-package.md) and a
   [manifest](../../../docs/templates/task-status.md) for dependencies, allowed/forbidden paths,
   worktrees, acceptance and one integrator. Shared-write conflicts require sequencing.
5. For an expensive design not settled by architecture/ADRs, obtain one fresh, read-only Codex plan
   critique following [review dispatch](../../../docs/agents/review-dispatch.md#plan-critique).
   Include the plan and acceptance in the request; verify claims and record accepted/rejected findings.
   Other plans skip that extra pass. Codex does not implement the plan.
6. Record actual approvals or pending questions and next owner. Produce links and a Claude build
   brief; never label a spec/plan approved based on model review or elapsed time. Finish with `/wrap`
   if handing off. Required unresolved decisions block only the dependent work.
