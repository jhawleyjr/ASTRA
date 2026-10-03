# ASTRA development workflow — v3

Baseline requested by Joe, September 2026. Replaces v2's prohibition on feature artifacts with
optional templates and risk-based depth. Preserves `just check`, human-owned oracles, oracle/main protection
hooks, model settings, draft PRs, and Joe's squash-merge authority. Git retains the v2 rationale.
Standing rules are stated once, in [AGENTS.md](../../AGENTS.md); this file holds procedure and rationale and links there.

## Sources of truth and lightweight state

[Architecture](../architecture.md) and accepted [ADRs](../decisions/) govern design.
What [PLAN.md](../../PLAN.md) and [STATE.md](../../STATE.md) (40 lines maximum) hold, and what they never
record, is in [AGENTS.md](../../AGENTS.md#read-first). STATE.md is written as main reads it after the merge,
under exactly `## Next step`, `## Open threads` and `## Honesty rules`, which `just docs-check` enforces
([`/wrap`](../../.claude/skills/wrap/SKILL.md) step 5). Rationale: a fact that becomes true only at merge made
every such edit stale on merge and forced bookkeeping PRs (`docs/lessons.md`, 2026-09-26). Verify Git/PR state
with `git log origin/main` rather than trusting either file. `CLAUDE.md` imports AGENTS.md;
[agent procedures](../agents/README.md) hold repeatable details.

A small task still needs only 3–10 lines of intent, acceptance commands, and binding ADR pointers.
Use [templates](../templates/README.md) for information that would otherwise be lost; never create
seven empty documents to satisfy a checklist. Specs describe behavior; plans describe implementation.
`docs/tasks/<feature>/STATUS.md` coordinates multi-package work only. Do not duplicate the milestone
checklist there. Handoffs link the PR body's exact base/head revisions rather than duplicating
pins. Reference data/goldens retain provenance.

## The twelve steps

| Step | Action and durable result | Exit condition |
|---|---|---|
| 1. Intent and architecture | Joe states outcome, boundaries, invariants; cite architecture/ADRs | Consequential ambiguities resolved |
| 2. Specification | Testable requirements, inputs/outputs, units/frames, edge/failure cases, non-goals | Required human spec approval recorded |
| 3. Repository-aware plan | Inspect code/tooling; map requirements to changes and checks | Risks/interfaces resolved; required plan approval recorded |
| 4. Decomposition | One package by default; dependencies and file ownership when splitting | Stable contracts, one writer per path/worktree |
| 5. Repository instructions | Read shared root rules and task inputs | No conflict with accepted design |
| 6. Reusable procedures | Use [Claude skills and role procedures](../agents/README.md#invocable-claude-skills) with existing checks | Bounded role, inputs, outputs, escalation known |
| 7. Validation | Fast loop, then full gate and task-specific acceptance | Commands finish successfully; results in the PR body |
| 8. Independent review | Different model, fresh context, actual diff plus spec/tests | Evidence-backed findings in the PR body |
| 9. Fix and retest | Builder resolves findings, reviewer verifies deltas | No unresolved critical/major findings; validation current |
| 10. Human review | Write the [PR description](../templates/pr-description.md); Joe reads consequential changes | Explicit decision against the current candidate |
| 11. Merge and release | Draft PR → ready → Joe squash-merges; separate release decision | Current validation/review, protected merge, release checklist |
| 12. Coordination | Keep next action/owner visible: STATE (next task), PR body (this PR's stage) or feature status | Handoff recoverable without chat; stop when complete |

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

This package-level integration is a different thing from **final integration with `main`** at
step 11: before merge, fetch and check whether `main` has advanced past the candidate's own
validated base (`git fetch origin && git log --oneline HEAD..origin/main`), not just whether the
candidate itself is green. If `main` moved with changes that overlap or interact with this
candidate, the merged result (`main` + candidate) is a combined state nothing has actually
validated yet: merge `origin/main` into the task branch (Joe's squash-merge flattens the merge
commits), then rerun the checks the assigned
risk tier requires against that combined state (at minimum fast validation; the full gate for tier
2-3, per [risk tiers](#risk-tiers)), and renew Codex review and Joe's decision for whatever the
merge materially touched — an approval given against the old base does not carry over to
a substantively different combined tree. GitHub's own required-status-check policy already
refuses a stale check on this repository's protected branch (`strict_required_status_checks_policy`,
live-verified below): a PR behind `main` cannot merge on an old green run. If `main` only moved in
unrelated, non-overlapping paths, the existing validation/review evidence for the exact candidate
still covers it — do not rerun checks that evidence already answers.

## Risk tiers

Assign the highest applicable tier with a one-line reason. Joe may raise it. Missing evidence or
uncertain numerical impact is reason to raise risk, not silently downgrade it.

| Tier | ASTRA examples | Minimum process |
|---|---|---|
| 0 — editorial | Prose, comments; no executable or policy behavior | Brief + links/consistency check; one Codex pass on the final diff (Joe may waive); Joe's merge decision |
| 1 — low | Small internal change with no interface/numerical effect | Brief, focused checks + full gate; one Codex pass on the final diff (Joe may waive) |
| 2 — normal | Feature, build/CI/hook/workflow behavior, dependency proposal | Spec/plan (may share one short artifact), Joe approves intent/plan, full gate + acceptance, independent review, PR description for Joe |
| 3 — high | `core/kernel/`, numerical `core/util/`, `plugins/physical/`, `aircraft/**/physical`, `aircraft/**/hardware`, `icd/`, transport/concurrency, health behavior, goldens/tolerances | Full relevant artifacts, Joe approves spec/plan, independent numerical/adversarial review and oracles, full gate, Joe reads critical diff/tests |

Joe's approvals ([AGENTS.md](../../AGENTS.md#commands-and-boundaries)) apply regardless of tier; security
decisions are his too. Tier 0 may omit the expensive product gate only when no execution, build, test, or
policy behavior changes; record the scope and checks instead of claiming a full pass. On Tier 0 and Tier 1,
a finding that is not a factual error or defect becomes a follow-up, not a correction round. Rationale: a
docs-only closeout once took six commits, two correction rounds and three reviews over nits.

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
- The standing push/draft-PR authorization and its limits are in [AGENTS.md](../../AGENTS.md#commands-and-boundaries).
  Keep the draft PR's body current with the brief, description and results. Changing branch protection also needs
  Joe's explicit authorization. Merging `origin/main` into the task branch is the required integration step
  (final integration with `main`, step 11), not a prohibited merge.
  A push after the PR opens does not re-run CI's `check`; see the `gh pr ready --undo` / `gh pr ready` rerun under
  [Human approval and merge/release gates](#human-approval-and-mergerelease-gates) before merge. That rerun re-triggers `check` and is not itself a merge signal.

## Verification

[Validation commands and coverage](validation.md) are wrappers over the existing `justfile`.
Fast is `just test-unit`; full is `just check`. Run the full gate once on the final candidate.
After a passing full gate, a change limited to Markdown files and evidence logs (docs, plans, README,
PLAN.md, STATE.md, lessons, `docs/reviews/evidence/` logs) needs only `just docs-check` and
`git diff --check <gated-head>..HEAD`, not a new gate; `git diff --name-only <gated-head>..HEAD` shows the
delta, and the PR body names the gated head and that Markdown-only delta. Any other changed file (a code
comment or docstring, a test, fixture, config, script, hook or setting) makes the gate stale, as do
integration and substantive fixes. Redirect noisy output to a log, wait for the real
process to exit, read its status and `check: OK` (the pass condition in AGENTS.md); record failures and
missing coverage honestly, in the PR body, with the trimmed log from [validation](validation.md#evidence).
Do not run concurrent checks in one checkout. A timeout or absent dependency is not a pass.

The oracle boundary is in [AGENTS.md](../../AGENTS.md#commands-and-boundaries); the guard covers
`tests/golden/**`, tolerance files, and `docs/decisions/**`. Claude's PreToolUse guard requires Joe to authorize and lift it via `ASTRA_TOUCH_GOLDENS=1` on the
launch environment or `.claude/touch-goldens` in the session or main checkout. SessionStart deletes
the file marker in both locations; a hook cannot unset a running session's environment variable, so
`ASTRA_TOUCH_GOLDENS` lasts as long as the session it was launched with. Agents must not arm their
own bypass. Other tools must honor this rule even without
Claude hooks. Widening tolerance, narrowing comparison, regenerating a golden, or weakening a test
is a blocking finding unless Joe explicitly approves the underlying requirement/oracle change.

## Review

Roles are in [AGENTS.md](../../AGENTS.md); Codex's review work includes the one-off critique of
consequential plans below. Roles change only when Joe reassigns them. Record the actual model.
Review receives the spec/plan, base/head, diff, relevant surrounding code, tests, and validation
results, not just the builder's rationale. Reviewer is read-only; builder owns fixes.
See [review procedure](../agents/review.md) and [findings template](../templates/review-findings.md).

Claude runs [`/review-cycle`](../../.claude/skills/review-cycle/SKILL.md) using the installed companion;
[dispatch instructions](../agents/review-dispatch.md) cover working-tree/branch/mixed candidates.
Physics diffs also need adversarial review focused on ADRs, units/signs, domain validity, allocation,
and determinism. Joe reads these diffs. Review is not run on every turn; the Stop hook is the fast
check. An accepted human exception to review must be recorded explicitly.

For an unpinned design expensive to unwind, do one independent plan critique before Joe sees the
plan, including acceptance commands and ADRs. Resolve supported findings; explain rejected ones.
Skip this extra critique when the architecture already pins the design. Cowork review applies to
Tier 2 and Tier 3 work, or when Joe asks; on Tier 0 and Tier 1 its findings default to follow-ups
unless something is factually wrong. Otherwise Cowork/design chat is an optional input, never a required hop.

## Review-fix limits and failure routing

Track each finding: ID, severity, file/line, concrete failure, evidence, requested outcome,
builder response, and reviewer verification. Status: open, fixed-unverified, verified, rejected,
deferred, or needs-human. Rejection needs evidence and reviewer agreement; disagreement goes to Joe.
Critical/major items cannot silently become follow-ups. Joe must explicitly accept any exception;
record its risk and owner. Minor improvements can be deferred without extending scope.

Batch fixes; add a meaningful regression test where behavior changes, rerun affected checks and the
full gate before final approval (unless the fixes are Markdown-only, per [Verification](#verification)),
and re-review changed areas plus affected invariants. Any substantive
push invalidates prior review/validation for affected work. Do not weaken requirements or tests.

- The budget (two correction rounds, two failed integrations) is in AGENTS.md; a finding surviving
  both rounds escalates. No infinite retry, review ping-pong, or reviewer refactoring.
- Record the running counts as `Correction rounds: n/2; failed integrations: n/2` in the PR body (the
  task manifest's own fields when one exists), the one place a fresh session or reviewer reads the budget
  without chat history; STATE.md never copies them.
- Build/test defect → builder. Infrastructure failure → tooling diagnosis. Spec ambiguity → Joe.
  Architecture conflict/scope expansion → Joe. Exhausted budget → summary of attempts and next choice.
- A fresh context after two failed corrections is useful, but does not reset the budget or approval.
  Claude's Stop hook blocks the first failed stop attempt, then allows an honest handoff on
  `stop_hook_active` continuation without rechecking. Its exit 0 is not validation evidence or task
  completion; retain failures and run any necessary checks explicitly. It grants no extra fix rounds.

## Human approval and merge/release gates

### Joe's engineering review

By step 10 the candidate has passed the full local gate and independent Codex review with no
unresolved critical/major findings (steps 7-9); this is not re-inspecting every line, it is
judging engineering decisions and evidence before deciding. Start from the spec/plan and the
[PR description](../templates/pr-description.md)'s acceptance evidence, not the diff: confirm the
candidate actually matches what was approved before reading implementation. Read the changed
tests early — they show how the builder interpreted each requirement, and a green gate proves
nothing if the tests misunderstood the behavior. Trace consequential behavior end-to-end along
its actual path (environment → transport exchange → vehicle wrenches → EOM → integrator →
post-step → logger, per `docs/architecture.md`'s per-frame order, or the relevant subset) rather
than reading files alphabetically.

Give disproportionate scrutiny to:

- Deleted behavior — why, and whether a test covered the path that is now gone.
- Unexpected scope — diff size/shape against the stated intent.
- Public interfaces — ownership, units, validity semantics, ease of misuse, cost to extend later.
- Unnecessary complexity — could this be substantially simpler without losing a requirement.

For numerical/physics changes, read [numerical and physics validation](validation.md#numerical-and-physics-validation)'s
actual evidence yourself — real thresholds and measurements, not a pass/fail label — and look
at a relevant plot or trajectory output where one exists; a numerical pass can still hide a
wrong-looking transient a human notices at a glance. Before deciding, ask what failure mode the
existing tests could still plausibly miss (a rare state transition, a timing/ordering assumption,
a configuration interaction) — this is usually worth more than another general read of clean
code. Match depth to the task's assigned [risk tier](#risk-tiers): quick for tier 0-1, diff plus
tests plus evidence for tier 2, deep inspection of algorithms/state/interfaces for tier 3. A
non-blocking observation becomes a recorded follow-up, not a merge blocker, using the same
critical/major-versus-minor distinction as [review-fix limits](#review-fix-limits-and-failure-routing).

When the decision is not a clean approval, route by what actually failed: an implementation
defect returns to step 9 (fix, retest, re-review); a planning defect returns to step 3 (revise
the plan); a requirements gap returns to step 2 (revise the specification, then downstream
plan/tests/code); an architecture conflict is Joe's own step 1 decision, recorded as a new or
superseding [ADR](../templates/adr.md) when it changes an accepted one. The workflow is not
strictly linear — a human-review finding just names which earlier step actually needs
revisiting, per [the twelve steps](#the-twelve-steps) above. Joe's decision is recorded by his own PR
comment or squash-merge, never written into the PR description by an agent.

Use [`/human-review`](../../.claude/skills/human-review/SKILL.md) to write the
[PR description](../templates/pr-description.md) before requesting approval: behavior,
requirements, diff scope, current check results (in the PR body), findings, assumptions, critical files,
rollback. Joe approves the current PR candidate; later substantive changes require renewed review. Human
approval, AI review, validation, merge, and release are separate states (approval: AGENTS.md).

**What actually enforces this — four different things, not one:**

- *Documented instructions*: this file, `AGENTS.md`, and the skills/hooks' own comments — process
  Claude, Codex, and Joe follow; GitHub does not check any of it.
- *Local hook coverage*: `.claude/hooks/no-main.sh`, a PreToolUse hook on this session's own Bash
  tool — recognizes a literal `git push`/`commit`/`merge`/`rebase`/... touching `main`, and a commit
  message carrying a co-author trailer; nothing outside this session, and no `gh`-based route (below).
- *Checked-in host configuration*: `.github/rulesets/main.json` — a record of the intended rules,
  not proof they are live. Beyond the rules listed below, it requires a pull request for every change
  to `main`. The file is the ruleset. It was applied once, from a terminal where `gh` is logged in, with
  `gh api -X POST repos/jhawleyjr/ASTRA/rulesets --input .github/rulesets/main.json`. After an
  authorized edit, re-apply it with
  `gh api -X PUT repos/jhawleyjr/ASTRA/rulesets/<id> --input .github/rulesets/main.json`
  (`gh api repos/jhawleyjr/ASTRA/rulesets` lists the id).
- *Verified live host enforcement*, checked directly against the GitHub API, 2026-09-17 (re-verify
  periodically; host settings can change without this doc updating): the checked-in file matches
  the live, active `protect-main` ruleset on `refs/heads/main` exactly. Deletion and non-fast-forward
  pushes are rejected, only squash-merge is an allowed method, and the `check` status must pass on
  an up-to-date branch (`strict_required_status_checks_policy`, so a PR behind `main` cannot merge
  on a stale green run — the live backstop for "final integration" above). The `gh`/git identity
  this repository's sessions authenticate as reports `current_user_can_bypass: "never"` on that
  ruleset, and is also the repository's sole collaborator (admin role); no separate, more
  restricted credential exists for an agent versus Joe himself. The ruleset intentionally requires
  **zero approving reviews**: Joe is the sole maintainer and approves by reviewing the candidate
  and explicitly deciding to merge (or performing the squash-merge himself); no separate GitHub
  approving-review badge or second human is required, and the rules do not auto-dismiss stale
  approvals or require thread resolution.

That live enforcement covers push/delete/merge-method/stale-check integrity on `main`. It does
**not** attest that full local validation ran, that Codex review completed, or that Joe actually
made a decision: the required `check` context is `GATE=local` (docs/hooks/lint scope only, below),
and GitHub has no way to verify an off-host review or a human judgment — that remains documented
process, trusted because Claude/Codex/Joe follow it, not because the host checks it. Do not claim
that local hook/CI configuration alone mechanically prevents an agent from bypassing these gates:
the real backstop against a direct push/force-push/delete/wrong-merge-method on `main` is the live
ruleset, which a `gh`-based route does not evade either (the ruleset applies to the ref regardless
of which client pushes to it) — the actual gap is that `no-main.sh` itself does not *also* warn on
that route locally, per its own scope above. The backstop against a merge without Joe's actual
review is his own process discipline, not a host or credential control, given the deliberate
zero-approving-reviews design and the shared `gh` identity. No deployment credentials or
environments exist yet (verified: zero repository secrets, zero environments), so the
least-privilege boundary a future release credential needs is a decision to make when one is
introduced, not a current gap.

If this residual gap is worth closing, the smallest concrete change is extending `no-main.sh`'s
command matcher to also recognize a `gh` merge/delete call against `main`, parallel to its existing
`git` patterns, as local defense-in-depth. Tradeoff: it adds no host-level enforcement, since the
same credential could still call the API directly outside this hook's reach, and it still cannot
verify that Joe's actual review happened — it only narrows the one concretely-identified blind spot
in the local hook itself. This needs Joe's authorization before editing the hook; not made in this pass.

`.github/workflows/check.yml` intentionally has **GATE=local** to conserve Joe's GitHub Actions
minutes: CI does not run the full product gate.
A green `check` must be paired with recorded local full-validation evidence for code/policy changes.
It triggers on opened/reopened/ready_for_review (not synchronize) and manual dispatch. After a later
push, rerun the checks on the new candidate: `gh pr ready --undo`, then `gh pr ready`; inspect the
actual current-head check results before merge. Do not treat an older green run as current evidence.
Returning to GATE=full is a separate cost/runtime decision; measure physics runtime before changing
coverage or timeouts.

ASTRA has no release/deployment pipeline yet. Before the first release:

1. Joe authorizes scope/version and rollback target; resolve current blocking findings.
2. Identify the exact merged commit by hash. Build and validate it from a **fresh checkout**
   (`git clone`/`git worktree add` of that commit, never a developer or agent's existing working
   tree) in a **clean build directory and environment** (no reused `build/`, `.venv/`, or
   compile-database symlinks), running the full gate and scenario/physics acceptance there.
3. Record the **exact reproducible commands run**, not just their result: the CMake preset/build
   flags, compiler/toolchain version, Python and pinned dependency versions (`pyproject.toml`'s
   dev group; the CMake project version remains the existing source for Python's own version),
   config/seed/reference-data provenance, test results, and limitations.
4. Respect the source-built plugin/ICD contract: Python is editable-only today; do not invent a
   wheel or binary distribution. If an artifact is introduced, record its SHA-256 checksum and the
   exact commit/flags/toolchain that produced it, so the pairing can be independently reproduced.
5. Before publishing, verify the release tag resolves to that exact commit
   (`git rev-list -n1 <tag>` matches the recorded hash) and that every recorded artifact/checksum
   was actually produced by step 2's fresh build of that commit, not a stale or divergent one. Joe
   authorizes tagging/publishing/deploying and any signing credentials. Record release notes,
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
work at any stage, including a blocked-validation handoff. It brings the docs current (branch scope by
default, repo scope when Joe asks and at every milestone closeout), and it records the first unfinished
gate, actual evidence, Codex findings/retry counts, human decisions and next action; it prepares the
PR description when appropriate and writes STATE once, as main reads it after the merge. It pushes the task branch and opens or updates the
draft PR; it does not move to the next milestone while the current task is unfinished. Append lessons only when something actually
bit. Review process cost when useful from existing evidence; the obsolete retrospective skill is
removed and no retro ceremony blocks a milestone. Add automation only for demonstrated repetitive work.

## Model and effort

`.claude/settings.json` pins `"model": "opus"` at `"effortLevel": "high"`, with subagents on
`"CLAUDE_CODE_SUBAGENT_MODEL": "opus"`, and starts sessions in plan mode (`"permissions": {"defaultMode": "plan"}`); who changes it, and the no-recon-subagents rule, are in AGENTS.md. Rationale: physics correctness comes from independent oracles
and review, not effort; a recon subagent costs tokens (`docs/lessons.md`, 2026-09-14) and hands the planner
a secondhand account instead of a first-hand read. One PR per context; log verbose output. Templates record
the assigned provider/model without changing provider settings. No new agent service or dependency is needed.

## Implementation boundaries

The project skills are instructions invoked by Claude; they do not themselves enforce a merge/release
state machine. Local validation, Codex output, human decisions and actual Git state supply the evidence.
The Stop hook supplies feedback, not approval. Its bounded continuation behavior follows the
[Claude hook contract](https://code.claude.com/docs/en/hooks#stop); project skill discovery follows the
[Claude skill contract](https://code.claude.com/docs/en/skills). No autonomous dispatcher is installed.
