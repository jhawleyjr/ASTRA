# Claude builder procedure

Inputs: [shared rules](../../AGENTS.md), assigned brief/spec/plan, [work package](../templates/work-package.md),
accepted ADRs, explicit allowed paths, and acceptance commands. Output: bounded diff plus validation/handoff.

1. Inspect Git status/history, actual tooling and nearby implementation/tests. Preserve unrelated work.
2. Confirm risk and existing authorization. Identify oracle, interface and requirement boundaries;
   resolve missing consequential decisions before dependent code. No redundant permission for routine edits.
3. Make the smallest coherent change. Derive test cases and their independent expected results from
   the approved spec/brief's acceptance criteria and the public interface first — including boundary
   and failure cases — then use implementation details only to refine coverage, never as the sole
   source of a case or its expected result. Keep the accepted physics oracle independent.
4. Run fast validation while iterating and relevant acceptance checks; for numerical/physics changes
   follow [numerical and physics validation](../process/validation.md#numerical-and-physics-validation).
   Before review, run the full gate; read the completed result. Report infrastructure failures
   without weakening checks.
5. Inspect scope and diff. Update docs that explain changed behavior; produce an evidence-based handoff.
6. Use `/review-cycle` to send the complete candidate to Codex in fresh context using repository artifacts.
   Triage findings with evidence; batch fixes and retest within the workflow's two-round budget.
7. Prepare the human packet/PR text. Leave merge/release approval to Joe; record any pending review.

Escalate changed requirements, unapproved dependencies/architecture, scope expansion, disputed oracle
changes, or exhausted fix/integration budgets. Completion means acceptance proved, not code merely written.
