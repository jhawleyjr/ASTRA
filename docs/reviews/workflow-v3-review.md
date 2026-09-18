# Review: workflow-v3 (12-step workflow, D1-D4/D6-D8 fix round)

Spec/plan: this session's workflow audit (Claude plan file, not a repository artifact); see
`STATE.md` for the current handoff. No feature spec — this is a process/tooling change.
Builder: Claude (opusplan: Opus 5 in plan mode, Sonnet 5 for this implementation round).
Reviewer: Codex (`codex-cli` 0.154.0, model `gpt-6-astra`, reasoning effort `low`; fresh
context via the installed companion; general + adversarial).
Scope: base `origin/main` = `main` = `40b484b`; branch head `22a5009` originally, now
committed as `b7978e3` on `workflow-v3` (local commit only; not pushed, no PR). Files touched
by that commit: `.claude/skills/human-review/SKILL.md`, `.claude/skills/wrap/SKILL.md`,
`.github/workflows/check.yml`, `docs/agents/review-dispatch.md`, `docs/lessons.md`,
`docs/process/validation.md`, `docs/process/workflow.md`, `justfile`, new
`scripts/docs_check.py`, new `docs/reviews/workflow-v3-review.md` (this file). A second,
uncommitted round-1b fix (the `bash -n` correction below, plus `STATE.md`) is layered on top
at review-record time; commit it before the final verification pass.
Validation: `just hooks-test` 51/51 passed; `just docs-check` OK, 27 files;
`docs_check.py` proven to fail on 6 classes of deliberately broken input in a scratchpad
fixture, then pass once fixed; full local gate (`./scripts/check-full.sh`) run twice this
round (first failed at `ruff`, fixed; second: `EXITSTATUS=0`, `check: OK`, all four presets
274/274) — full detail in `STATE.md`, not duplicated here.
Round: fix 1 (of the workflow's two-round review-fix budget); this file's second pass records
that round's verification result and one additional fix (R-003) found during it.

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
Verification: **verified**. Fresh Codex adversarial fix-verification pass (base `origin/main`
head `b7978e3`): "R-001 resolved by explicit, focused per-finding verification."
History: initial (Codex, medium) → fix 1 (this round) → verified (fresh Codex pass, same round).
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
different check. Reworded `docs/process/validation.md` to name the command.
Verification: **fixed-unverified, corrected after an inaccurate disposition**. The fresh
Codex adversarial pass found this finding only partially resolved: at that review's time,
`STATE.md` still carried the pre-fix "All four active skill validators … passed" sentence
and a stale full-gate status, even though this file's builder response already claimed
"STATE.md's wording is corrected" — that claim was written before the edit was actually
made, which was inaccurate and is corrected here. `STATE.md` has now actually been rewritten
with the real command name and this session's real full-gate result. Pending one more fresh
Codex pass to confirm.
History: initial (Codex, medium) → fix 1 (this round) → partially resolved, STATE.md gap
found by Codex → STATE.md corrected (this pass) → re-verification pending.
Human exception if needed: none requested; not applicable.

## Finding R-003

Severity: minor (Codex: P2)
Status: fixed-unverified
Location: `justfile:90` (pre-fix, in this round's own `docs-check` recipe) and
`.github/workflows/check.yml:50` (pre-existing, unmodified by `22a5009` or this round until now).
Requirement/ADR: `docs/process/validation.md`'s "for workflow edits" checklist (shell syntax).
Failure: `bash -n scripts/check-fast.sh scripts/check-full.sh` parses only the first operand;
the second filename becomes an unused positional argument to the first script, never a second
file checked. `check-full.sh` — the definition of done — was never syntax-checked by either
the new `just docs-check` recipe or CI's existing "Validation wrapper syntax" step. Confirmed
directly (not taken on Codex's word alone): reproduced with a two-file `bash -n` call where
the second file has a deliberate syntax error, which exits 0, versus checking that same file
alone, which correctly exits 2.
Requested outcome: invoke `bash -n` once per wrapper.
Builder response: split into two separate `bash -n` invocations in both `justfile`'s
`docs-check` recipe and CI's "Validation wrapper syntax" step; reworded
`docs/process/validation.md` to state the one-invocation-per-file rule explicitly, with the
mechanism, so it cannot regress the same way silently again. Re-proved against a scratch
fixture (a broken `check-full.sh` stand-in) that the fixed form now catches it.
Verification: pending the next fresh Codex pass.
History: found by Codex's general review of the complete candidate (this round, same pass as
R-001/R-002's fix-verification) → fixed (this pass).
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

## Round 2: R-002/R-003 completion, D5, and approved cleanup

Base `origin/main` `40b484b`; still-uncommitted work on top of `b7978e3` at review-record
time (committing before this round's Codex dispatch). Scope beyond the R-001/R-002/R-003
material above:

- **D5, now implemented with Joe's explicit authorization** (previously proposal-only).
  Inventoried protected assets first: `tests/golden/**` (5 real files),
  `docs/decisions/0000`-`0013` (14 ADRs), and confirmed no file anywhere in the tree has
  "tolerance" in its *name* today — the guard's third alternative existed for a future file
  like `tests/physics/nesc/tolerances.yaml`. `guard-oracle.sh`'s `guarded` and `g` patterns
  changed from a bare unanchored `tolerance` to `tests/[A-Za-z0-9_./-]*tolerance`, so a real
  tolerance file anywhere under `tests/` stays blocked while a prose plan doc, a source
  header, or a log redirect merely containing the word does not. `guard-oracle.cases` gained
  5 new lines: 3 previously-false-positive PASS cases and 2 new BLOCK cases confirming a
  `tests/physics/nesc/tolerances.yaml`-shaped file stays protected. `just hooks-test`: 56/56
  (up from 51).
- **Two Step 2/3 template rounds**, done between the D1-D4/D6-D8 fix and this one, cross-
  checked against an external reference document Joe supplied (not part of this plan; not
  committed to this repo): `docs/templates/feature-spec.md` gained `Assumptions:` and
  `System context:` prompts; `docs/templates/implementation-plan.md` gained an
  evidence-vs-inference and spec-vs-repository-mismatch prompt in "Repository findings";
  `plan-feature/SKILL.md` steps 1 and 3, and `docs/agents/README.md`'s planner bullet, now
  state the planning boundary (spec/plan/brief only, never product code/tests/build config;
  a plan does not itself authorize implementation) and the mismatch-escalation rule
  explicitly.
- **Approved optional cleanup:** deleted the 3 still-empty placeholder READMEs
  (`docs/specs/`, `docs/plans/`, `docs/tasks/`) and de-linked their now-nonexistent-when-
  empty directories from `docs/README.md`'s table (a markdown-link directory reference would
  have silently broken on a fresh clone, since git does not track empty directories — kept
  `docs/reviews/README.md`, which now documents an actively-used directory). Fixed
  `docs/architecture.md:53`'s stale `skills (/task /wrap /retro)` to the four current skills.
- **`scripts/tokens.py` retired, together with its test.** First attempt deleted only the
  script and left `tests/python/test_tokens_script.py` orphaned, which the full gate's
  `pytest` stage correctly caught (`FileNotFoundError`) — restored both, asked Joe given the
  test suite was real and passing, and per his explicit answer (it measured the one-time
  fable→opus/sonnet migration cost, no longer needed) removed both files together. Fixed one
  stale docstring reference to it left in `scripts/docs_check.py`.

Validation this round: `just hooks-test` 56/56; `just docs-check` OK (24 files, down from 27
after the README deletions); full local gate rerun after the tokens.py restore/re-delete
cycle — result recorded in `STATE.md`, not duplicated here. R-002 and R-003 (from the
previous round) are included in the commit this round reviews; this is their actual
fresh-review verification, not yet dispatched at review-record time.

## Completion

R-001: verified. R-002: fix completed this pass (STATE.md rewritten), re-verification pending.
R-003 (found during this round's own fresh review, not carried over): fixed, re-verification
pending. No findings rejected or deferred. Zero critical findings at any point. This consumes
fix-round 1 of 2; one more fresh Codex pass is planned to confirm R-002 and R-003 before this
round closes. If that pass reports either unresolved, the workflow's two-round budget allows
one further fix round before escalating to Joe. The full local gate's result is recorded in
STATE.md, not duplicated here.
