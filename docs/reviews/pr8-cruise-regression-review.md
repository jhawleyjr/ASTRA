# Review: M3 PR 8 — trimmed cruise config, regression run, phugoid cross-check

Spec/plan: `docs/plans/pr8-cruise-regression.md` (Revision 2, approved by Joe 2026-09-22 with five conditions)
Builder: Claude (Opus 5.5)
Reviewer: Codex (companion 1.0.6; fresh context; general + adversarial)
Scope: committed branch diff `origin/main` `553700f` → `18ca1e8` (pre-capture candidate; code at
`b4c5f44`, evidence commit on top); working tree clean at dispatch. Fix verification at `ad04b9e`.
No golden exists by design; capture awaits Joe's arming.
Validation: `./scripts/check-full.sh` at `b4c5f44` and at `ad04b9e`: `check: OK`, exit 0
(`docs/reviews/evidence/{b4c5f44,ad04b9e}-full-gate.log`); four-preset capture dry run
(`docs/reviews/evidence/b4c5f44-capture-dryrun.md`).
Round: initial (general + adversarial); fix 1 (R-001), verified; fix 2 (R-002, raised by the fix-1
verification pass). Correction rounds used: 2/2, so any further finding goes to Joe.

**General review** (native `review --base origin/main`, exit 0): "No actionable defects found in the
pre-capture changes." Reviewer note: its own pytest attempt could not start (read-only sandbox,
no writable temp directory); it ran shell syntax validation and the existing Release binary only.

**Adversarial review** (`adversarial-review --base origin/main` with a six-area focus: trim
literals/provenance, capture-script refusals and arming parity with `guard-oracle.sh`, run
definition, 5×5 construction and measurement, the `test_trim.py` change, frames/units/time/
determinism), exit 0, verdict needs-attention: one finding (R-001). Reviewer's notes: the trim repr
values were not independently recomputed; the halt and byte-mismatch refusals were verified by
inspection only, not by the guard tests. The second note is addressed in fix 1 as coverage (not
a separate finding ID).

## Finding R-001

Severity: minor (Codex: medium)
Status: verified
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
Builder response: fixed in `ad04b9e`. `_git` now passes `env=_env()`, which drops every `GIT_*`
variable and `ASTRA_TOUCH_GOLDENS`, and every other subprocess already used it. The new
`test_inherited_git_variables_cannot_redirect_the_fixture` sets `GIT_DIR`/`GIT_WORK_TREE` to a
decoy repository and asserts that the decoy's HEAD and `status --porcelain` are unchanged after
the tree is built and the script runs. It fails before the fix and passes after. Also added, per
the reviewer's coverage note: `test_refuses_to_write_when_the_run_halts` and
`test_refuses_to_write_on_a_cross_preset_mismatch`. Both run the real script in a
`git clone --shared` of this repository (baseline in history, armed) with a stub `cmake` that
builds nothing and writes a stub harness. Each asserts a nonzero exit, the expected refusal
message, an unchanged HEAD and a clean tree. Mutation check: with the script's `cross_check`
call disabled, the mismatch test fails, and it passes once the call is restored.
Verification: Codex adversarial fix verification, base `553700f` head `ad04b9e`, exit 0: "R-001 fix
holds by inspection at ad04b9e against base 553700f: Git environments are sanitized, clone writes
stay local, and refusal diagnostics prevent vacuous passes." Full gate at `ad04b9e`: `check: OK`.
History: round 1 — open → fixed-unverified (`ad04b9e`) → verified.
Human exception if needed: none.

## Finding R-002

