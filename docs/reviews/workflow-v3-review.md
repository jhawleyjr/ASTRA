# Review: workflow-v3 (12-step workflow, D1-D4/D6-D8 fix round)

Spec/plan: this session's workflow audit (Claude plan file, not a repository artifact); see
`STATE.md` for the current handoff. No feature spec — this is a process/tooling change.
Builder: Claude (opusplan: Opus 5 in plan mode, Sonnet 5 for this implementation round).
Reviewer: Codex (`codex-cli` 0.154.0, model `gpt-6-astra`, reasoning effort `low`; fresh
context via the installed companion; general + adversarial).
Scope: base `origin/main` = `main` = `40b484b`; branch head `22a5009`; plus this round's
uncommitted working-tree changes: `.claude/skills/human-review/SKILL.md`,
`.claude/skills/wrap/SKILL.md`, `.github/workflows/check.yml`,
`docs/agents/review-dispatch.md`, `docs/lessons.md`, `docs/process/validation.md`,
`docs/process/workflow.md`, `justfile`, and new `scripts/docs_check.py` (this file itself is
also new, added by this fix round).
Validation: `just hooks-test` 51/51 passed (this round); `just docs-check` OK, 27 files (this
round, new recipe); `docs_check.py` proven to fail on 6 classes of deliberately broken input
in a scratchpad fixture, then pass once fixed; CI YAML re-parsed and its 3 `run:` blocks
`bash -n`-clean; full local gate (`./scripts/check-full.sh`) run separately this round —
result recorded in STATE.md and the human-review packet, not duplicated here.
Round: fix 1 (of the workflow's two-round review-fix budget).

## Prior findings from the initial adversarial pass (job `review-mu6c7gfe-zb8eac`)

Codex's own severity label was "medium"; this template has no such tier, so both are mapped
to **major** (requirement failure / missing required check — the template's definition, not
a claim that either is safety-critical).

## Finding R-001

Severity: major (Codex: medium)
Status: fixed-unverified
Location: `docs/agents/review-dispatch.md:19-23` (pre-fix) — fresh dispatch commands pass
only a diff target.
Requirement/ADR: `docs/process/workflow.md` §Review-fix limits (reviewer verification and
reviewer agreement on rejections); `.claude/skills/review-cycle/SKILL.md` step 5.
Failure: a fresh `review`/`adversarial-review` pass cannot see prior finding IDs, builder
responses, or which rejections were agreed, since native `review` takes no focus text and the
documented `adversarial-review` examples never reference a findings record. Treating a clean
fresh pass as proof that earlier findings were verified is an unsupported inference.
Requested outcome: define an explicit fix-verification dispatch that supplies previous
finding IDs, builder responses, candidate revisions and validation evidence through a
supported read-only focused request; require per-finding verification rather than
interpreting silence as agreement.
Builder response: added a "Fix verification" section to `docs/agents/review-dispatch.md`
(this round's diff) defining the recipe: record findings/dispositions in
`docs/reviews/<feature>-review.md` (this file is the first real instance of that pattern),
then dispatch `adversarial-review` with a compact, safely-quoted focus naming that file's
path, the base/head revisions, and the specific finding IDs to verify, requiring an explicit
per-finding verdict.
Verification: pending this round's fresh adversarial pass — asked to verify R-001 and R-002
by name against this file and the current diff.
History: initial (Codex, medium) → fix 1 (this round).
Human exception if needed: none requested; not applicable.

## Finding R-002

Severity: major (Codex: medium)
Status: fixed-unverified
Location: `docs/process/validation.md:61-64` (pre-fix) — prescribed Markdown-link, command-
name and line-budget checks had no executable form; `.claude/skills/**` was validated by
neither the local gate nor CI.
Requirement/ADR: `docs/process/validation.md`'s own "for workflow edits" checklist;
`docs/process/workflow.md` §Verification.
Failure: `justfile`'s `check` recipe had no such step; CI's `Scope` classifier treated
`.claude/skills/**` as neither `code` nor `hooks`, so a skills-only PR ran only `bash -n` on
two scripts. STATE.md additionally claimed "All four active skill validators … passed" with
no such validator retained or identifiable anywhere in the repository — an untraceable claim.
(Note on that STATE.md wording, corrected per Joe: those earlier checks were external/ad hoc
and did actually run in that session; the defect is the absence of *reproducible repository
tooling*, not a fabricated result. This finding's fix gives that ad hoc work a durable,
rerunnable home.)
Requested outcome: retain a lightweight workflow-check command covering all four skills'
metadata, local references, and instruction budgets; document its invocation and include it
in local validation; replace the untraceable validator claim with command-specific evidence.
Builder response: added `scripts/docs_check.py` (stdlib-only; Markdown link/anchor
resolution, `.claude/skills/*/SKILL.md` frontmatter + name-matches-directory, the
AGENTS.md+CLAUDE.md line budget, `just`/`./scripts/` command-name references) and a `just
docs-check` recipe wired into `check`; extended CI's `Scope` step with an independent `docs`
classification (`docs/process/`, `docs/agents/`, `docs/templates/`, `.claude/skills/`, and the
root instruction files) and a `docs_gate` step that runs `just docs-check` whenever that scope
is touched — not merely folded into the existing `hooks` classifier, which triggers a
different check. Reworded `docs/process/validation.md` to name the command. STATE.md's wording
is corrected in this round's handoff to cite the actual command and this round's real result
rather than the earlier untraceable sentence.
Verification: pending this round's fresh adversarial pass.
History: initial (Codex, medium) → fix 1 (this round).
Human exception if needed: none requested; not applicable.

## Additional changes in this round (not reviewer findings; self-identified, for context)

These were raised by Claude's own audit of `22a5009`, not by Codex, and are included here only
so the fresh review has full context for the diff it is reading. They do not require a
per-finding verdict, but the reviewer should say if any looks wrong.

- `docs/process/workflow.md`: added a "Stage transitions" paragraph after the twelve-step
  table naming where implementation and integration happen, without renumbering or deleting
  any of the twelve rows (`workflow.md#the-twelve-steps`).
- `docs/process/workflow.md`, `.claude/skills/wrap/SKILL.md`, `STATE.md`: gave the two-round
  review-fix/integration budget a durable, task-scoped home (`Correction rounds: n/2; failed
  integrations: n/2` in STATE.md's `Tried / failed` section), named in `/wrap`'s STATE.md step.
- `docs/process/workflow.md:84-85`: corrected wording — SessionStart deletes only the file
  marker (in both accepted locations); it cannot unset a launched session's
  `ASTRA_TOUCH_GOLDENS` environment variable.
- `.github/workflows/check.yml`: guarded the `code` scope flag against an empty diff
  (`printf` on an empty `$changed` previously produced a false `code=true`).
- `.claude/skills/human-review/SKILL.md`: added an explicit idempotency check — look for an
  existing packet for the same candidate before writing a second one.

Not in scope for this round, per Joe: D5 (the oracle guard's unanchored `tolerance` pattern)
is presented as a separate inventory-plus-proposal, not implemented — narrowing that guard
needs Joe's explicit authorization. Optional cleanup (placeholder READMEs, stale `/retro`
references, `scripts/tokens.py`'s disposition) is deliberately deferred.

## Completion

Two findings open from the initial pass, both `fixed-unverified` pending this round's fresh
adversarial pass. No findings rejected or deferred. Zero critical findings at any point. This
round consumes fix-round 1 of 2; a second round remains available if the fresh pass reports
either finding unresolved or raises a new one. The full local gate's result and any residual
scope gaps are recorded in STATE.md and the human-review packet, not duplicated here.
