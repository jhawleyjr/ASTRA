# Review: M6 PR 9, the frame-time benchmark

Spec/plan: [PR 9 plan](../plans/m6-pr9-benchmark.md); [M6 plan](../plans/m6-analysis-tooling.md) R19, Q11, C-19, N-05.
Builder: Claude (Opus 5.5)
Reviewer: Codex, through companion 1.0.6 `review --base origin/main`, in a fresh context, as a general review. The thread
is `01a0fb6e-f113-70c0-bb64-2fb7f846ef04`.
Scope: the committed branch diff from `8b4852b` (origin/main) to `5c08ca7`, with a clean tree. The reviewer read
`tests/benchmark/**`, the docs, PLAN.md and ADR-0027. It also ran `astra_frame_stats_test` in Release and in ASan;
both exited 0. It did not rerun the smoke or the gate, because its sandbox is read-only.
Validation: the full gate on `fde0e20` exited 0 with `check: OK`
([summary](evidence/fde0e20-gate-summary.log)). The Release baseline is at `c1c0976`.
Round: initial, then fix 1.

## Finding R-001

Severity: minor (Codex P2)
Status: fixed-unverified
Location: `tests/benchmark/frame_benchmark.cpp:310-315` at `5c08ca7` (`time_one_repeat`)
Requirement/ADR: [ADR-0027](../decisions/0027-run-log-finalization.md) item 8. A tool deletes its own run's file
when it handles a failure, through `discard_log()`, while its `Simulation` is alive.
Failure: several failures in a logged repeat leave before `discard_log()` runs: a halt, a `close_log()` that
throws (for example, when publication is unsupported), and a failed check. The destructor never deletes a log,
so the `.partial` or the published file stays in the benchmark's temporary directory, and so does the directory.
Requested outcome: exception-safe cleanup while the `Simulation` is alive, and the empty directory removed on
failure too.
Builder response: fixed.
- **Cleanup on failure.** `time_one_repeat` now wraps everything after construction in a try block. On any
  failure, `discard_after_failure()` calls `discard_log()`. That call is `noexcept`, never deletes by name,
  and reports on stderr anything it could not delete.
- **The original error.** A non-`Failure` exception is rethrown as a `Failure` that names the repeat.
- **The directory.** The temporary directory is now an RAII `LogDir`, removed on every way out, and only when
  it is empty.
- **Regression coverage.** A test-only option, `--inject-log-fault-for-testing rename|summary`, calls
  `inject_log_fault_for_testing` on each logged run. The smoke now checks that every run, failed or not, leaves
  nothing under its own `TMPDIR`. It also runs both faults, each expecting exit 1, the repeat named, no JSON
  and no " left " report.
- **Checks.** `ctest --preset asan -R 'FrameStats|FrameBenchmark'` passes 11/11. A manual run with the `rename`
  fault exits 1 naming the refused publication, and nothing remains under `/tmp`.
Verification: pending (Codex fix verification).
History: initial round, R-001 opened; fix 1, fixed-unverified.
Human exception if needed: none.

## Completion

No critical or major finding is open. R-001 is fixed and awaits Codex verification. There are no rejected
findings and no deferrals.