Severity: minor (Codex: medium)
Status: verified
Location: `tests/python/test_capture_cruise_regression_guard.py:189-193` at `ad04b9e`
(`armed_clone` fixture)
Requirement/ADR: plan "Mapping and sequence" (commit B captures and commits the golden; the full
gate follows).
Failure: the fixture asserted that the golden directory is absent at the cloned HEAD. Once commit B
commits the golden, both refusal tests would fail in setup, before exercising either refusal, and
the post-capture full gate could not pass. Raised by the R-001 fix-verification pass as a new
finding in the fix delta.
Requested outcome: support an already-committed golden. Snapshot its contents before invoking the
script and assert they are unchanged afterwards, along with an unchanged HEAD and a clean status.
Builder response: fixed. The absence assertion is removed. A new `_golden_snapshot` records every
file under the golden directory (or `None` when absent), and both refusal tests assert it is
unchanged, together with HEAD and a clean status. The new
`test_refusals_leave_an_already_committed_golden_untouched` commits stand-in golden files in the
clone, then runs both the halt and mismatch refusals and asserts that the files are byte-identical,
HEAD is unchanged and the tree is clean.
Verification: Codex adversarial fix verification, base `553700f` head `ed10734`, exit 0, verdict
approve: "R-002: fix holds; removing the absence assertion permits committed goldens, while
snapshots detect persistent file additions, deletions, and byte changes, HEAD checks detect
commits, and status checks detect staged/uncommitted changes." Reviewer caveat: these are
final-state checks, not proof against a transient write that is later restored. Full gate at
`ed10734`: `check: OK` (`docs/reviews/evidence/ed10734-full-gate.log`).
History: round 2 — open → fixed-unverified (`ed10734`) → verified.
Human exception if needed: none.

## Commits B and C (post-capture) review

Scope: `ed10734` (Joe-approved pre-capture candidate) → `bffc751`: the packet-record commit
`4ce6a9f`, commit B `3d0d3a6` (the armed capture script's golden commit) and commit C `bffc751`
(the golden-comparison test, plus Joe's findings J-1 and J-2, which he ruled are not a correction
round). Full gate at `bffc751`: `check: OK` (`docs/reviews/evidence/bffc751-full-gate.log`).

- **Adversarial review** (focus on B's manifest and header, C's golden test, J-1 and J-2): exit 0,
  verdict approve, "No material findings". It confirmed B touches only the two golden files, that
  the manifest revisions and blob hashes match git, and that the frozen bytes match the dry-run
  hash. C compares every value bitwise and can't pass vacuously. J-2 runs before any build or
  write, and the clone tests would fail without it. Caveat on J-1: "its peak-order claim is
  specific to this response, not arbitrary slow-mode mixtures."
- **General review:** one finding, R-003 below.

## Finding R-003

Severity: minor (Codex: P2)
Status: needs-human
Location: `tests/unit/kernel/notional_transport_cruise_regression_test.cpp:13` at `bffc751`, with
`tests/capture/capture_cruise_regression.sh:107` (`capture_under_all_presets`)
Requirement/ADR: J-2's documented re-capture path: Joe removes the existing golden in a commit he
authorizes, then re-runs the script.
Failure: the regression test now includes the golden header unconditionally, and step 4 runs a
full `cmake --build build/<preset>` before building the harness. After the golden is removed, that
full build compiles the regression test, fails on the missing header, and stops the script before
the harness can regenerate the golden. So the documented re-capture path can't be used as written.
The current candidate is unaffected (the golden exists, the gate is green), and the failure mode
is safe: the script stops and writes nothing.
Requested outcome: keep an authorized re-capture buildable without the golden header.
Builder response: not fixed, because correction rounds are exhausted (2/2) and this finding goes
to Joe. Recommended fix: delete step 4's full-build line (`:107`) and build only
`--target capture_cruise_regression`. That target already pulls in every plugin: under the dynamic
presets through `astra_finalize_plugins()`'s `add_dependencies` (this PR added
`capture_cruise_regression` to that list), and under static-plugins because the plugins link into
`astra_core`, which the harness links. It would be a one-line script change plus a guard-test
check. Alternative: make the test's golden include conditional in CMake, which is more machinery
for a rarely used path.
Verification: pending Joe's decision.
History: B/C review — open → needs-human.
Human exception if needed: pending (Joe).

## Completion

Open findings: R-003 (needs-human, minor, re-capture path only). Pre-capture findings: none open. R-001 and R-002 verified, and the final verification pass at `ed10734`
re-confirmed R-001. New findings from that pass's fix-delta inspection: none ("No material
findings", verdict approve). Correction rounds used: 2/2, so any further finding goes to Joe.

Not reviewed yet: the golden capture (commit B) and the golden-comparing test (commit C). Neither
exists until Joe arms the capture, and both need renewed review and validation. The reviewer's
recommendation is not Joe's approval.
