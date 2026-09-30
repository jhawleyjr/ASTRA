# Review: M6 PR 2, ADR-0027 implementation (run-log finalization)

Spec/plan: [ADR-0027](../decisions/0027-run-log-finalization.md), [PR plan](../plans/m6-pr2-finalization.md)
(Joe's J1 to J12), [M6 plan](../plans/m6-analysis-tooling.md) PR 2 row.
Builder: Claude (Opus 5.5)
Reviewer: Codex (`gpt-6-astra`, `model_reasoning_effort = high` from `~/.codex/config.toml`; fresh
context; adversarial review, companion 1.0.6), thread `01a0f00a-7bc3-7932-a47c-19a987810f50`
Scope: committed branch diff `origin/main` `02bddfa` to `3c5b920`, clean tree; focus: byte identity
across drain patterns, the file lifecycle under failure and kill, the no-replace rename and identity
check, and `discard_log()` as the only delete path. Read-only; the reviewer ran no builds or tests.
Validation: `./scripts/check-full.sh` from a fresh configure on `3c5b920`, exit 0, `check: OK`
(986 s). The fix batch below is validated in the PR body.
Round: fix 1 (of 2)

## Finding R-001

Severity: major (Codex: medium)
Status: fixed-unverified
Location: `core/src/kernel/run_log.cpp:643-648` at `3c5b920` (`RunLog::finalize_by_destructor`)
Requirement/ADR: ADR-0027 item 4: after any earlier failure, a `kLog` halt included, the destructor
writes nothing more and sets no marker.
Failure: a full ring halts `kLog` in `Simulation::halt_log()` without recording a write failure in
`RunLog`, so the destructor's `failed()` guard passed and it wrote the tails, `/summary`,
`/final_state` and `finalized_by = "destructor"`. Reproduction: `buffer_frames: 10`, eleven
`step()` calls without a drain, then drop the `Simulation`.
Requested outcome: no further writes or marker after a `kLog` halt; a regression for it.
Builder response: fixed. `~Simulation` calls the new `RunLog::close_after_log_halt()` when
`log_halt_` is set, which closes the handles and writes nothing through ASTRA: no tail, no closing
object, no marker. HDF5's own close of the handles still writes its cached metadata. That is the
certified structure, written by the library's close, not an ASTRA write; the codebase has no way to
close a sound HDF5 file without it, short of leaking the handles until the process exits, where
`H5close` would write the same metadata. Regression:
`RunLogClose.TheDestructorWritesNothingAfterAFullRingLogHalt` (no `complete`, no `finalized_by`, no
`/summary`, zero rows, `rows_committed = 0`).
Verification: pending (a Codex fix-verification pass).
History: fix 1, open to fixed-unverified.

## Finding R-002

Severity: major (Codex: medium)
Status: fixed-unverified
Location: `python/astra/log.py:223-231` at `3c5b920` (`replay`)
Requirement/ADR: ADR-0027 items 7 and 8, the plan's tool rule: any exception after the
`Simulation` is built discards through `discard_log()` while it is alive.
Failure: the byte comparison ran outside the cleanup `try`. A read error, or an interrupt during
the comparison, left the published `out` of a failed replay in place, with no `discard_log()`.
Requested outcome: the comparison inside the cleanup scope, J6's explicit error kept, and a
regression for a comparison failure.
Builder response: fixed. `_identical()` runs inside the `try`, so its failure goes through
`_runkit.discard_on_failure()`. The mismatch path (J6) is unchanged. Regression:
`test_log_replay_discards_its_published_out_when_the_comparison_fails`.
Verification: pending (a Codex fix-verification pass).
History: fix 1, open to fixed-unverified.

## Completion

Two findings, both fixed in fix 1 and awaiting Codex verification; none rejected or deferred. The
reviewer found the row merge and per-block flush ordering independent of drain timing, and raised
nothing on the rename, the identity check or the delete paths. Codex's recommendation is not Joe's
approval.
