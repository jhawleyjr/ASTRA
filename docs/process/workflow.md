# ASTRA development workflow — v3

Baseline requested by Joe, September 2026. Replaces v2's prohibition on feature artifacts with
optional templates and risk-based depth. Preserves `just check`, human-owned oracles, oracle/main protection
hooks, model settings, draft PRs, and Joe's squash-merge authority. `/wrap` is revised for stage-aware
handoff; its old mandatory push/open-PR closeout no longer applies. Git retains the v2 rationale.

## Sources of truth and lightweight state

[Architecture](../architecture.md) and accepted [ADRs](../decisions/) govern design.
[PLAN.md](../../PLAN.md) is the milestone checklist; [STATE.md](../../STATE.md) is the current handoff
(40 lines maximum, rewritten). Verify Git/PR state: a handoff can be stale after a merge.
[AGENTS.md](../../AGENTS.md) is the canonical shared rule file; `CLAUDE.md` imports it. Together they
stay under 100 lines. [Agent procedures](../agents/README.md) hold repeatable details.

A small task still needs only 3–10 lines of intent, acceptance commands, and binding ADR pointers.
Use [templates](../templates/README.md) for information that would otherwise be lost; never create
seven empty documents to satisfy a checklist. Specs describe behavior; plans describe implementation.
`docs/tasks/<feature>/STATUS.md` coordinates multi-package work only. Do not duplicate the milestone
checklist there. Keep transient revision pins out of standing rules. Check/review evidence records
exact base/head revisions; task/session handoffs may cite these when needed to identify validated
work. Prefer links to evidence over duplicated, stale pins. Reference data/goldens retain provenance.

## The twelve steps

