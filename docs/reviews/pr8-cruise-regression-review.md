# Review: M3 PR 8 — trimmed cruise config, regression run, phugoid cross-check

Spec/plan: `docs/plans/pr8-cruise-regression.md` (Revision 2, approved by Joe 2026-09-22 with five conditions)
Builder: Claude (Opus 5.5)
Reviewer: Codex (companion 1.0.6; fresh context; general + adversarial)
Scope: committed branch diff `origin/main` `553700f` → `18ca1e8` (pre-capture candidate; code at
`b4c5f44`, evidence commit on top); working tree clean at dispatch. No golden exists by design;
capture awaits Joe's arming.
Validation: `./scripts/check-full.sh` at `b4c5f44`: `check: OK`, exit 0
(`docs/reviews/evidence/b4c5f44-full-gate.log`); four-preset capture dry run
(`docs/reviews/evidence/b4c5f44-capture-dryrun.md`).
Round: initial (general + adversarial), then fix 1 (R-001). Correction rounds used: 1/2.

**General review** (native `review --base origin/main`, exit 0): "No actionable defects found in the
pre-capture changes." Reviewer note: its own pytest attempt could not start (read-only sandbox,
no writable temp directory); it ran shell syntax validation and the existing Release binary only.

**Adversarial review** (`adversarial-review --base origin/main` with the six-area focus: trim
literals/provenance, capture-script refusals and arming parity with `guard-oracle.sh`, run
definition, 5×5 construction and measurement, the `test_trim.py` change, frames/units/time/
determinism), exit 0, verdict needs-attention: one finding (R-001). Reviewer's notes: the trim repr
values were not independently recomputed; the halt and byte-mismatch refusals were verified by
inspection only, not by the guard tests. The second note is addressed in fix 1 as coverage (not
a separate finding ID).

## Finding R-001

Severity: minor (Codex: medium)
Status: fixed-unverified
Location: `tests/python/test_capture_cruise_regression_guard.py:23-26` at `18ca1e8` (`_git`)
Requirement/ADR: plan R5 (the guard test must run only in its throwaway tree); AGENTS.md
(preserve unrelated work).
Failure: `_git` passed no `env`, so it inherited the pytest process's environment. With
`GIT_DIR`/`GIT_WORK_TREE` inherited (for example from a git hook), `git -C` does not override
them, so the fixture's `init`/`add -A`/`commit` would act on that other repository. That could
commit a real checkout's changes before any capture guard runs. Reproduced in fix 1: the new
isolation test, run against the unfixed `_git`, moved the decoy repository's HEAD
(`4868892…` → `3fc903f…`).
Requested outcome: sanitize the environment for every fixture git command, and add an isolation
test with inherited git variables aimed at a disposable repository.
Builder response: fixed. `_git` now passes `env=_env()`, which drops every `GIT_*` variable and
`ASTRA_TOUCH_GOLDENS`, and every other subprocess already used it. The new
`test_inherited_git_variables_cannot_redirect_the_fixture` sets `GIT_DIR`/`GIT_WORK_TREE` to a
decoy repository and asserts that the decoy's HEAD and `status --porcelain` are unchanged after
the tree is built and the script runs. It fails before the fix and passes after. Also added, per
the reviewer's coverage note: `test_refuses_to_write_when_the_run_halts` and
`test_refuses_to_write_on_a_cross_preset_mismatch`. Both run the real script in a
`git clone --shared` of this repository (baseline in history, armed) with a stub `cmake` that
builds nothing and writes a stub harness. Each asserts a nonzero exit, the expected refusal
message, no `tests/golden/notional_transport/`, an unchanged HEAD and a clean tree. Mutation check:
with the script's `cross_check` call disabled, the mismatch test fails, and it passes once the
call is restored.
Verification: pending Codex fix verification.
History: round 1 — open → fixed-unverified.
Human exception if needed: none.

## Completion

Pending Codex fix verification of R-001 and the fix delta.
