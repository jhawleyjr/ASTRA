# Workflow calibration and retro outcomes (Tier 2, docs/skills only)

Recovered from `~/.claude/plans/pasted-content-id-5c4e-workflow-calibra-nifty-eclipse.md` on 2026-09-30. Approval: the file records that Joe approved the intent; the work merged as #60. Never committed: neither the file nor git history says why.

## Context

Joe's 2026-09-26 retro found five problems. PLAN.md (953 lines) records facts that only become true at merge,
which forced bookkeeping PRs. Ceremony does not scale down with tier. Rules are restated in six files, and #59's
Codex finding came from a mismatch between them. About 64k lines of evidence logs were committed. Cowork review
became a required hop. This PR applies Joe's decisions A–F and adds one lesson. It leaves the hooks, CI and the
Tier 3 definition unchanged. Joe approved the intent, so there is no plan critique. He waived `just check` because
no hook, script or build file changes.

Joe's answer during planning: the open item "restore the no-recon-subagents rule's visibility" is **closed by this
PR**. AGENTS.md states the rule once, and plan-feature step 1 links to it.

## Setup and Git

- `git fetch origin`, then `git worktree add .claude/worktrees/workflow-calibration -b worktree-workflow-calibration origin/main`. Confirm the base is `b6f2078`.
- Commit coherent steps as they land and push the task branch after each one. Open the draft PR at the first
  commit, with the retro findings in the body (condensed from Joe's list).
- Commit order: (1) PLAN.md compaction, (2) AGENTS.md, CLAUDE.md and workflow.md single-sourcing plus B and F,
  (3) skills, docs/agents, validation.md and templates (C, E), (4) lesson, (5) STATE at `/wrap`.

## A. PLAN.md (`PLAN.md`)

- **Header:** keep the §16 pointer. State once: PLAN.md holds the milestone checklist, the open items and one
  line per PR, in the form `PR n (#NN): scope. Plan: <link>. Packet: <link>.`. It holds nothing that becomes true
  only at merge (merge hashes, merge or approval dates, round counts, review narratives), because git log and the
  PR record those.
- **Milestones M0–M4:** each keeps its `[x]`, its acceptance line verbatim and one evidence pointer:
  - M2: the PR 3c packet and ADR-0016.
  - M3: the PR 6, PR 7 and PR 8 packets, keeping the existing one-line trim, modes and regression summary.
  - M4: `docs/reviews/m4-pr6-lockstep-acceptance-human.md` plus the `docs/reviews/evidence/4e418e1-*` logs.
  - M0 and M1 have no retained packet, so the pointer names their PRs.
- **PR lines:** plain bullets with no checkbox. PR numbers come from `git log` (#1…#58). Merged non-PR items
  get one line each under their milestone: the case05 fix (#33) and ADR-0015 (#32). Items resolved by a PR
  (the atmosphere misattribution window, the ICD deflection sense, the fresh-checkout tidy failure) fold into
  the scope of the resolving PR's line. "Plan: none" or "Packet: none" is written explicitly where no file
  exists. Link paths already exist under `docs/plans/` and `docs/reviews/`.
- **Open items:** one list of `- [ ]` lines (the session-start hook greps `- [ ]`, so the format stays). Each
  item is carried verbatim or tightened with no fact lost, and tagged with its origin (milestone/PR) and owner
  where one is stated. There are 16 items. The recon-subagent item closes in this PR per Joe's answer. M5 stays
  as the milestone `[ ]` line.
- **Decisions found only in PLAN.md:** a short "Recorded only here" list, one line each, citing the code comment
  where one exists. For each candidate, first grep `docs/plans`, `docs/reviews`, `docs/decisions`, `docs/specs`
  and `docs/architecture.md` for it:
  - M2 PR 2: `Simulation::for_testing` stays a public static, and that was accepted explicitly.
  - M2 PR 2: `assumed_position_representation()` is declared but not enforced.
  - M2 PR 2c: the lookahead is first-order and has a ~5 µm near-zero-velocity band where the halt comes one frame
    late, as `kNonFiniteState`.
  - M4: Joe rendered and checked the trace SVGs during his PR 6 review, which supersedes the packet's "could not
    render" note.

  Items already confirmed as recorded elsewhere, with no line needed:
  - the PR 3b "Settled" items (spec and plan `nesc-harness.md`, ADR-0014);
  - the geoid gap (ADR-0012, spec);
  - Cmadot (PR 4 packet);
  - the cosmetic SVG label and R2c risk (PR 6 packet);
  - `for_testing` not bound (PR 5 plan);
  - O5 (PR 8 plan).
- **Hard checks, before committing (scratch scripts):**
  1. Extract every `- [ ]` line from `git show origin/main:PLAN.md`, all 17 including M5. Match each to its new
     line, or to "closed by this PR" for the recon item. The mapping (old item → new line) goes in the PR body.
  2. Every `docs/…` path linked or cited in the new PLAN.md exists.

## B, D and F. Rules stated once (`AGENTS.md`, `CLAUDE.md`, `docs/process/workflow.md`)

- **AGENTS.md** gains or absorbs the following, compressing existing lines to stay under 100 lines with CLAUDE.md
  (now 80). Each rule appears once, and the other files link to it.
  - The PLAN.md and STATE rule: STATE is written once at `/wrap` and says nothing that changes when the PR merges.
  - No recon or exploration subagents (cost and fidelity, lessons 2026-09-14). Codex review and critique are the
    only subagent route.
  - Model and effort settings in `.claude/settings.json` are Joe's to change (moved from CLAUDE.md).
  - Results go in the PR body: command, exit code, summary counts, the Codex verdict and thread id. They never go
    in record-only commits.
  - Evidence: commit a trimmed gate log. Full logs stay out of git unless a run fails or Joe asks.
  - A gate passes only on exit 0 plus `check: OK`.
  - The correction budget: at most two correction rounds and two failed integrations. A new context does not
    reset it.
  - Approval is only Joe's explicit decision or his squash-merge.
  - Self-review, or an unavailable Codex, means review is pending.
  - A pointer to the tier table for the minimum process per tier.
- **CLAUDE.md:** drop the model-settings line and the `/wrap` push description, and keep the pointers.
- **workflow.md:** keeps procedure and rationale only.
  - Replace restated rules with links to AGENTS.md: the Sources-of-truth PLAN/STATE text, the Git standing
    authorization (keep the worktree commands and the `gh pr ready --undo`/`gh pr ready` rerun), the Verification
    oracle rule (keep the guard mechanics), the Review role split, the review-fix caps, the Coordination `/wrap`
    description, and Model and effort (keep the rationale).
  - Tier table (B): the Tier 0 and Tier 1 minimum process gets "one Codex pass on the final diff (Joe may waive)".
    Add: "On Tier 0 and Tier 1, a finding that is not a factual error or defect becomes a follow-up, not a
    correction round." Tier 2 and 3 rows and the Tier 3 examples are unchanged.
  - Cowork line (F): "Cowork review applies to Tier 2 and Tier 3 work, or when Joe asks. On Tier 0 and Tier 1 its
    findings default to follow-ups unless something is factually wrong."
  - Step 7's exit condition "evidence recorded" becomes "results in the PR body".
- **docs/agents/claude-code.md, README.md, build.md and review.md:** replace restated rules with links: the
  standing authorization, the model settings, no recon, "stop after two fix rounds" and the Joe-owns list. Keep
  the Claude-specific procedure (the `no-main.sh` behavior, LSP, `run_in_background` gate handling).
- **Disagreements, where AGENTS.md wins:** record each one found while editing in the PR body. Known so far:
  - workflow.md lets Tier 0 omit the gate, while AGENTS.md says "definition of done". Resolution: the exception
    lives in the tier table as the scoped Tier 0 rule, and AGENTS.md links to it. This is disclosed as a
    reconciliation.
  - the wrap skill's "merged once origin/main contains it" wording, which C replaces.
  - validation.md's "retain/attach logs", which E replaces.

## C and E. Results in the PR body, trimmed evidence

- **Skills:**
  - `wrap`: packet step: results in the PR body, linking the trimmed log. PLAN step: per the AGENTS.md rule, one
    line per PR and no merge-time facts. STATE step: written once at `/wrap`, nothing that changes at merge,
    replacing the merge-pending wording. The Git section links the standing authorization.
  - `human-review`: the check and Codex results (command, exit, counts, verdict, thread id) go in the PR body.
    The push bullet becomes a link.
  - `review-cycle`: step 3's raw output stays outside git, and the verdict, thread id and findings go in the PR
    body. Add the Tier 0 and Tier 1 path: one pass on the final diff, with non-defect findings as follow-ups.
    Step 5 links the budget.
  - `plan-feature`: step 1 says "Inspect … directly (AGENTS.md)".
- **`docs/agents/review-dispatch.md`:** "Preserve raw output…" becomes: record scope, model, job or thread id and
  the verdict in the PR body, and keep raw output out of git. Fix verification: a findings file may be updated
  only in the commit carrying the fix batch it describes, and the verdicts go in the PR body.
- **`docs/process/validation.md` Evidence:** replace the retain/attach paragraph with the trimmed-log procedure.
  The header lines are date, command, base and head, and exit. Then grep the full log for `Test project`,
  `tests passed`, `: OK`, `hooks:`, `All checks passed`, the pyright summary, the `N passed` lines and
  `check: OK`, plus any failure output in full. Commit the result as `docs/reviews/evidence/<head>-gate-summary.log`; `<head>-full-gate.log` names only a full log committed after a failed run.
  On a failed run, commit the full log. Existing evidence files are left alone.
- **Templates:**
  - `human-review.md`: the Evidence column becomes "trimmed log / candidate", and results go in the PR body.
  - `task-status.md`: its "results live in linked evidence" wording becomes a PR-body pointer.
  - `README.md`: check for any "retain the full log" wording.

## Lesson (`docs/lessons.md`, appended)

One entry, dated 2026-09-26: facts that only become true at merge (merge hashes, "approved", "merged as") do not
belong in PLAN.md or STATE, because git and the PR record them. Evidence: the bookkeeping PRs #13, #14 and #49.

## STATE (at `/wrap`, once)

STATE is written before the Codex pass, so that pass covers the final diff. It names the branch and PR and says
the review results are in the PR body, so it stays true after both the Codex pass and the merge. It does not
state the Codex outcome.

## Verification

1. `python3 scripts/docs_check.py && git diff --check`, with the exit codes recorded.
2. The open-item mapping script and the PLAN.md link-existence script (scratchpad), with the output summarized in
   the PR body.
3. Line budget: `wc -l AGENTS.md CLAUDE.md` is under 100 (docs_check also enforces this).
4. The PR body states that `just check` was waived by Joe (no hook, script or build change), as for #59.
5. After `/wrap`: one Codex pass, `review --base origin/main --wait`, redirected to a scratch log with its exit
   captured. The focus is no open item or unique decision lost from PLAN.md, each rule stated once with no
   contradictions, and links resolving. Native `review` takes no focus text, so this is an `adversarial-review`
   with that focus. The verdict, thread id and findings go in the PR body, not a commit.
6. If Codex finds a defect: fix it once, push, and note it in the body. Anything beyond that is reported and the
   work stops.