| Step | Action and durable result | Exit condition |
|---|---|---|
| 1. Intent and architecture | Joe states outcome, boundaries, invariants; cite architecture/ADRs | Consequential ambiguities resolved |
| 2. Specification | Testable requirements, inputs/outputs, units/frames, edge/failure cases, non-goals | Required human spec approval recorded |
| 3. Repository-aware plan | Inspect code/tooling; map requirements to changes and checks | Risks/interfaces resolved; required plan approval recorded |
| 4. Decomposition | One package by default; dependencies and file ownership when splitting | Stable contracts, one writer per path/worktree |
| 5. Repository instructions | Read shared root rules and task inputs | No conflict with accepted design |
| 6. Reusable procedures | Use [Claude skills and role procedures](../agents/README.md#invocable-claude-skills) with existing checks | Bounded role, inputs, outputs, escalation known |
| 7. Validation | Fast loop, then full gate and task-specific acceptance | Commands finish successfully; evidence recorded |
| 8. Independent review | Different model, fresh context, actual diff plus spec/tests | Evidence-backed findings recorded |
| 9. Fix and retest | Builder resolves findings, reviewer verifies deltas | No unresolved critical/major findings; validation current |
| 10. Human review | Prepare [packet](../templates/human-review.md); Joe reads consequential changes | Explicit decision against the current candidate |
| 11. Merge and release | Draft PR → ready → Joe squash-merges; separate release decision | Current validation/review, protected merge, release checklist |
| 12. Coordination | Keep next action/owner visible in STATE or feature status | Handoff recoverable without chat; stop when complete |

**Stage transitions (implementation and integration).** The table keeps its original twelve
numbered steps; two stages that produce most of the actual work sit at explicit transitions
inside it rather than as separate rows, so a handoff can always name where a task stands.
*Implementation* is the bounded diff plus tests that steps 4–6 (decomposition, repository
instructions, reusable procedures) hand to step 7: it starts once steps 1–3 have resolved
intent, spec and plan, and is complete once step 7's fast/full validation has run against it
— see the [Claude builder procedure](../agents/build.md). *Integration* is the transition a
multi-package task adds between step 9 (fix and retest) and step 10 (human review): the
integrator combines every package's validated diff into one tree in dependency order and
reruns the full gate on the combined tree before that tree is eligible for step 10. A
single-package task (the default) has no separate integration transition, since step 7's own
full gate already covers the whole candidate. `/wrap` treats "implementation, validation or
integration incomplete" as one of its named unfinished-gate cases.

## Risk tiers

Assign the highest applicable tier with a one-line reason. Joe may raise it. Missing evidence or
uncertain numerical impact is reason to raise risk, not silently downgrade it.

| Tier | ASTRA examples | Minimum process |
|---|---|---|
| 0 — editorial | Prose, comments; no executable or policy behavior | Brief + links/consistency check; Joe's merge decision |
| 1 — low | Small internal change with no interface/numerical effect | Brief, focused checks + full gate, independent review |
| 2 — normal | Feature, build/CI/hook/workflow behavior, dependency proposal | Spec/plan (may share one short artifact), Joe approves intent/plan, full gate + acceptance, independent review, human packet |
| 3 — high | `core/kernel/`, numerical `core/util/`, `plugins/physical/`, `icd/`, transport/concurrency, health behavior, goldens/tolerances | Full relevant artifacts, Joe approves spec/plan, independent numerical/adversarial review and oracles, full gate, Joe reads critical diff/tests |

New dependencies, architecture/API decisions, security, and release authority always need Joe's
approval regardless of tier. Existing explicit authorization counts; do not request it again for
routine implementation choices. Tier 0 may omit the expensive product gate only when no execution,
build, test, or policy behavior changes; record the scope and checks instead of claiming a full pass.

## Git and worktrees

- Inspect `git status --short --branch`, `git worktree list`, and relevant history first. Never reset,
  clean, or stash another task's changes. One task/PR per session; keep existing branch conventions.
- Start new Claude tasks with `claude --worktree <name>`; generic agents may use the equivalent:
  `git fetch origin`, then `git worktree add .claude/worktrees/<name> -b worktree-<name> origin/main`.
  Replace placeholders; verify fetched main is the intended base. An explicitly assigned task branch wins.
- One writer per checkout. Each worktree has its own build directories and venv; never share those
  mutable outputs. Gitignored `docs/refs/` inputs must be read from the main checkout explicitly.
- Parallel builders are optional, only for independent owned paths with stable contracts and an
  explicit assignment. Prefer dependency waves; one integrator combines completed packages and runs
  full validation on the combined tree. Resolve semantic conflicts with the spec owner.
- Use coherent, descriptive commits. Open draft PRs; push only the task branch. Joe squash-merges.
  No agent merges, force-pushes shared history, changes branch protection, or publishes a release
  without explicit authorization. Permission to implement locally is not permission to send messages.

## Verification

[Validation commands and coverage](validation.md) are wrappers over the existing `justfile`.
Fast is `just test-unit`; full is `just check`. Run the full gate once on the final candidate,
then rerun after substantive fixes/integration. Redirect noisy output to a log, wait for the real
process to exit, read its status and `check: OK`; record failures and missing coverage honestly.
Do not run concurrent checks in one checkout. A timeout or absent dependency is not a pass.

The oracle remains human-owned: `tests/golden/**`, tolerance files, and `docs/decisions/**`.
Claude's PreToolUse guard requires Joe to authorize and lift it via `ASTRA_TOUCH_GOLDENS=1` on the
launch environment or `.claude/touch-goldens` in the session or main checkout. SessionStart deletes
the file marker in both locations; a hook cannot unset a running session's environment variable, so
`ASTRA_TOUCH_GOLDENS` lasts as long as the session it was launched with. Agents must not arm their
own bypass. Other tools must honor this rule even without
Claude hooks. Widening tolerance, narrowing comparison, regenerating a golden, or weakening a test
is a blocking finding unless Joe explicitly approves the underlying requirement/oracle change.

## Review

Joe's role assignment: **Claude owns planning, implementation, tests/validation, fixes, integration,
and handoff; Codex owns independent review and adversarial review**, including the one-off critique
of consequential plans below. These roles are not interchangeable unless Joe explicitly changes the
assignment. Joe remains the requirement owner, architect, human approver and merge/release authority.
Use fresh review context and record the actual model.
Review receives the spec/plan, base/head, diff, relevant surrounding code, tests, and validation
results, not just the builder's rationale. Reviewer is read-only; builder owns fixes.
See [review procedure](../agents/review.md) and [findings template](../templates/review-findings.md).

Claude runs [`/review-cycle`](../../.claude/skills/review-cycle/SKILL.md) using the installed companion;
[dispatch instructions](../agents/review-dispatch.md) cover working-tree/branch/mixed candidates.
Physics diffs also need adversarial review focused on ADRs, units/signs, domain validity, allocation,
and determinism. Joe reads these diffs. Review is not run on every turn; the Stop hook is the fast
check. If Codex review is unavailable, say so and leave review pending; self-review is not
independent review. An accepted human exception must be recorded explicitly.

For an unpinned design expensive to unwind, do one independent plan critique before Joe sees the
plan, including acceptance commands and ADRs. Resolve supported findings; explain rejected ones.
Skip this extra critique when the architecture already pins the design. Cowork/design chat is an
optional input, never a required hop.

## Review-fix limits and failure routing

Track each finding: ID, severity, file/line, concrete failure, evidence, requested outcome,
builder response, and reviewer verification. Status: open, fixed-unverified, verified, rejected,
deferred, or needs-human. Rejection needs evidence and reviewer agreement; disagreement goes to Joe.
Critical/major items cannot silently become follow-ups. Joe must explicitly accept any exception;
record its risk and owner. Minor improvements can be deferred without extending scope.

Batch fixes; add a meaningful regression test where behavior changes, rerun affected checks and the
full gate before final approval, and re-review changed areas plus affected invariants. Any substantive
push invalidates prior review/validation for affected work. Do not weaken requirements or tests.

- At most two autonomous correction/review-fix rounds per task; a finding surviving both escalates.
- At most two failed integration attempts. No infinite retry, review ping-pong, or reviewer refactoring.
- Record the running counts as `Correction rounds: n/2; failed integrations: n/2` in STATE.md's
  `Tried / failed` section (the task manifest's own fields when one exists), so a fresh session or
  reviewer reads the budget without chat history; a default single-package task still needs this line.
- Build/test defect → builder. Infrastructure failure → tooling diagnosis. Spec ambiguity → Joe.
  Architecture conflict/scope expansion → Joe. Exhausted budget → summary of attempts and next choice.
- A fresh context after two failed corrections is useful, but does not reset the budget or approval.
  Claude's Stop hook blocks the first failed stop attempt, then allows an honest handoff on
  `stop_hook_active` continuation without rechecking. Its exit 0 is not validation evidence or task
  completion; retain failures and run any necessary checks explicitly. It grants no extra fix rounds.

## Human approval and merge/release gates

Use [`/human-review`](../../.claude/skills/human-review/SKILL.md) to prepare a
[human review packet](../templates/human-review.md) before requesting approval: behavior,
requirements, diff scope, current check results/logs, findings, assumptions, critical files, rollback.
Joe approves the current PR candidate; later substantive changes require renewed review. Human
approval, AI review, validation, merge, and release are separate states, never inferred from labels.

**Checked-in configuration, not a claim about live GitHub settings:**
`.github/rulesets/main.json` requires PRs, squash merges, up-to-date `check`, no force pushes or deletion.
It intentionally requires **zero approving reviews**: Joe is the sole maintainer and approves by
reviewing the candidate and explicitly deciding to merge (or performing the squash-merge himself).
No separate GitHub approving-review badge or second human is required. The checked-in rules do not
automatically dismiss stale approvals or require thread resolution; Joe still inspects substantive
changes after his earlier review. Audit live settings before relying on protection; this baseline
does not change host rules or add approval checks.

`.github/workflows/check.yml` intentionally has **GATE=local** to conserve Joe's GitHub Actions
minutes: CI does not run the full product gate.
A green `check` must be paired with recorded local full-validation evidence for code/policy changes.
It triggers on opened/reopened/ready_for_review (not synchronize) and manual dispatch. After a later
push, rerun the checks on the new candidate: `gh pr ready --undo`, then `gh pr ready`; inspect the
actual current-head check results before merge. Do not treat an older green run as current evidence.
Returning to GATE=full is a separate cost/runtime decision; measure physics runtime before changing
coverage or timeouts. Existing CI and hook configuration remain the enforcement mechanisms.

ASTRA has no release/deployment pipeline yet. Before the first release:

1. Joe authorizes scope/version and rollback target; resolve current blocking findings.
2. Validate the clean merged tree with full gate and scenario/physics acceptance. A squash/merge
   creates a new revision; link its validation to the release evidence.
3. Record toolchain/dependency versions, config/seed/reference-data provenance, test results and
   limitations. The CMake project version is the existing source for Python's version.
4. Respect the source-built plugin/ICD contract: Python is editable-only today; do not invent a wheel
   or binary distribution. If artifacts are introduced, record checksums and reproducible build inputs.
5. Joe authorizes tagging/publishing/deploying and any signing credentials. Record release notes,
   smoke checks and rollback steps; verify the intended version after release. No automatic deployment.
6. Agree who observes the result and for how long; a regression becomes a test/lesson and a new task.

## Coordination and handoff

Use human-readable states, not an orchestration service:
`draft → ready → implementing → validating → reviewing ↔ fixing → human-review → ready-to-merge → merged`.
`ready` means required spec/plan decisions are approved; `reviewing` requires passing validation;
`human-review` requires no unresolved critical/major findings; `ready-to-merge` requires Joe's
current decision and checks (his own squash-merge may record that decision); only observed GitHub state establishes `merged`. Track release separately.
`blocked` includes reason, owner, and next action. Labels/status prose do not bypass a gate.

Use [task status](../templates/task-status.md) for multiple packages; otherwise STATE/PR suffice.
Use the revised [`/wrap`](../../.claude/skills/wrap/SKILL.md) when Claude pauses or finishes meaningful
work at any stage, including a local-only handoff or blocked validation. It records the first unfinished
gate, actual evidence, Codex findings/retry counts, human decisions and next action; it prepares the
human packet when appropriate. It does not force commit/push/PR creation, infer approval from tests,
or move to the next milestone while the current task is unfinished. Previously authorized Git actions
remain authorized; handoff alone does not authorize them. Append lessons only when something actually
bit. Review process cost when useful from existing evidence; the obsolete retrospective skill is
removed and no retro ceremony blocks a milestone. Add automation only for demonstrated repetitive work.

## Model and effort

Preserve `.claude/settings.json`: `opusplan`, `high`, Sonnet subagents. No recon subagents for this
repository. Model/effort escalation is Joe's decision; physics correctness comes from independent
oracles and review. One PR per context; log verbose output. Templates record the assigned provider/model
without changing provider settings. Claude does the work; Codex reviews it. No new agent service or dependency is needed.

## Implementation boundaries

The project skills are instructions invoked by Claude; they do not themselves enforce a merge/release
state machine. Local validation, Codex output, human decisions and actual Git state supply the evidence.
The Stop hook supplies feedback, not approval. Its bounded continuation behavior follows the
[Claude hook contract](https://code.claude.com/docs/en/hooks#stop); project skill discovery follows the
[Claude skill contract](https://code.claude.com/docs/en/skills). No autonomous dispatcher is installed.
