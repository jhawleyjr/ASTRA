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
cycle: `EXITSTATUS=0`, `check: OK`. Committed as `b2ef1ac`.

**Fresh Codex review of `b2ef1ac`** (general: no actionable regressions; adversarial
fix-verification, focus naming R-001/R-002/R-003): **R-001 verified** ("resolved by explicit
per-finding dispatch"). **R-002 verified** ("resolved by retained validation tooling,
local/CI wiring, and corrected STATE evidence"). **R-003 verified** ("resolved by separate
bash syntax-check invocations"). No dangling references from the 3 README deletions or
`tokens.py`'s removal. Verdict: **needs-attention** — one new finding, not carried over from
any prior round:

## Finding R-006

Severity: major (Codex: medium — "Preserve tolerance protection for writes after changing
directories")
Status: fixed-unverified
Location (pre-fix): `.claude/hooks/guard-oracle.sh:50-51` (the Bash write-target matcher `g`).
Requirement/ADR: the oracle-guard boundary itself (`workflow.md` §Verification).
Failure: by static inspection, `cd tests/physics && sed -i 's/1e-6/1e-3/' tolerances.yaml`
now passes the guard — the write target (`tolerances.yaml`, bare) lacks the literal `tests/`
prefix D5's new pattern requires, and the regex cannot connect it back across the `cd`.
**Verified directly, not taken on Codex's word alone**: reproduced with the hook run against
assembled JSON (`cd tests/${G} && echo x > cruise.csv` — `${G}` = `golden`, exit 0, should be
exit 2) and confirmed this is a **pre-existing gap in the whole Bash-command matcher**, not
D5-specific: the identical `cd tests/golden && ...` and `cd docs/decisions && ...` bypasses
also succeed against the *unmodified* `tests/golden/` and `docs/decisions/` alternatives,
which predate this entire workflow-v3 branch. D5 did not introduce this class of gap; it
surfaced it by being the finding under test.
Requested outcome (Codex): resolve write targets against the effective command directory, or
conservatively block ambiguous relative writes to any bare/relative path containing a guarded
name; add `cd`-then-write regression cases for all three guarded categories, preserving the
D5 PASS cases.
Builder response: initially deferred, out of scope for the Round 3 batch (R-004/R-005) per
Joe's explicit scope-confinement instruction at the time. A later, separately authorized
request ("this request authorizes these bounded fixes, including the previously deferred
guard issue") implemented it, committed as `1a12623`. `.claude/hooks/guard-oracle.sh`'s Bash
branch (currently lines 46-235) now resolves every write-shaped statement's target against the
command's actual working directory — the hook's own `cwd` input field plus any literal `cd`
inside the command, tracked statement by statement, quote-aware — before checking the same
guarded-path pattern the direct-write check already used; this closes the bypass for all three
guarded categories (`tests/golden/**`, `docs/decisions/**`, `tests/**/*tolerance*`), including
the exact shape Codex demonstrated (`cd tests/physics/nesc && sed -i ... tolerances.yaml`).
`guard-oracle.sh:41`'s Edit/Write branch also normalizes `file_path` (`os.path.normpath`) so a
`..` traversal cannot dodge the same substring check. 21 new lines in
`.claude/hooks/tests/guard-oracle.cases` cover: inline `cd` + write and chained/`..`-relative
`cd` into each guarded category; the session's Bash `cwd` already persisted inside a guarded
directory with no `cd` in the command at all; destination-only commands (`cp`/`touch`); the
same shapes under an unarmed worktree; `cd` back out or an absolute target elsewhere still
passing; reads from inside a guarded directory; and a `..`-traversal Edit/Write path (both
directions). Documented, narrow remaining gap, stated in the hook's own comments: a `cd` to a
variable, glob, `~`, or `-` (previous directory) is not followed — the write is then checked
against the last known directory instead of the real one, a stated limitation, not a silent one.
Validation (local, this session): `just hooks-test` — 77/77 passed, re-run fresh during this
reconciliation, not just cited from the earlier commit message (was 56/56 before this fix). The
full local gate was also run to completion after this change (`EXITSTATUS=0`, `check: OK`,
all four presets 274/274) — see `1a12623`'s commit message; not re-run here since no code
changed since that commit.
Verification: pending Codex dispatch. No adversarial or general review has been requested for
this fix yet; nothing here should be read as Codex having verified it.
History: found by Codex's adversarial fix-verification pass on `b2ef1ac` → confirmed
independently by the builder → deferred, not fixed, per explicit scope confinement (Round 3) →
implemented and locally validated, committed as `1a12623` (a later, separately authorized
batch) → this entry corrected to match that implementation after an audit found `STATE.md`
and this record had drifted out of sync → independent verification still pending.
Human exception if needed: none requested; not applicable. Joe explicitly authorized
implementing this previously-deferred fix (see Builder response); that authorization covers
the fix itself, not a substitute for the independent verification still pending.

## Round 3: two Joe-reported findings (CI docs-scope gap, same-page anchor validation)

Scope: exactly these two findings and their tests/evidence, per Joe's explicit instruction;
R-006 above is deliberately not touched in this batch.

## Finding R-004

Severity: minor
Status: fixed-unverified
Location: `.github/workflows/check.yml`'s `Scope` step (the `docs` classifier regex).
Reported by: Joe.
Failure: the `docs` classifier missed `docs/README.md`, `docs/reviews/README.md`,
`scripts/docs_check.py`, `justfile`, and `.github/workflows/check.yml` itself — a change to
any of these (including one that broke `docs-check`'s own CI wiring) would run only the
unconditional `bash -n` step, not `docs-check`.
Requested outcome: cover those five paths without enabling `GATE=full` or otherwise touching
the local-full-gate policy.
Builder response: extended the `docs` regex to
`^(docs/process/|docs/agents/|docs/templates/|\.claude/skills/)|^(AGENTS|CLAUDE|README)\.md$|^docs/(README|reviews/README)\.md$|^(scripts/docs_check\.py|justfile|\.github/workflows/check\.yml)$`.
Verified with a bash simulation of the classifier against all 5 target paths (all now `true`)
and 4 control paths that must stay `false` (`docs/reviews/workflow-v3-review.md`,
`docs/architecture.md`, `scripts/check-full.sh`, `scripts/setup-ubuntu.sh`) plus 3 previously-
covered paths that must stay `true` (`docs/process/workflow.md`, a skill file, `AGENTS.md`) —
all 12 correct. YAML re-parsed clean. `GATE=local` untouched.
Verification: pending Codex dispatch — not sent this batch, per Joe's explicit instruction
("stop after validation and summarize the diff for Codex verification").
History: reported by Joe → fixed and locally verified (this batch).
Human exception if needed: none requested; not applicable.

## Finding R-005

Severity: minor
Status: fixed-unverified
Location: `scripts/docs_check.py`'s `check_links` — the `if not path_part: continue` branch.
Reported by: Joe.
Failure: a pure same-page fragment link (`[Section](#does-not-exist)`) was silently skipped
rather than checked against the current file's own headings, so a broken same-page anchor
passed validation.
Requested outcome: validate same-page fragments against the current file; keep cross-file
anchor checking working; retain regression tests for both directions.
Builder response: the pure-fragment branch now calls `anchors_of(f)` (the current file, not
the link target) and reports `broken same-page anchor '#{frag}'` when missing. Added
`tests/python/test_docs_check.py` (new, 6 tests): same-page anchor exists/passes, same-page
anchor missing/fails, cross-file anchor exists/passes, cross-file anchor missing/fails, a
pre-existing broken-link-target case (regression guard), and a bare `[text](#)` no-op link
(must not false-positive). All 6 pass; `just docs-check` on the real repository still reports
OK (24 files, unchanged) — the fix introduced no new finding against real content.
Verification: pending Codex dispatch — not sent this batch, per Joe's explicit instruction
(same "stop after validation" instruction recorded under R-004).
History: reported by Joe → fixed and locally verified with new regression coverage (this batch).
Human exception if needed: none requested; not applicable.

## Completion (through Round 3+)

R-001, R-002, R-003: **verified** by fresh Codex review of `b2ef1ac`. R-004, R-005, R-006: all
**fixed-unverified** — R-004/R-005 implemented and locally validated in the Round 3 batch;
R-006 implemented and locally validated in a subsequent, separately authorized batch, committed
as `1a12623` (see R-006's own entry for what changed and its evidence). Codex verification is
intentionally not yet dispatched for any of the three, per Joe's explicit "stop after
validation" instructions each time — this file is that summary, corrected here after an audit
found `STATE.md` already reflected R-006's fix while this record still called it open.

**Correction rounds: 2/2 — budget exhausted.** Round 1 (D1-D4/D6-D8, `b7978e3`) verified clean.
Round 2 (R-002/R-003 completion + D5 + cleanup + `tokens.py` retirement, `b2ef1ac`) verified
R-001-003 clean but surfaced R-006. The Round-3 batch (R-004, R-005) and R-006's later fix are
each **explicitly authorized by Joe beyond the exhausted autonomous budget** ("this request
authorizes this specific fix batch if the autonomous budget has been exhausted" for R-004/R-005;
"this request authorizes these bounded fixes, including the previously deferred guard issue"
for R-006) — neither consumes a third autonomous round on its own initiative. No findings
rejected. Zero critical findings at any point. R-004/R-005's full local gate result is recorded
in `STATE.md`; R-006's is in `1a12623`'s commit message, with `hooks-test` (only, no code
changed since) reconfirmed fresh during this reconciliation — neither duplicated here.
